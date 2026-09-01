// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actors/HeroArenaRecoveryField.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Character/LyraHealthComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/StaticMesh.h"
#include "Effects/HeroArenaHealingRules.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Teams/LyraTeamSubsystem.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaRecoveryField, Log, All);

AHeroArenaRecoveryField::AHeroArenaRecoveryField()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(true);
	SetNetUpdateFrequency(10.0f);
	InitialLifeSpan = FieldDuration;
	PrimaryActorTick.bCanEverTick = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	HealingVolume = CreateDefaultSubobject<USphereComponent>(TEXT("HealingVolume"));
	HealingVolume->SetupAttachment(SceneRoot);
	HealingVolume->SetSphereRadius(HealingRadius);
	HealingVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	HealingVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	HealingVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	HealingVolume->SetGenerateOverlapEvents(true);

	PadMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PadMesh"));
	PadMesh->SetupAttachment(SceneRoot);
	PadMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PadMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 2.0f));
	PadMesh->SetCastShadow(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> RingMesh(
		TEXT("/Game/Weapons/Spawnpad/Mesh/SM_launchpad_Round.SM_launchpad_Round"));
	if (RingMesh.Succeeded())
	{
		PadMesh->SetStaticMesh(RingMesh.Object);
		const FBoxSphereBounds RingBounds = RingMesh.Object->GetBounds();
		const float RingRadius = FMath::Max(RingBounds.BoxExtent.X, RingBounds.BoxExtent.Y);
		const float RingScale = RingRadius > UE_SMALL_NUMBER ? HealingRadius / RingRadius : 1.0f;
		PadMesh->SetRelativeScale3D(FVector(RingScale, RingScale, 1.0f));
	}

	StatusText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("StatusText"));
	StatusText->SetupAttachment(SceneRoot);
	StatusText->SetHorizontalAlignment(EHTA_Center);
	StatusText->SetVerticalAlignment(EVRTA_TextCenter);
	StatusText->SetWorldSize(12.0f);
	StatusText->SetRelativeLocation(FVector(0.0f, -HealingRadius * 0.70f, 32.0f));
	StatusText->SetCastShadow(false);
	StatusText->SetText(FText::FromString(TEXT("RECOVERY")));

	HealingPulseFx = CreateDefaultSubobject<UNiagaraComponent>(TEXT("HealingPulseFx"));
	HealingPulseFx->SetupAttachment(SceneRoot);
	HealingPulseFx->SetAutoActivate(false);

	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> HealFx(TEXT("/Game/Effects/Particles/Item/NS_Heal.NS_Heal"));
	if (HealFx.Succeeded())
	{
		HealingPulseFx->SetAsset(HealFx.Object);
	}
}

void AHeroArenaRecoveryField::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		if (const ULyraTeamSubsystem* TeamSubsystem = GetWorld()->GetSubsystem<ULyraTeamSubsystem>())
		{
			FieldTeamId = TeamSubsystem->FindTeamFromObject(GetInstigator());
		}

		ServerEndTime = GetWorld()->GetTimeSeconds() + FieldDuration;
		SetLifeSpan(FieldDuration);
		GetWorldTimerManager().SetTimer(HealingTimerHandle, this, &ThisClass::PulseHealing, HealingInterval, true, 0.25f);
		ForceNetUpdate();

		UE_LOG(LogHeroArenaRecoveryField, Log,
			TEXT("Recovery field deployed: %s team=%d radius=%.0f duration=%.1f authority=true"),
			*GetNameSafe(this), FieldTeamId, HealingRadius, FieldDuration);
	}
	else
	{
		UE_LOG(LogHeroArenaRecoveryField, Log,
			TEXT("Recovery field replicated to client: %s authority=false"), *GetNameSafe(this));
	}

	RefreshPresentation();
}

void AHeroArenaRecoveryField::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!IsValid(StatusText) || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	StatusText->SetText(FText::FromString(FString::Printf(TEXT("RECOVERY  %.1fs"), GetRemainingTime())));
	if (const APlayerCameraManager* CameraManager = UGameplayStatics::GetPlayerCameraManager(this, 0))
	{
		FRotator FacingRotation = CameraManager->GetCameraRotation();
		FacingRotation.Pitch = 0.0f;
		FacingRotation.Roll = 0.0f;
		FacingRotation.Yaw += 180.0f;
		StatusText->SetWorldRotation(FacingRotation);
	}
}

void AHeroArenaRecoveryField::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(HealingTimerHandle);
	UE_LOG(LogHeroArenaRecoveryField, Log,
		TEXT("Recovery field ended: %s reason=%d authority=%s"),
		*GetNameSafe(this), static_cast<int32>(EndPlayReason), HasAuthority() ? TEXT("true") : TEXT("false"));
	Super::EndPlay(EndPlayReason);
}

void AHeroArenaRecoveryField::PulseHealing()
{
	if (!HasAuthority())
	{
		return;
	}

	AActor* SourceActor = GetInstigator();
	UAbilitySystemComponent* SourceASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(SourceActor);
	const ULyraTeamSubsystem* TeamSubsystem = GetWorld()->GetSubsystem<ULyraTeamSubsystem>();
	if (!IsValid(SourceASC) || !IsValid(TeamSubsystem))
	{
		return;
	}

	TArray<AActor*> OverlappingActors;
	HealingVolume->GetOverlappingActors(OverlappingActors, APawn::StaticClass());
	bool bHealedAnyTarget = false;
	for (AActor* TargetActor : OverlappingActors)
	{
		if (!IsValid(TargetActor) || TeamSubsystem->CompareTeams(SourceActor, TargetActor) != ELyraTeamComparison::OnSameTeam)
		{
			continue;
		}

		ULyraHealthComponent* HealthComponent = ULyraHealthComponent::FindHealthComponent(TargetActor);
		const float OldHealth = HealthComponent ? HealthComponent->GetHealth() : 0.0f;
		const float ActualHeal = UHeroArenaHealingRules::ApplyStandardHealing(SourceASC, TargetActor, this, HealPerPulse);
		if (ActualHeal <= 0.0f)
		{
			continue;
		}
		bHealedAnyTarget = true;

		UE_LOG(LogHeroArenaRecoveryField, Log,
			TEXT("Recovery pulse healed %s: %.0f -> %.0f team=%d"),
			*GetNameSafe(TargetActor), OldHealth, OldHealth + ActualHeal, FieldTeamId);
	}

	if (bHealedAnyTarget)
	{
		++HealingPulseSequence;
		ForceNetUpdate();
		if (IsValid(HealingPulseFx))
		{
			HealingPulseFx->Activate(true);
		}
	}
}

float AHeroArenaRecoveryField::GetRemainingTime() const
{
	if (ServerEndTime <= 0.0f)
	{
		return FieldDuration;
	}

	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	const float ServerTime = GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	return FMath::Max(0.0f, ServerEndTime - ServerTime);
}

void AHeroArenaRecoveryField::RefreshPresentation()
{
	const bool bBlueTeam = FieldTeamId == 1;
	const FLinearColor TeamColor = bBlueTeam ? FLinearColor(0.05f, 0.45f, 1.0f) : FLinearColor(1.0f, 0.12f, 0.08f);
	StatusText->SetTextRenderColor(TeamColor.ToFColor(true));
}

void AHeroArenaRecoveryField::OnRep_FieldState()
{
	RefreshPresentation();
	UE_LOG(LogHeroArenaRecoveryField, Log,
		TEXT("Recovery field state replicated: %s team=%d remaining=%.1f"),
		*GetNameSafe(this), FieldTeamId, GetRemainingTime());
}

void AHeroArenaRecoveryField::OnRep_HealingPulse()
{
	if (IsValid(HealingPulseFx))
	{
		HealingPulseFx->Activate(true);
	}

	UE_LOG(LogHeroArenaRecoveryField, Log,
		TEXT("Recovery pulse replicated: %s sequence=%u authority=false"),
		*GetNameSafe(this), HealingPulseSequence);
}

void AHeroArenaRecoveryField::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, FieldTeamId);
	DOREPLIFETIME(ThisClass, ServerEndTime);
	DOREPLIFETIME(ThisClass, HealingPulseSequence);
}
