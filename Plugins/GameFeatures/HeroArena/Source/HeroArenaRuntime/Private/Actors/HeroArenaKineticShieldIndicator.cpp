#include "Actors/HeroArenaKineticShieldIndicator.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/SceneComponent.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "UObject/ConstructorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaKineticShieldIndicator)

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaKineticShieldIndicator, Log, All);

AHeroArenaKineticShieldIndicator::AHeroArenaKineticShieldIndicator()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
	SetCanBeDamaged(false);
	PrimaryActorTick.bCanEverTick = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	ShieldFx = CreateDefaultSubobject<UNiagaraComponent>(TEXT("ShieldFx"));
	ShieldFx->SetupAttachment(SceneRoot);
	ShieldFx->SetAutoActivate(true);
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> ShieldSystem(
		TEXT("/Game/Effects/Particles/Environmental/NS_ElectricMovement.NS_ElectricMovement"));
	if (ShieldSystem.Succeeded())
	{
		ShieldFx->SetAsset(ShieldSystem.Object);
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

void AHeroArenaKineticShieldIndicator::InitializeIndicator(AActor* InTargetActor, const float InShieldAmount,
	const float InDuration)
{
	check(HasAuthority());
	TargetActor = InTargetActor;
	CurrentShield = InShieldAmount;
	MaxShield = InShieldAmount;
	ServerEndTime = GetWorld()->GetTimeSeconds() + InDuration;
	SetLifeSpan(InDuration + 0.5f);
	RefreshPresentation();
	ForceNetUpdate();
	UE_LOG(LogHeroArenaKineticShieldIndicator, Log,
		TEXT("Kinetic shield indicator initialized: target=%s shield=%.0f duration=%.1f authority=true"),
		*GetNameSafe(TargetActor), CurrentShield, InDuration);
}

void AHeroArenaKineticShieldIndicator::SetShieldAmount(const float InShieldAmount)
{
	check(HasAuthority());
	CurrentShield = FMath::Clamp(InShieldAmount, 0.0f, MaxShield);
	RefreshPresentation();
	ForceNetUpdate();
}

void AHeroArenaKineticShieldIndicator::BeginPlay()
{
	Super::BeginPlay();
	RefreshPresentation();
}

void AHeroArenaKineticShieldIndicator::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	RefreshPresentation();
	if (const APlayerCameraManager* CameraManager = UGameplayStatics::GetPlayerCameraManager(this, 0))
	{
		FRotator FacingRotation = CameraManager->GetCameraRotation();
		FacingRotation.Pitch = 0.0f;
		FacingRotation.Roll = 0.0f;
		FacingRotation.Yaw += 180.0f;
		StatusText->SetWorldRotation(FacingRotation);
	}
}

void AHeroArenaKineticShieldIndicator::OnRep_State()
{
	RefreshPresentation();
	UE_LOG(LogHeroArenaKineticShieldIndicator, Log,
		TEXT("Kinetic shield state replicated: target=%s shield=%.0f/%.0f remaining=%.1f"),
		*GetNameSafe(TargetActor), CurrentShield, MaxShield, GetRemainingTime());
}

void AHeroArenaKineticShieldIndicator::RefreshPresentation()
{
	if (IsValid(TargetActor) && GetAttachParentActor() != TargetActor)
	{
		AttachToActor(TargetActor, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	}
	if (StatusText)
	{
		StatusText->SetText(FText::FromString(FString::Printf(
			TEXT("SHIELD  %.0f/%.0f  %.1fs"), CurrentShield, MaxShield, GetRemainingTime())));
	}
}

float AHeroArenaKineticShieldIndicator::GetRemainingTime() const
{
	if (ServerEndTime <= 0.0f || !GetWorld())
	{
		return 0.0f;
	}
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	const float ServerTime = GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	return FMath::Max(0.0f, ServerEndTime - ServerTime);
}

void AHeroArenaKineticShieldIndicator::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, TargetActor);
	DOREPLIFETIME(ThisClass, CurrentShield);
	DOREPLIFETIME(ThisClass, MaxShield);
	DOREPLIFETIME(ThisClass, ServerEndTime);
}
