#include "Actors/HeroArenaBarrier.h"

#include "AbilitySystem/Attributes/LyraHealthSet.h"
#include "AbilitySystem/LyraAbilitySystemComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Physics/LyraCollisionChannels.h"
#include "Projectiles/HeroArenaSpiralProjectileComponent.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaBarrier)

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaBarrier, Log, All);

AHeroArenaBarrier::AHeroArenaBarrier(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(true);
	SetNetUpdateFrequency(20.0f);
	InitialLifeSpan = 12.0f;

	BarrierMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BarrierMesh"));
	SetRootComponent(BarrierMesh);
	BarrierMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	BarrierMesh->SetCollisionObjectType(ECC_WorldDynamic);
	BarrierMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	BarrierMesh->SetCollisionResponseToChannel(Lyra_TraceChannel_Weapon, ECR_Block);
	BarrierMesh->SetCollisionResponseToChannel(Lyra_TraceChannel_Weapon_Capsule, ECR_Block);
	BarrierMesh->SetCollisionResponseToChannel(Lyra_TraceChannel_Weapon_Multi, ECR_Block);
	// Lyra grenades use a WorldDynamic collision body. Pawns remain ignored, so
	// teammates can walk through while physical projectiles bounce or detonate.
	BarrierMesh->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	BarrierMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	BarrierMesh->SetGenerateOverlapEvents(false);
	BarrierMesh->SetCastShadow(false);
	BarrierMesh->SetRelativeScale3D(FVector(0.12f, 2.2f, 1.25f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		BarrierMesh->SetStaticMesh(CubeMesh.Object);
	}

	BarrierImpactEffectAsset = TSoftObjectPtr<UNiagaraSystem>(
		FSoftObjectPath(TEXT("/Game/Effects/Particles/Impacts/NS_ImpactConcrete.NS_ImpactConcrete")));
	BarrierBreakEffectAsset = TSoftObjectPtr<UNiagaraSystem>(
		FSoftObjectPath(TEXT("/Game/Effects/Particles/Explosion/NS_Grenade_Explosion.NS_Grenade_Explosion")));
	BarrierMaterialAsset = TSoftObjectPtr<UMaterialInterface>(
		FSoftObjectPath(TEXT("/Game/Weapons/Spawnpad/Material/MI_InternalShapeGlow.MI_InternalShapeGlow")));

	StatusText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("StatusText"));
	StatusText->SetupAttachment(BarrierMesh);
	StatusText->SetHorizontalAlignment(EHTA_Center);
	StatusText->SetVerticalAlignment(EVRTA_TextCenter);
	StatusText->SetWorldSize(20.0f);
	StatusText->SetRelativeLocation(FVector(-55.0f, 0.0f, 0.0f));
	StatusText->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
	StatusText->SetCastShadow(false);

	AbilitySystemComponent = ObjectInitializer.CreateDefaultSubobject<ULyraAbilitySystemComponent>(this, TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Full);
	HealthSet = CreateDefaultSubobject<ULyraHealthSet>(TEXT("HealthSet"));
}

void AHeroArenaBarrier::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	AbilitySystemComponent->InitAbilityActorInfo(this, this);
	HealthSet->OnHealthChanged.AddUObject(this, &ThisClass::HandleHealthChanged);
	HealthSet->OnOutOfHealth.AddUObject(this, &ThisClass::HandleOutOfHealth);
}

void AHeroArenaBarrier::BeginPlay()
{
	Super::BeginPlay();
	if (UMaterialInterface* BaseMaterial = BarrierMaterialAsset.LoadSynchronous())
	{
		BarrierMesh->SetMaterial(0, BaseMaterial);
	}
	BarrierMaterial = BarrierMesh ? BarrierMesh->CreateAndSetMaterialInstanceDynamic(0) : nullptr;
	if (IsValid(BarrierMaterial))
	{
		BarrierMaterial->SetScalarParameterValue(TEXT("Intensity"), 3.5f);
	}
	BarrierImpactEffect = BarrierImpactEffectAsset.LoadSynchronous();
	BarrierBreakEffect = BarrierBreakEffectAsset.LoadSynchronous();

	if (HasAuthority())
	{
		AbilitySystemComponent->SetNumericAttributeBase(ULyraHealthSet::GetMaxHealthAttribute(), MaxBarrierHealth);
		AbilitySystemComponent->SetNumericAttributeBase(ULyraHealthSet::GetHealthAttribute(), MaxBarrierHealth);
		ForceNetUpdate();
	}

	RefreshPresentation();
	if (GetNetMode() != NM_DedicatedServer)
	{
		RefreshLocalCameraOcclusion();
		GetWorldTimerManager().SetTimer(LocalCameraOcclusionTimer, this,
			&ThisClass::RefreshLocalCameraOcclusion, 0.05f, true);
	}
	UE_LOG(LogHeroArenaBarrier, Log, TEXT("Barrier ready: %s authority=%s team=%d health=%.0f"),
		*GetNameSafe(this), HasAuthority() ? TEXT("true") : TEXT("false"), GenericTeamIdToInteger(MyTeamID), GetBarrierHealth());
}

void AHeroArenaBarrier::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(LocalCameraOcclusionTimer);
	Super::EndPlay(EndPlayReason);
}

void AHeroArenaBarrier::Destroyed()
{
	if (IsValid(BarrierBreakEffect) && GetWorld())
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, BarrierBreakEffect, GetActorLocation(),
			GetActorRotation(), FVector(0.35f), true, true, ENCPoolMethod::AutoRelease);
	}

	Super::Destroyed();
}

UAbilitySystemComponent* AHeroArenaBarrier::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void AHeroArenaBarrier::SetGenericTeamId(const FGenericTeamId& NewTeamID)
{
	if (!HasAuthority())
	{
		UE_LOG(LogHeroArenaBarrier, Warning, TEXT("Rejected non-authority team change for %s"), *GetNameSafe(this));
		return;
	}

	const FGenericTeamId OldTeamID = MyTeamID;
	MyTeamID = NewTeamID;
	ConditionalBroadcastTeamChanged(this, OldTeamID, MyTeamID);
	RefreshPresentation();
	ForceNetUpdate();
}

float AHeroArenaBarrier::GetBarrierHealth() const
{
	return HealthSet ? HealthSet->GetHealth() : 0.0f;
}

float AHeroArenaBarrier::GetBarrierMaxHealth() const
{
	return HealthSet ? HealthSet->GetMaxHealth() : 0.0f;
}

void AHeroArenaBarrier::NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp,
	const bool bSelfMoved, const FVector HitLocation, const FVector HitNormal, const FVector NormalImpulse, const FHitResult& Hit)
{
	// This Actor-level callback is the proven collision surface for the duplicated
	// Lyra grenade. The projectile component owns team validation and damage so
	// normal Lyra world/Pawn hit semantics remain unchanged.
	if (HasAuthority() && Other)
	{
		if (UHeroArenaSpiralProjectileComponent* SpiralComponent =
			Other->FindComponentByClass<UHeroArenaSpiralProjectileComponent>())
		{
			SpiralComponent->TryApplyBarrierImpact(this, Hit);
		}
	}

	Super::NotifyHit(MyComp, Other, OtherComp, bSelfMoved, HitLocation, HitNormal, NormalImpulse, Hit);
	UE_LOG(LogHeroArenaBarrier, Log, TEXT("Barrier collision: %s other=%s other_component=%s authority=%s location=%s"),
		*GetNameSafe(this), *GetNameSafe(Other), *GetNameSafe(OtherComp),
		HasAuthority() ? TEXT("true") : TEXT("false"), *HitLocation.ToCompactString());
}

void AHeroArenaBarrier::HandleHealthChanged(AActor* DamageInstigator, AActor* DamageCauser,
	const FGameplayEffectSpec* DamageEffectSpec, const float DamageMagnitude, const float OldValue, const float NewValue)
{
	RefreshPresentation();
	if (NewValue < OldValue)
	{
		PlayDamageFeedback();
	}
	UE_LOG(LogHeroArenaBarrier, Log, TEXT("Barrier health: %s %.0f -> %.0f instigator=%s authority=%s"),
		*GetNameSafe(this), OldValue, NewValue, *GetNameSafe(DamageInstigator), HasAuthority() ? TEXT("true") : TEXT("false"));
}

void AHeroArenaBarrier::HandleOutOfHealth(AActor* DamageInstigator, AActor* DamageCauser,
	const FGameplayEffectSpec* DamageEffectSpec, const float DamageMagnitude, const float OldValue, const float NewValue)
{
	if (HasAuthority())
	{
		UE_LOG(LogHeroArenaBarrier, Log, TEXT("Barrier destroyed by damage: %s instigator=%s"),
			*GetNameSafe(this), *GetNameSafe(DamageInstigator));
		Destroy();
	}
}

void AHeroArenaBarrier::PlayDamageFeedback()
{
	if (IsValid(BarrierMaterial))
	{
		BarrierMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(4.0f, 4.0f, 4.0f, 1.0f));
		BarrierMaterial->SetScalarParameterValue(TEXT("Intensity"), 12.0f);
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(DamageFlashTimer, this, &ThisClass::RestoreTeamColor, 0.12f, false);
		const double Now = World->GetTimeSeconds();
		if (IsValid(BarrierImpactEffect) && (LastImpactFeedbackTime < 0.0 || Now - LastImpactFeedbackTime >= 0.08))
		{
			LastImpactFeedbackTime = Now;
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, BarrierImpactEffect, GetActorLocation(),
				GetActorRotation(), FVector(0.65f), true, true, ENCPoolMethod::AutoRelease);
		}
	}
}

void AHeroArenaBarrier::RestoreTeamColor()
{
	if (IsValid(BarrierMaterial))
	{
		BarrierMaterial->SetVectorParameterValue(TEXT("Color"), GetTeamColor());
		BarrierMaterial->SetScalarParameterValue(TEXT("Intensity"), 3.5f);
	}
}

FLinearColor AHeroArenaBarrier::GetTeamColor() const
{
	return GenericTeamIdToInteger(MyTeamID) == 1
		? FLinearColor(0.025f, 0.22f, 1.0f, 1.0f)
		: FLinearColor(1.0f, 0.025f, 0.04f, 1.0f);
}

void AHeroArenaBarrier::RefreshPresentation()
{
	if (!StatusText)
	{
		return;
	}

	const FLinearColor TeamColor = GetTeamColor();
	StatusText->SetTextRenderColor(TeamColor.ToFColor(true));
	StatusText->SetText(FText::FromString(FString::Printf(TEXT("BARRIER  %.0f"), GetBarrierHealth())));
	if (IsValid(BarrierMaterial) && !GetWorldTimerManager().IsTimerActive(DamageFlashTimer))
	{
		BarrierMaterial->SetVectorParameterValue(TEXT("Color"), TeamColor);
	}
}

void AHeroArenaBarrier::RefreshLocalCameraOcclusion()
{
	bool bShouldHide = false;
	const APlayerCameraManager* CameraManager = UGameplayStatics::GetPlayerCameraManager(this, 0);
	const AActor* ViewTarget = CameraManager ? CameraManager->GetViewTarget() : nullptr;
	const UStaticMesh* StaticMesh = BarrierMesh ? BarrierMesh->GetStaticMesh() : nullptr;
	if (IsValid(CameraManager) && IsValid(ViewTarget) && IsValid(StaticMesh))
	{
		const FVector BarrierLocation = GetActorLocation();
		const FVector BarrierNormal = GetActorForwardVector();
		const FVector CameraLocation = CameraManager->GetCameraLocation();
		const FVector TargetLocation = ViewTarget->GetActorLocation();
		const float CameraSide = FVector::DotProduct(CameraLocation - BarrierLocation, BarrierNormal);
		const float TargetSide = FVector::DotProduct(TargetLocation - BarrierLocation, BarrierNormal);

		// Hide only when the barrier plane actually lies between the local camera and
		// its view target, and the segment crosses the visible mesh rectangle.
		if (CameraSide * TargetSide < 0.0f)
		{
			const float Denominator = CameraSide - TargetSide;
			if (!FMath::IsNearlyZero(Denominator))
			{
				const float Alpha = CameraSide / Denominator;
				const FVector Intersection = FMath::Lerp(CameraLocation, TargetLocation, Alpha);
				const FVector LocalIntersection = BarrierMesh->GetComponentTransform().InverseTransformPosition(Intersection);
				const FVector LocalExtent = StaticMesh->GetBounds().BoxExtent;
				bShouldHide = FMath::Abs(LocalIntersection.Y) <= LocalExtent.Y + 12.0f &&
					FMath::Abs(LocalIntersection.Z) <= LocalExtent.Z + 12.0f;
			}
		}
	}

	if (bHiddenForLocalCamera != bShouldHide)
	{
		bHiddenForLocalCamera = bShouldHide;
		BarrierMesh->SetVisibility(!bHiddenForLocalCamera, true);
		UE_LOG(LogHeroArenaBarrier, Verbose, TEXT("Barrier local camera visibility: %s hidden=%s"),
			*GetNameSafe(this), bHiddenForLocalCamera ? TEXT("true") : TEXT("false"));
	}
}

void AHeroArenaBarrier::OnRep_MyTeamID(const FGenericTeamId OldTeamID)
{
	ConditionalBroadcastTeamChanged(this, OldTeamID, MyTeamID);
	RefreshPresentation();
}

void AHeroArenaBarrier::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, MyTeamID);
}
