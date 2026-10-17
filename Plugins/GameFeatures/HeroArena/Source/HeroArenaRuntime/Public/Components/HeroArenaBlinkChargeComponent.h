#pragma once

#include "Components/PlayerStateComponent.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "GameplayPrediction.h"

#include "HeroArenaBlinkChargeComponent.generated.h"

class UHeroArenaHeroDefinition;
struct FLyraVerbMessage;

USTRUCT(BlueprintType)
struct FHeroArenaBlinkChargeState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Hero Arena|Blink")
	uint8 CurrentCharges = 3;

	UPROPERTY(BlueprintReadOnly, Category="Hero Arena|Blink")
	float NextChargeReadyServerTime = 0.0f;

	// Paired server-clock sample; client presentation does not subtract its smoothed GameState clock.
	UPROPERTY()
	float SnapshotServerTime = 0.0f;

	UPROPERTY()
	uint32 RechargeSequence = 0;

	// Echo the last processed client request with the authoritative count, including collision refusals.
	UPROPERTY()
	int16 LastResolvedPredictionKey = 0;

	UPROPERTY()
	uint32 ResetEpoch = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FHeroArenaBlinkChargeStateChanged, int32, CurrentCharges, float, NextChargeRemaining);

/** Owner-visible, server-authoritative sequential recharge state for Phase Blink. */
UCLASS(Blueprintable, Meta=(BlueprintSpawnableComponent))
class HEROARENARUNTIME_API UHeroArenaBlinkChargeComponent : public UPlayerStateComponent
{
	GENERATED_BODY()

public:
	UHeroArenaBlinkChargeComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UFUNCTION(BlueprintPure, Category="Hero Arena|Blink")
	int32 GetCurrentCharges() const { return FMath::Max(0, int32(ChargeState.CurrentCharges) - PendingPredictionKeys.Num()); }

	UFUNCTION(BlueprintPure, Category="Hero Arena|Blink")
	int32 GetMaxCharges() const { return MaxCharges; }

	UFUNCTION(BlueprintPure, Category="Hero Arena|Blink")
	float GetNextChargeRemaining() const;

	bool HasAvailableCharge() const;
	bool TryConsumeCharge(FPredictionKey PredictionKey);
	void ResolvePrediction(const FPredictionKey& PredictionKey);
	int32 GetPendingPredictionCount() const { return PendingPredictionKeys.Num(); }

	UPROPERTY(BlueprintAssignable, Category="Hero Arena|Blink")
	FHeroArenaBlinkChargeStateChanged OnChargeStateChanged;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UFUNCTION()
	void OnRep_ChargeState();

	UFUNCTION()
	void HandleHeroChanged(const UHeroArenaHeroDefinition* OldHero, const UHeroArenaHeroDefinition* NewHero);

	UFUNCTION()
	void HandlePawnSet(APlayerState* PlayerState, APawn* NewPawn, APawn* OldPawn);

	void ResetCharges(const TCHAR* Reason);
	void RestoreNextCharge();
	void ScheduleNextRecharge();
	void HandleDamageMessage(FGameplayTag Channel, const FLyraVerbMessage& Payload);
	void HandleEliminationMessage(FGameplayTag Channel, const FLyraVerbMessage& Payload);
	void HandleAssistMessage(FGameplayTag Channel, const FLyraVerbMessage& Payload);
	bool AdvanceCurrentRecharge(float Seconds, const TCHAR* Reason);
	bool IsPhaseSelected() const;
	void BroadcastState();
	void PublishState();

	UPROPERTY(ReplicatedUsing=OnRep_ChargeState)
	FHeroArenaBlinkChargeState ChargeState;

	UPROPERTY(EditDefaultsOnly, Category="Hero Arena|Blink", meta=(ClampMin="1", ClampMax="8"))
	uint8 MaxCharges = 3;

	UPROPERTY(EditDefaultsOnly, Category="Hero Arena|Blink", meta=(ClampMin="0.1", Units="s"))
	float RechargeDuration = 4.0f;

	UPROPERTY(EditDefaultsOnly, Category="Hero Arena|Blink", meta=(ClampMin="0.0", Units="s"))
	float DamageRechargeReduction = 0.35f;

	UPROPERTY(EditDefaultsOnly, Category="Hero Arena|Blink", meta=(ClampMin="0.0", Units="s"))
	float EliminationRechargeReduction = 1.5f;

	UPROPERTY(EditDefaultsOnly, Category="Hero Arena|Blink", meta=(ClampMin="0.0", Units="s"))
	float AssistRechargeReduction = 0.75f;

	UPROPERTY(EditDefaultsOnly, Category="Hero Arena|Blink", meta=(ClampMin="0.0", Units="s"))
	float DamageRewardInterval = 0.25f;

	FTimerHandle RechargeTimerHandle;
	FGameplayMessageListenerHandle DamageListenerHandle;
	FGameplayMessageListenerHandle EliminationListenerHandle;
	FGameplayMessageListenerHandle AssistListenerHandle;
	float LastDamageRewardServerTime = -BIG_NUMBER;
	TArray<int16> PendingPredictionKeys;
	uint32 ObservedResetEpoch = 0;
	uint32 ObservedRechargeSequence = 0;
	double ClientRechargeDeadline = 0.0;
};
