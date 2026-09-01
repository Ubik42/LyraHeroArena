#include "Actors/HeroArenaGroundSlamSlow.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Components/SceneComponent.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "UObject/ConstructorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaGroundSlamSlow)

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaGroundSlamSlow, Log, All);

AHeroArenaGroundSlamSlow::AHeroArenaGroundSlamSlow()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
	SetCanBeDamaged(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	SlowFx = CreateDefaultSubobject<UNiagaraComponent>(TEXT("SlowFx"));
	SlowFx->SetupAttachment(SceneRoot);
	SlowFx->SetAutoActivate(true);
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> ImpactFx(
		TEXT("/Game/Effects/Particles/Impacts/NS_ImpactConcrete.NS_ImpactConcrete"));
	if (ImpactFx.Succeeded())
	{
		SlowFx->SetAsset(ImpactFx.Object);
	}

	StatusText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("StatusText"));
	StatusText->SetupAttachment(SceneRoot);
	StatusText->SetHorizontalAlignment(EHTA_Center);
	StatusText->SetVerticalAlignment(EVRTA_TextCenter);
	StatusText->SetRelativeLocation(FVector(0.0f, 0.0f, 120.0f));
	StatusText->SetWorldSize(13.0f);
	StatusText->SetTextRenderColor(FColor(255, 190, 40));
	StatusText->SetCastShadow(false);
	StatusText->SetText(FText::FromString(TEXT("SLOWED")));
}

void AHeroArenaGroundSlamSlow::InitializeSlow(AActor* InTargetActor, const float InMultiplier, const float InDuration)
{
	check(HasAuthority());
	TargetActor = InTargetActor;
	SpeedMultiplier = FMath::Clamp(InMultiplier, 0.1f, 1.0f);
	ServerEndTime = GetWorld()->GetTimeSeconds() + InDuration;
	SetLifeSpan(InDuration);
	ApplySlowLocally();
	ForceNetUpdate();
}

void AHeroArenaGroundSlamSlow::BeginPlay()
{
	Super::BeginPlay();
	ApplySlowLocally();
}

void AHeroArenaGroundSlamSlow::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RestoreSpeedLocally();
	Super::EndPlay(EndPlayReason);
}

void AHeroArenaGroundSlamSlow::OnRep_State()
{
	ApplySlowLocally();
}

void AHeroArenaGroundSlamSlow::ApplySlowLocally()
{
	ACharacter* Character = Cast<ACharacter>(TargetActor);
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!IsValid(Movement) || bAppliedLocally)
	{
		return;
	}

	OriginalMaxWalkSpeed = Movement->MaxWalkSpeed;
	AppliedMaxWalkSpeed = OriginalMaxWalkSpeed * SpeedMultiplier;
	Movement->MaxWalkSpeed = AppliedMaxWalkSpeed;
	bAppliedLocally = true;
	int32 StatusTagCount = -1;
	if (UAbilitySystemComponent* AbilitySystem =
		UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(TargetActor))
	{
		const FGameplayTag StatusTag =
			FGameplayTag::RequestGameplayTag(TEXT("Status.HeroArena.GroundSlamSlowed"));
		AbilitySystem->AddLooseGameplayTag(StatusTag);
		StatusTagCount = AbilitySystem->GetTagCount(StatusTag);
	}
	AttachToActor(TargetActor, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	UE_LOG(LogHeroArenaGroundSlamSlow, Log,
		TEXT("Ground slam slow applied: target=%s speed=%.0f->%.0f status_count=%d authority=%s"),
		*GetNameSafe(TargetActor), OriginalMaxWalkSpeed, AppliedMaxWalkSpeed,
		StatusTagCount,
		HasAuthority() ? TEXT("true") : TEXT("false"));
}

void AHeroArenaGroundSlamSlow::RestoreSpeedLocally()
{
	ACharacter* Character = Cast<ACharacter>(TargetActor);
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!bAppliedLocally)
	{
		return;
	}
	int32 StatusTagCount = -1;
	if (UAbilitySystemComponent* AbilitySystem =
		UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(TargetActor))
	{
		const FGameplayTag StatusTag =
			FGameplayTag::RequestGameplayTag(TEXT("Status.HeroArena.GroundSlamSlowed"));
		AbilitySystem->RemoveLooseGameplayTag(StatusTag);
		StatusTagCount = AbilitySystem->GetTagCount(StatusTag);
	}
	if (!IsValid(Movement))
	{
		bAppliedLocally = false;
		return;
	}

	// Do not clobber a newer movement modifier that replaced the slam value.
	if (FMath::IsNearlyEqual(Movement->MaxWalkSpeed, AppliedMaxWalkSpeed, 0.5f))
	{
		Movement->MaxWalkSpeed = OriginalMaxWalkSpeed;
	}
	UE_LOG(LogHeroArenaGroundSlamSlow, Log,
		TEXT("Ground slam slow ended: target=%s speed=%.0f status_count=%d authority=%s"),
		*GetNameSafe(TargetActor), Movement->MaxWalkSpeed, StatusTagCount,
		HasAuthority() ? TEXT("true") : TEXT("false"));
	bAppliedLocally = false;
}

void AHeroArenaGroundSlamSlow::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, TargetActor);
	DOREPLIFETIME(ThisClass, SpeedMultiplier);
	DOREPLIFETIME(ThisClass, ServerEndTime);
}
