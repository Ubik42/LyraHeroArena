#pragma once

#include "Components/GameStateComponent.h"

#include "HeroArenaBattlePulseComponent.generated.h"

class ALyraCharacter;
class ULyraExperienceDefinition;
class ULyraAbilitySystemComponent;

UENUM(BlueprintType)
enum class EHeroArenaBattlePulse : uint8
{
	None,
	LowGravity,
	PowerSurge,
	RescueWindow
};

/** Arcade rules for the Battle Pulse experience; inactive in standard Hero Arena experiences. */
UCLASS(Meta=(BlueprintSpawnableComponent))
class HEROARENARUNTIME_API UHeroArenaBattlePulseComponent : public UGameStateComponent
{
	GENERATED_BODY()

public:
	UHeroArenaBattlePulseComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UFUNCTION(BlueprintPure, Category="Hero Arena|Battle Pulse")
	EHeroArenaBattlePulse GetCurrentPulse() const { return CurrentPulse; }

	UFUNCTION(BlueprintPure, Category="Hero Arena|Battle Pulse")
	FText GetCurrentPulseLabel() const;

	UFUNCTION(BlueprintPure, Category="Hero Arena|Battle Pulse")
	float GetSecondsRemaining() const;

	UFUNCTION(BlueprintPure, Category="Hero Arena|Battle Pulse")
	bool IsArcadeModeActive() const { return bArcadeModeActive; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	void HandleExperienceLoaded(const ULyraExperienceDefinition* Experience);
	void AdvancePulse();
	void RefreshPlayers();
	void RemovePulseFromPlayers();
	float GetServerTime() const;

	UPROPERTY(Replicated)
	EHeroArenaBattlePulse CurrentPulse = EHeroArenaBattlePulse::None;

	UPROPERTY(Replicated)
	float PulseEndServerTime = 0.0f;

	UPROPERTY(Replicated)
	bool bArcadeModeActive = false;

	struct FMovementDefaults
	{
		float GravityScale = 1.0f;
		float JumpZVelocity = 500.0f;
	};

	TMap<TWeakObjectPtr<ALyraCharacter>, FMovementDefaults> MovementDefaults;
	TSet<TWeakObjectPtr<ULyraAbilitySystemComponent>> PowerSurgeRecipients;
	TSet<TWeakObjectPtr<ULyraAbilitySystemComponent>> RescueWindowRecipients;
	FTimerHandle PulseTimer;
	FTimerHandle RefreshTimer;

	UPROPERTY(EditDefaultsOnly, Category="Hero Arena|Battle Pulse", meta=(ClampMin="10.0", Units="s"))
	float PulseDuration = 24.0f;
};
