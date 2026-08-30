// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actors/HeroArenaWeaponOverloadIndicator.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/SceneComponent.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaWeaponOverloadIndicator, Log, All);

AHeroArenaWeaponOverloadIndicator::AHeroArenaWeaponOverloadIndicator()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
	PrimaryActorTick.bCanEverTick = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	ElectricFx = CreateDefaultSubobject<UNiagaraComponent>(TEXT("ElectricFx"));
	ElectricFx->SetupAttachment(SceneRoot);
	ElectricFx->SetAutoActivate(true);
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> ElectricSystem(
		TEXT("/Game/Effects/Particles/Environmental/NS_ElectricMovement.NS_ElectricMovement"));
	if (ElectricSystem.Succeeded())
	{
		ElectricFx->SetAsset(ElectricSystem.Object);
	}

	StatusText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("StatusText"));
	StatusText->SetupAttachment(SceneRoot);
	StatusText->SetHorizontalAlignment(EHTA_Center);
	StatusText->SetVerticalAlignment(EVRTA_TextCenter);
	StatusText->SetRelativeLocation(FVector(0.0f, 0.0f, 135.0f));
	StatusText->SetWorldSize(11.0f);
	StatusText->SetTextRenderColor(FColor(80, 220, 255));
	StatusText->SetCastShadow(false);
}

void AHeroArenaWeaponOverloadIndicator::InitializeIndicator(AActor* InTargetActor, const float InDuration)
{
	check(HasAuthority());
	TargetActor = InTargetActor;
	ServerEndTime = GetWorld()->GetTimeSeconds() + InDuration;
	SetLifeSpan(InDuration);
	RefreshAttachment();
	ForceNetUpdate();

	UE_LOG(LogHeroArenaWeaponOverloadIndicator, Log,
		TEXT("Weapon overload indicator initialized: target=%s duration=%.1f authority=true"),
		*GetNameSafe(TargetActor), InDuration);
}

void AHeroArenaWeaponOverloadIndicator::BeginPlay()
{
	Super::BeginPlay();
	RefreshAttachment();
	if (!HasAuthority())
	{
		UE_LOG(LogHeroArenaWeaponOverloadIndicator, Log,
			TEXT("Weapon overload indicator replicated: target=%s authority=false"), *GetNameSafe(TargetActor));
	}
}

void AHeroArenaWeaponOverloadIndicator::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	StatusText->SetText(FText::FromString(FString::Printf(TEXT("OVERLOAD  %.1fs"), GetRemainingTime())));
	if (const APlayerCameraManager* CameraManager = UGameplayStatics::GetPlayerCameraManager(this, 0))
	{
		FRotator FacingRotation = CameraManager->GetCameraRotation();
		FacingRotation.Pitch = 0.0f;
		FacingRotation.Roll = 0.0f;
		FacingRotation.Yaw += 180.0f;
		StatusText->SetWorldRotation(FacingRotation);
	}
}

void AHeroArenaWeaponOverloadIndicator::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UE_LOG(LogHeroArenaWeaponOverloadIndicator, Log,
		TEXT("Weapon overload indicator ended: target=%s reason=%d authority=%s"),
		*GetNameSafe(TargetActor), static_cast<int32>(EndPlayReason), HasAuthority() ? TEXT("true") : TEXT("false"));
	Super::EndPlay(EndPlayReason);
}

void AHeroArenaWeaponOverloadIndicator::OnRep_IndicatorState()
{
	RefreshAttachment();
	UE_LOG(LogHeroArenaWeaponOverloadIndicator, Log,
		TEXT("Weapon overload state replicated: target=%s remaining=%.1f"),
		*GetNameSafe(TargetActor), GetRemainingTime());
}

void AHeroArenaWeaponOverloadIndicator::RefreshAttachment()
{
	if (IsValid(TargetActor) && GetAttachParentActor() != TargetActor)
	{
		AttachToActor(TargetActor, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	}
}

float AHeroArenaWeaponOverloadIndicator::GetRemainingTime() const
{
	if (ServerEndTime <= 0.0f)
	{
		return 0.0f;
	}
	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	const float ServerTime = GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	return FMath::Max(0.0f, ServerEndTime - ServerTime);
}

void AHeroArenaWeaponOverloadIndicator::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, TargetActor);
	DOREPLIFETIME(ThisClass, ServerEndTime);
}
