#include "Components/HeroArenaBattlePulseComponent.h"

#include "AbilitySystem/LyraAbilitySystemComponent.h"
#include "Character/LyraCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameModes/LyraExperienceDefinition.h"
#include "GameModes/LyraExperienceManagerComponent.h"
#include "Net/UnrealNetwork.h"
#include "Player/LyraPlayerState.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaBattlePulseComponent)

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaBattlePulse, Log, All);

UHeroArenaBattlePulseComponent::UHeroArenaBattlePulseComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = false;
}

void UHeroArenaBattlePulseComponent::BeginPlay()
{
	Super::BeginPlay();
	AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	ULyraExperienceManagerComponent* ExperienceManager = GameState
		? GameState->FindComponentByClass<ULyraExperienceManagerComponent>() : nullptr;
	if (ExperienceManager)
	{
		ExperienceManager->CallOrRegister_OnExperienceLoaded_LowPriority(
			FOnLyraExperienceLoaded::FDelegate::CreateUObject(this, &ThisClass::HandleExperienceLoaded));
	}
}

void UHeroArenaBattlePulseComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(PulseTimer);
		GetWorld()->GetTimerManager().ClearTimer(RefreshTimer);
	}
	if (GetOwner() && GetOwner()->HasAuthority()) RemovePulseFromPlayers();
	Super::EndPlay(EndPlayReason);
}

void UHeroArenaBattlePulseComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, CurrentPulse);
	DOREPLIFETIME(ThisClass, PulseEndServerTime);
	DOREPLIFETIME(ThisClass, bArcadeModeActive);
}

float UHeroArenaBattlePulseComponent::GetServerTime() const
{
	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	return GameState ? GameState->GetServerWorldTimeSeconds() : 0.0f;
}

float UHeroArenaBattlePulseComponent::GetSecondsRemaining() const
{
	return PulseEndServerTime > 0.0f ? FMath::Max(0.0f, PulseEndServerTime - GetServerTime()) : 0.0f;
}

FText UHeroArenaBattlePulseComponent::GetCurrentPulseLabel() const
{
	switch (CurrentPulse)
	{
	case EHeroArenaBattlePulse::LowGravity: return NSLOCTEXT("HeroArenaBattlePulse", "LowGravity", "低重力 · 跳跃增强");
	case EHeroArenaBattlePulse::PowerSurge: return NSLOCTEXT("HeroArenaBattlePulse", "PowerSurge", "火力脉冲 · 伤害 +12%");
	case EHeroArenaBattlePulse::RescueWindow: return NSLOCTEXT("HeroArenaBattlePulse", "RescueWindow", "救援窗口 · 治疗 +50%");
	default: return NSLOCTEXT("HeroArenaBattlePulse", "Preparing", "战场脉冲准备中");
	}
}

void UHeroArenaBattlePulseComponent::HandleExperienceLoaded(const ULyraExperienceDefinition* Experience)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Experience ||
		!Experience->GetClass()->GetName().Contains(TEXT("Arcade")))
	{
		return;
	}
	bArcadeModeActive = true;
	GetWorld()->GetTimerManager().SetTimer(PulseTimer, this, &ThisClass::AdvancePulse, 3.0f, false);
	GetWorld()->GetTimerManager().SetTimer(RefreshTimer, this, &ThisClass::RefreshPlayers, 0.5f, true);
	UE_LOG(LogHeroArenaBattlePulse, Log, TEXT("Battle Pulse arcade rules enabled: experience=%s"), *GetNameSafe(Experience));
}

void UHeroArenaBattlePulseComponent::RemovePulseFromPlayers()
{
	const FGameplayTag PowerTag = FGameplayTag::RequestGameplayTag(TEXT("Status.HeroArena.Event.PowerSurge"));
	const FGameplayTag HealingTag = FGameplayTag::RequestGameplayTag(TEXT("Status.HeroArena.HealingAmplified"));
	for (const TWeakObjectPtr<ULyraAbilitySystemComponent>& Recipient : PowerSurgeRecipients)
	{
		if (ULyraAbilitySystemComponent* ASC = Recipient.Get()) ASC->RemoveLooseGameplayTag(PowerTag);
	}
	for (const TWeakObjectPtr<ULyraAbilitySystemComponent>& Recipient : RescueWindowRecipients)
	{
		if (ULyraAbilitySystemComponent* ASC = Recipient.Get()) ASC->RemoveLooseGameplayTag(HealingTag);
	}
	PowerSurgeRecipients.Reset();
	RescueWindowRecipients.Reset();
	for (const TPair<TWeakObjectPtr<ALyraCharacter>, FMovementDefaults>& Pair : MovementDefaults)
	{
		if (ALyraCharacter* Character = Pair.Key.Get())
		{
			if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
			{
				Movement->GravityScale = Pair.Value.GravityScale;
				Movement->JumpZVelocity = Pair.Value.JumpZVelocity;
			}
		}
	}
	MovementDefaults.Reset();
}

void UHeroArenaBattlePulseComponent::AdvancePulse()
{
	if (!bArcadeModeActive || !GetOwner() || !GetOwner()->HasAuthority()) return;
	RemovePulseFromPlayers();
	CurrentPulse = CurrentPulse == EHeroArenaBattlePulse::LowGravity ? EHeroArenaBattlePulse::PowerSurge
		: CurrentPulse == EHeroArenaBattlePulse::PowerSurge ? EHeroArenaBattlePulse::RescueWindow
		: EHeroArenaBattlePulse::LowGravity;
	PulseEndServerTime = GetServerTime() + PulseDuration;
	RefreshPlayers();
	GetWorld()->GetTimerManager().SetTimer(PulseTimer, this, &ThisClass::AdvancePulse, PulseDuration, false);
	UE_LOG(LogHeroArenaBattlePulse, Log, TEXT("Battle Pulse changed: pulse=%s duration=%.1fs"),
		*GetCurrentPulseLabel().ToString(), PulseDuration);
}

void UHeroArenaBattlePulseComponent::RefreshPlayers()
{
	if (!bArcadeModeActive || !GetOwner() || !GetOwner()->HasAuthority()) return;
	AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (!GameState) return;
	const FGameplayTag PowerTag = FGameplayTag::RequestGameplayTag(TEXT("Status.HeroArena.Event.PowerSurge"));
	const FGameplayTag HealingTag = FGameplayTag::RequestGameplayTag(TEXT("Status.HeroArena.HealingAmplified"));
	for (APlayerState* BaseState : GameState->PlayerArray)
	{
		ALyraPlayerState* PlayerState = Cast<ALyraPlayerState>(BaseState);
		ULyraAbilitySystemComponent* ASC = PlayerState ? PlayerState->GetLyraAbilitySystemComponent() : nullptr;
		ALyraCharacter* Character = PlayerState ? Cast<ALyraCharacter>(PlayerState->GetPawn()) : nullptr;
		if (ASC)
		{
			if (CurrentPulse == EHeroArenaBattlePulse::PowerSurge && !ASC->HasMatchingGameplayTag(PowerTag))
			{
				ASC->AddLooseGameplayTag(PowerTag);
				PowerSurgeRecipients.Add(ASC);
			}
			if (CurrentPulse == EHeroArenaBattlePulse::RescueWindow && !ASC->HasMatchingGameplayTag(HealingTag))
			{
				ASC->AddLooseGameplayTag(HealingTag);
				RescueWindowRecipients.Add(ASC);
			}
		}
		if (CurrentPulse == EHeroArenaBattlePulse::LowGravity && Character)
		{
			UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
			if (Movement && !MovementDefaults.Contains(Character))
			{
				MovementDefaults.Add(Character, { Movement->GravityScale, Movement->JumpZVelocity });
				Movement->GravityScale *= 0.55f;
				Movement->JumpZVelocity *= 1.35f;
			}
		}
	}
}
