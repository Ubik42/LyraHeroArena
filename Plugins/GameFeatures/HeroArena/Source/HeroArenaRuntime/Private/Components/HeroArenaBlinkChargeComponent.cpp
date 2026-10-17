#include "Components/HeroArenaBlinkChargeComponent.h"

#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Hero/HeroArenaHeroDefinition.h"
#include "Hero/HeroArenaHeroSelectionComponent.h"
#include "Messages/LyraVerbMessage.h"
#include "Messages/LyraVerbMessageHelpers.h"
#include "NativeGameplayTags.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaBlinkChargeComponent)

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaBlinkCharges, Log, All);

namespace HeroArenaMomentumRecovery
{
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_DamageMessage, "Lyra.Damage.Message");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_EliminationMessage, "Lyra.Elimination.Message");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_AssistMessage, "Lyra.Assist.Message");
}

UHeroArenaBlinkChargeComponent::UHeroArenaBlinkChargeComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsReplicatedByDefault(true);
	ChargeState.CurrentCharges = MaxCharges;
}

void UHeroArenaBlinkChargeComponent::BeginPlay()
{
	Super::BeginPlay();

	APlayerState* PlayerState = Cast<APlayerState>(GetOwner());
	if (!PlayerState)
	{
		return;
	}
	PlayerState->OnPawnSet.AddUniqueDynamic(this, &ThisClass::HandlePawnSet);
	if (UHeroArenaHeroSelectionComponent* Selection =
		PlayerState->FindComponentByClass<UHeroArenaHeroSelectionComponent>())
	{
		Selection->OnHeroChanged.AddUniqueDynamic(this, &ThisClass::HandleHeroChanged);
	}
	if (PlayerState->HasAuthority())
	{
		UGameplayMessageSubsystem& Messages = UGameplayMessageSubsystem::Get(this);
		DamageListenerHandle = Messages.RegisterListener(
			HeroArenaMomentumRecovery::TAG_DamageMessage, this, &ThisClass::HandleDamageMessage);
		EliminationListenerHandle = Messages.RegisterListener(
			HeroArenaMomentumRecovery::TAG_EliminationMessage, this, &ThisClass::HandleEliminationMessage);
		AssistListenerHandle = Messages.RegisterListener(
			HeroArenaMomentumRecovery::TAG_AssistMessage, this, &ThisClass::HandleAssistMessage);
		ResetCharges(TEXT("begin_play"));
	}
}

void UHeroArenaBlinkChargeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	PendingPredictionKeys.Reset();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RechargeTimerHandle);
	}
	if (DamageListenerHandle.IsValid())
	{
		UGameplayMessageSubsystem::Get(this).UnregisterListener(DamageListenerHandle);
	}
	if (EliminationListenerHandle.IsValid())
	{
		UGameplayMessageSubsystem::Get(this).UnregisterListener(EliminationListenerHandle);
	}
	if (AssistListenerHandle.IsValid())
	{
		UGameplayMessageSubsystem::Get(this).UnregisterListener(AssistListenerHandle);
	}
	if (APlayerState* PlayerState = Cast<APlayerState>(GetOwner()))
	{
		PlayerState->OnPawnSet.RemoveDynamic(this, &ThisClass::HandlePawnSet);
		if (UHeroArenaHeroSelectionComponent* Selection =
			PlayerState->FindComponentByClass<UHeroArenaHeroSelectionComponent>())
		{
			Selection->OnHeroChanged.RemoveDynamic(this, &ThisClass::HandleHeroChanged);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void UHeroArenaBlinkChargeComponent::HandleDamageMessage(
	FGameplayTag Channel, const FLyraVerbMessage& Payload)
{
	APlayerState* PlayerState = Cast<APlayerState>(GetOwner());
	APlayerState* Instigator = ULyraVerbMessageHelpers::GetPlayerStateFromObject(Payload.Instigator);
	APlayerState* Target = ULyraVerbMessageHelpers::GetPlayerStateFromObject(Payload.Target);
	UWorld* World = GetWorld();
	if (!PlayerState || !PlayerState->HasAuthority() || Instigator != PlayerState ||
		Target == PlayerState || Payload.Magnitude <= 0.0 || !World || !IsPhaseSelected())
	{
		return;
	}
	const float Now = World->GetTimeSeconds();
	if (Now - LastDamageRewardServerTime < DamageRewardInterval)
	{
		return;
	}
	if (AdvanceCurrentRecharge(DamageRechargeReduction, TEXT("damage")))
	{
		LastDamageRewardServerTime = Now;
	}
}

void UHeroArenaBlinkChargeComponent::HandleEliminationMessage(
	FGameplayTag Channel, const FLyraVerbMessage& Payload)
{
	APlayerState* PlayerState = Cast<APlayerState>(GetOwner());
	if (PlayerState && PlayerState->HasAuthority() &&
		ULyraVerbMessageHelpers::GetPlayerStateFromObject(Payload.Instigator) == PlayerState &&
		ULyraVerbMessageHelpers::GetPlayerStateFromObject(Payload.Target) != PlayerState)
	{
		AdvanceCurrentRecharge(EliminationRechargeReduction, TEXT("elimination"));
	}
}

void UHeroArenaBlinkChargeComponent::HandleAssistMessage(
	FGameplayTag Channel, const FLyraVerbMessage& Payload)
{
	APlayerState* PlayerState = Cast<APlayerState>(GetOwner());
	if (PlayerState && PlayerState->HasAuthority() &&
		ULyraVerbMessageHelpers::GetPlayerStateFromObject(Payload.Instigator) == PlayerState)
	{
		AdvanceCurrentRecharge(AssistRechargeReduction, TEXT("assist"));
	}
}

bool UHeroArenaBlinkChargeComponent::AdvanceCurrentRecharge(float Seconds, const TCHAR* Reason)
{
	APlayerState* PlayerState = Cast<APlayerState>(GetOwner());
	UWorld* World = GetWorld();
	if (!PlayerState || !PlayerState->HasAuthority() || !World || !IsPhaseSelected() ||
		ChargeState.CurrentCharges >= MaxCharges || ChargeState.NextChargeReadyServerTime <= 0.0f || Seconds <= 0.0f)
	{
		return false;
	}

	const float PreviousRemaining = GetNextChargeRemaining();
	const float NewRemaining = FMath::Max(0.0f, PreviousRemaining - Seconds);
	World->GetTimerManager().ClearTimer(RechargeTimerHandle);
	if (NewRemaining <= KINDA_SMALL_NUMBER)
	{
		RestoreNextCharge();
	}
	else
	{
		const AGameStateBase* GameState = World->GetGameState();
		const float ServerTime = GameState ? GameState->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
		ChargeState.NextChargeReadyServerTime = ServerTime + NewRemaining;
		World->GetTimerManager().SetTimer(
			RechargeTimerHandle, this, &ThisClass::RestoreNextCharge, NewRemaining, false);
		PlayerState->ForceNetUpdate();
		BroadcastState();
	}
	UE_LOG(LogHeroArenaBlinkCharges, Log,
		TEXT("Blink recharge advanced: player=%s reason=%s reduction=%.2f remaining=%.2f->%.2f charges=%d/%d"),
		*GetNameSafe(PlayerState), Reason, Seconds, PreviousRemaining, GetNextChargeRemaining(),
		ChargeState.CurrentCharges, MaxCharges);
	return true;
}

void UHeroArenaBlinkChargeComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(ThisClass, ChargeState, COND_OwnerOnly);
}

bool UHeroArenaBlinkChargeComponent::HasAvailableCharge() const
{
	return IsPhaseSelected() && GetCurrentCharges() > 0;
}

bool UHeroArenaBlinkChargeComponent::TryConsumeCharge(FPredictionKey PredictionKey)
{
	APlayerState* PlayerState = Cast<APlayerState>(GetOwner());
	if (!PlayerState || !HasAvailableCharge())
	{
		return false;
	}

	if (PlayerState->HasAuthority())
	{
		const bool bWasFull = ChargeState.CurrentCharges == MaxCharges;
		--ChargeState.CurrentCharges;
		ResolvePrediction(PredictionKey);
		if (bWasFull)
		{
			ScheduleNextRecharge();
		}
		PlayerState->ForceNetUpdate();
		BroadcastState();
		UE_LOG(LogHeroArenaBlinkCharges, Log,
			TEXT("Blink charge consumed: player=%s charges=%d/%d next=%.2f authority=true"),
			*GetNameSafe(PlayerState), ChargeState.CurrentCharges, MaxCharges, GetNextChargeRemaining());
		return true;
	}

	APawn* Pawn = PlayerState->GetPawn();
	if (!Pawn || !Pawn->IsLocallyControlled() || !PredictionKey.IsValidKey() ||
		PendingPredictionKeys.Contains(PredictionKey.Current))
	{
		return false;
	}
	const int16 Key = PredictionKey.Current;
	const uint32 Epoch = ChargeState.ResetEpoch;
	PendingPredictionKeys.Add(Key);
	PredictionKey.NewRejectedDelegate().BindWeakLambda(this, [this, Key, Epoch]()
	{
		if (ChargeState.ResetEpoch == Epoch && PendingPredictionKeys.RemoveSingle(Key) > 0)
		{
			BroadcastState();
			UE_LOG(LogHeroArenaBlinkCharges, Log,
				TEXT("Blink prediction rejected: key=%d visible=%d authoritative=%d pending=%d"),
				Key, GetCurrentCharges(), ChargeState.CurrentCharges, PendingPredictionKeys.Num());
		}
	});
	BroadcastState();
	UE_LOG(LogHeroArenaBlinkCharges, Log,
		TEXT("Blink charge predicted: player=%s charges=%d/%d next=%.2f authority=false"),
		*GetNameSafe(PlayerState), GetCurrentCharges(), MaxCharges, GetNextChargeRemaining());
	return true;
}

void UHeroArenaBlinkChargeComponent::ResolvePrediction(const FPredictionKey& PredictionKey)
{
	APlayerState* PlayerState = Cast<APlayerState>(GetOwner());
	if (PlayerState && PlayerState->HasAuthority() && PredictionKey.IsValidKey() && !PredictionKey.IsServerInitiatedKey())
	{
		ChargeState.LastResolvedPredictionKey = PredictionKey.Current;
		PlayerState->ForceNetUpdate();
	}
}

float UHeroArenaBlinkChargeComponent::GetNextChargeRemaining() const
{
	if (ChargeState.NextChargeReadyServerTime <= 0.0f)
	{
		return 0.0f;
	}
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	const float ServerTime = GameState ? GameState->GetServerWorldTimeSeconds() : (World ? World->GetTimeSeconds() : 0.0f);
	return FMath::Max(0.0f, ChargeState.NextChargeReadyServerTime - ServerTime);
}

void UHeroArenaBlinkChargeComponent::ScheduleNextRecharge()
{
	APlayerState* PlayerState = Cast<APlayerState>(GetOwner());
	UWorld* World = GetWorld();
	if (!PlayerState || !PlayerState->HasAuthority() || !World || ChargeState.CurrentCharges >= MaxCharges)
	{
		return;
	}
	const AGameStateBase* GameState = World->GetGameState();
	const float ServerTime = GameState ? GameState->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
	ChargeState.NextChargeReadyServerTime = ServerTime + RechargeDuration;
	World->GetTimerManager().SetTimer(
		RechargeTimerHandle, this, &ThisClass::RestoreNextCharge, RechargeDuration, false);
}

void UHeroArenaBlinkChargeComponent::RestoreNextCharge()
{
	APlayerState* PlayerState = Cast<APlayerState>(GetOwner());
	if (!PlayerState || !PlayerState->HasAuthority() || !IsPhaseSelected())
	{
		ResetCharges(TEXT("invalid_recharge_context"));
		return;
	}

	ChargeState.CurrentCharges = FMath::Min<uint8>(ChargeState.CurrentCharges + 1, MaxCharges);
	if (ChargeState.CurrentCharges < MaxCharges)
	{
		ScheduleNextRecharge();
	}
	else
	{
		ChargeState.NextChargeReadyServerTime = 0.0f;
	}
	PlayerState->ForceNetUpdate();
	BroadcastState();
	UE_LOG(LogHeroArenaBlinkCharges, Log,
		TEXT("Blink charge restored: player=%s charges=%d/%d next=%.2f"),
		*GetNameSafe(PlayerState), ChargeState.CurrentCharges, MaxCharges, GetNextChargeRemaining());
}

void UHeroArenaBlinkChargeComponent::ResetCharges(const TCHAR* Reason)
{
	APlayerState* PlayerState = Cast<APlayerState>(GetOwner());
	if (!PlayerState || !PlayerState->HasAuthority())
	{
		return;
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RechargeTimerHandle);
	}
	ChargeState.CurrentCharges = MaxCharges;
	ChargeState.LastResolvedPredictionKey = 0;
	++ChargeState.ResetEpoch;
	ChargeState.NextChargeReadyServerTime = 0.0f;
	LastDamageRewardServerTime = -BIG_NUMBER;
	PlayerState->ForceNetUpdate();
	BroadcastState();
	UE_LOG(LogHeroArenaBlinkCharges, Log,
		TEXT("Blink charges reset: player=%s charges=%d/%d reason=%s"),
		*GetNameSafe(PlayerState), ChargeState.CurrentCharges, MaxCharges, Reason);
}

bool UHeroArenaBlinkChargeComponent::IsPhaseSelected() const
{
	const UHeroArenaHeroSelectionComponent* Selection =
		GetOwner() ? GetOwner()->FindComponentByClass<UHeroArenaHeroSelectionComponent>() : nullptr;
	const UHeroArenaHeroDefinition* Hero = Selection ? Selection->GetSelectedHero() : nullptr;
	return Hero && Hero->HeroId == TEXT("Phase");
}

void UHeroArenaBlinkChargeComponent::HandleHeroChanged(
	const UHeroArenaHeroDefinition* OldHero, const UHeroArenaHeroDefinition* NewHero)
{
	PendingPredictionKeys.Reset();
	ResetCharges(TEXT("hero_changed"));
}

void UHeroArenaBlinkChargeComponent::HandlePawnSet(
	APlayerState* PlayerState, APawn* NewPawn, APawn* OldPawn)
{
	if (OldPawn != NewPawn && IsPhaseSelected())
	{
		PendingPredictionKeys.Reset();
		ResetCharges(NewPawn ? TEXT("pawn_spawned") : TEXT("pawn_removed"));
	}
}

void UHeroArenaBlinkChargeComponent::OnRep_ChargeState()
{
	if (ObservedResetEpoch != ChargeState.ResetEpoch)
	{
		PendingPredictionKeys.Reset();
		ObservedResetEpoch = ChargeState.ResetEpoch;
	}
	else if (ChargeState.LastResolvedPredictionKey > 0)
	{
		const int16 Resolved = ChargeState.LastResolvedPredictionKey;
		// GAS client keys are positive int16 values. Compare within half their wrap range.
		// Reliable activation RPCs preserve request order; one replicated update may cover several keys.
		PendingPredictionKeys.RemoveAll([Resolved](int16 Key)
		{
			return ((uint16(Resolved - Key) & 0x7fff) < 0x4000);
		});
	}
	BroadcastState();
	UE_LOG(LogHeroArenaBlinkCharges, Log,
		TEXT("Blink charge state replicated: player=%s charges=%d/%d next=%.2f"),
		*GetNameSafe(GetOwner()), ChargeState.CurrentCharges, MaxCharges, GetNextChargeRemaining());
}

void UHeroArenaBlinkChargeComponent::BroadcastState()
{
	OnChargeStateChanged.Broadcast(GetCurrentCharges(), GetNextChargeRemaining());
}
