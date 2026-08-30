#include "Actors/HeroArenaRelayBeacon.h"

#include "AbilitySystem/Attributes/LyraHealthSet.h"
#include "AbilitySystem/LyraAbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Character/LyraHealthComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Effects/HeroArenaHealingRules.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "Hero/HeroArenaHeroDefinition.h"
#include "Hero/HeroArenaHeroSelectionComponent.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Physics/LyraCollisionChannels.h"
#include "Teams/LyraTeamSubsystem.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaRelayBeacon)

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaRelayBeacon, Log, All);

AHeroArenaRelayBeacon::AHeroArenaRelayBeacon(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bReplicates = true;
	const bool bUseSpatialReplication = FParse::Param(FCommandLine::Get(), TEXT("HeroArenaRelaySpatialReplication"));
	bAlwaysRelevant = !bUseSpatialReplication;
	if (bUseSpatialReplication)
	{
		SetNetCullDistanceSquared(FMath::Square(12000.0f));
	}
	SetReplicateMovement(true);
	SetNetUpdateFrequency(15.0f);
	InitialLifeSpan = Duration;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	BeaconMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BeaconMesh"));
	BeaconMesh->SetupAttachment(SceneRoot);
	BeaconMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	BeaconMesh->SetCollisionObjectType(ECC_WorldDynamic);
	BeaconMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	BeaconMesh->SetCollisionResponseToChannel(Lyra_TraceChannel_Weapon, ECR_Block);
	BeaconMesh->SetCollisionResponseToChannel(Lyra_TraceChannel_Weapon_Capsule, ECR_Block);
	BeaconMesh->SetCollisionResponseToChannel(Lyra_TraceChannel_Weapon_Multi, ECR_Block);
	BeaconMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	BeaconMesh->SetCastShadow(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PadMesh(
		TEXT("/Game/Weapons/Spawnpad/Mesh/SM_launchpad_Round.SM_launchpad_Round"));
	if (PadMesh.Succeeded())
	{
		BeaconMesh->SetStaticMesh(PadMesh.Object);
		BeaconMesh->SetRelativeScale3D(FVector(0.65f));
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GlowMaterial(
		TEXT("/Game/Weapons/Spawnpad/Material/MI_InternalShapeGlow.MI_InternalShapeGlow"));
	if (GlowMaterial.Succeeded())
	{
		BeaconMesh->SetMaterial(0, GlowMaterial.Object);
	}

	SupportVolume = CreateDefaultSubobject<USphereComponent>(TEXT("SupportVolume"));
	SupportVolume->SetupAttachment(SceneRoot);
	SupportVolume->SetSphereRadius(SupportRadius);
	SupportVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SupportVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	SupportVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	SupportVolume->SetGenerateOverlapEvents(true);

	StatusText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("StatusText"));
	StatusText->SetupAttachment(SceneRoot);
	StatusText->SetHorizontalAlignment(EHTA_Center);
	StatusText->SetVerticalAlignment(EVRTA_TextCenter);
	StatusText->SetWorldSize(14.0f);
	StatusText->SetRelativeLocation(FVector(0.0f, 0.0f, 85.0f));
	StatusText->SetCastShadow(false);

	SupportFx = CreateDefaultSubobject<UNiagaraComponent>(TEXT("SupportFx"));
	SupportFx->SetupAttachment(SceneRoot);
	SupportFx->SetAutoActivate(false);
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> HealFx(
		TEXT("/Game/Effects/Particles/Item/NS_Heal.NS_Heal"));
	if (HealFx.Succeeded())
	{
		SupportFx->SetAsset(HealFx.Object);
	}
	BreakEffectAsset = TSoftObjectPtr<UNiagaraSystem>(
		FSoftObjectPath(TEXT("/Game/Effects/Particles/Explosion/NS_Grenade_Explosion.NS_Grenade_Explosion")));

	AbilitySystemComponent = ObjectInitializer.CreateDefaultSubobject<ULyraAbilitySystemComponent>(
		this, TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Full);
	HealthSet = CreateDefaultSubobject<ULyraHealthSet>(TEXT("HealthSet"));
}

void AHeroArenaRelayBeacon::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	AbilitySystemComponent->InitAbilityActorInfo(this, this);
	HealthSet->OnHealthChanged.AddUObject(this, &ThisClass::HandleHealthChanged);
	HealthSet->OnOutOfHealth.AddUObject(this, &ThisClass::HandleOutOfHealth);
}

void AHeroArenaRelayBeacon::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		AbilitySystemComponent->SetNumericAttributeBase(ULyraHealthSet::GetMaxHealthAttribute(), MaxBeaconHealth);
		AbilitySystemComponent->SetNumericAttributeBase(ULyraHealthSet::GetHealthAttribute(), MaxBeaconHealth);
		ServerEndTime = GetWorld()->GetTimeSeconds() + Duration;
		SetLifeSpan(Duration);
		GetWorldTimerManager().SetTimer(SupportTimer, this, &ThisClass::PulseSupport, PulseInterval, true, 0.25f);
		if (UHeroArenaHeroSelectionComponent* Selection =
			GetOwner() ? GetOwner()->FindComponentByClass<UHeroArenaHeroSelectionComponent>() : nullptr)
		{
			Selection->OnHeroChanged.AddUniqueDynamic(this, &ThisClass::HandleOwnerHeroChanged);
		}
		ForceNetUpdate();
	}
	RefreshPresentation();
	UE_LOG(LogHeroArenaRelayBeacon, Log,
		TEXT("Relay beacon ready: beacon=%s authority=%s team=%d health=%.0f radius=%.0f duration=%.1f"),
		*GetNameSafe(this), HasAuthority() ? TEXT("true") : TEXT("false"),
		GenericTeamIdToInteger(MyTeamID), GetBeaconHealth(), SupportRadius, Duration);
}

void AHeroArenaRelayBeacon::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(SupportTimer);
	if (HasAuthority())
	{
		if (UHeroArenaHeroSelectionComponent* Selection =
			GetOwner() ? GetOwner()->FindComponentByClass<UHeroArenaHeroSelectionComponent>() : nullptr)
		{
			Selection->OnHeroChanged.RemoveDynamic(this, &ThisClass::HandleOwnerHeroChanged);
		}
	}
	UE_LOG(LogHeroArenaRelayBeacon, Log,
		TEXT("Relay beacon ended: beacon=%s reason=%d recalled=%s authority=%s"),
		*GetNameSafe(this), static_cast<int32>(EndPlayReason), bRecalled ? TEXT("true") : TEXT("false"),
		HasAuthority() ? TEXT("true") : TEXT("false"));
	Super::EndPlay(EndPlayReason);
}

void AHeroArenaRelayBeacon::Destroyed()
{
	if (!bRecalled && GetNetMode() != NM_DedicatedServer)
	{
		if (UNiagaraSystem* BreakEffect = BreakEffectAsset.LoadSynchronous())
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, BreakEffect, GetActorLocation(),
				GetActorRotation(), FVector(0.45f));
		}
	}
	Super::Destroyed();
}

UAbilitySystemComponent* AHeroArenaRelayBeacon::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void AHeroArenaRelayBeacon::SetGenericTeamId(const FGenericTeamId& NewTeamID)
{
	if (!HasAuthority())
	{
		return;
	}
	const FGenericTeamId OldTeamID = MyTeamID;
	MyTeamID = NewTeamID;
	ConditionalBroadcastTeamChanged(this, OldTeamID, MyTeamID);
	RefreshPresentation();
	ForceNetUpdate();
}

bool AHeroArenaRelayBeacon::RecallBy(AActor* Requester)
{
	if (!HasAuthority() || Requester != GetOwner())
	{
		return false;
	}
	MulticastMarkRecalled();
	UE_LOG(LogHeroArenaRelayBeacon, Log, TEXT("Relay beacon recalled: beacon=%s owner=%s"),
		*GetNameSafe(this), *GetNameSafe(Requester));
	Destroy();
	return true;
}

float AHeroArenaRelayBeacon::GetBeaconHealth() const
{
	return HealthSet ? HealthSet->GetHealth() : 0.0f;
}

float AHeroArenaRelayBeacon::GetBeaconMaxHealth() const
{
	return HealthSet ? HealthSet->GetMaxHealth() : 0.0f;
}

void AHeroArenaRelayBeacon::PulseSupport()
{
	if (!HasAuthority())
	{
		return;
	}
	UAbilitySystemComponent* SourceASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner());
	ULyraTeamSubsystem* Teams = GetWorld()->GetSubsystem<ULyraTeamSubsystem>();
	if (!SourceASC || !Teams)
	{
		return;
	}

	TArray<AActor*> Targets;
	SupportVolume->GetOverlappingActors(Targets, APawn::StaticClass());
	int32 HealedTargets = 0;
	for (AActor* Target : Targets)
	{
		if (!IsValid(Target) || Teams->CompareTeams(this, Target) != ELyraTeamComparison::OnSameTeam)
		{
			continue;
		}
		const float ActualHeal = UHeroArenaHealingRules::ApplyStandardHealing(SourceASC, Target, this, HealPerPulse);
		if (ActualHeal > 0.0f)
		{
			++HealedTargets;
			UE_LOG(LogHeroArenaRelayBeacon, Log,
				TEXT("Relay beacon healed: beacon=%s target=%s amount=%.2f"),
				*GetNameSafe(this), *GetNameSafe(Target), ActualHeal);
		}
	}

	if (HealedTargets > 0)
	{
		++SupportPulseSequence;
		ForceNetUpdate();
		if (GetNetMode() != NM_DedicatedServer)
		{
			SupportFx->Activate(true);
		}
	}
}

void AHeroArenaRelayBeacon::HandleOwnerHeroChanged(
	const UHeroArenaHeroDefinition*, const UHeroArenaHeroDefinition* NewHero)
{
	if (!HasAuthority() || (NewHero && NewHero->HeroId == TEXT("Relay")))
	{
		return;
	}

	MulticastMarkRecalled();
	UE_LOG(LogHeroArenaRelayBeacon, Log,
		TEXT("Relay beacon removed after hero switch: beacon=%s owner=%s new_hero=%s"),
		*GetNameSafe(this), *GetNameSafe(GetOwner()),
		NewHero ? *NewHero->HeroId.ToString() : TEXT("None"));
	Destroy();
}

void AHeroArenaRelayBeacon::HandleHealthChanged(AActor* DamageInstigator, AActor*, const FGameplayEffectSpec*,
	float, const float OldValue, const float NewValue)
{
	RefreshPresentation();
	UE_LOG(LogHeroArenaRelayBeacon, Log,
		TEXT("Relay beacon health: beacon=%s %.0f->%.0f instigator=%s authority=%s"),
		*GetNameSafe(this), OldValue, NewValue, *GetNameSafe(DamageInstigator),
		HasAuthority() ? TEXT("true") : TEXT("false"));
}

void AHeroArenaRelayBeacon::HandleOutOfHealth(AActor* DamageInstigator, AActor*, const FGameplayEffectSpec*,
	float, float, float)
{
	if (HasAuthority())
	{
		UE_LOG(LogHeroArenaRelayBeacon, Log,
			TEXT("Relay beacon destroyed by damage: beacon=%s instigator=%s"),
			*GetNameSafe(this), *GetNameSafe(DamageInstigator));
		Destroy();
	}
}

void AHeroArenaRelayBeacon::RefreshPresentation()
{
	if (!StatusText)
	{
		return;
	}
	const bool bBlue = GenericTeamIdToInteger(MyTeamID) == 1;
	StatusText->SetTextRenderColor((bBlue ? FLinearColor(0.05f, 0.45f, 1.0f) :
		FLinearColor(1.0f, 0.12f, 0.08f)).ToFColor(true));
	StatusText->SetText(FText::FromString(FString::Printf(TEXT("RELAY  %.0f"), GetBeaconHealth())));
}

void AHeroArenaRelayBeacon::OnRep_Team(const FGenericTeamId OldTeamID)
{
	ConditionalBroadcastTeamChanged(this, OldTeamID, MyTeamID);
	RefreshPresentation();
	UE_LOG(LogHeroArenaRelayBeacon, Log,
		TEXT("Relay beacon team replicated: beacon=%s team=%d end=%.2f health=%.0f"),
		*GetNameSafe(this), GenericTeamIdToInteger(MyTeamID), ServerEndTime, GetBeaconHealth());
}

void AHeroArenaRelayBeacon::OnRep_EndTime()
{
	UE_LOG(LogHeroArenaRelayBeacon, Log,
		TEXT("Relay beacon lifetime replicated: beacon=%s team=%d end=%.2f"),
		*GetNameSafe(this), GenericTeamIdToInteger(MyTeamID), ServerEndTime);
}

void AHeroArenaRelayBeacon::OnRep_SupportPulse()
{
	SupportFx->Activate(true);
	UE_LOG(LogHeroArenaRelayBeacon, Log,
		TEXT("Relay beacon support replicated: beacon=%s sequence=%u authority=false"),
		*GetNameSafe(this), SupportPulseSequence);
}

void AHeroArenaRelayBeacon::MulticastMarkRecalled_Implementation()
{
	bRecalled = true;
}

void AHeroArenaRelayBeacon::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, MyTeamID);
	DOREPLIFETIME(ThisClass, ServerEndTime);
	DOREPLIFETIME(ThisClass, SupportPulseSequence);
}
