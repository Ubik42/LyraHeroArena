#pragma once

#include "AbilitySystem/Abilities/LyraGameplayAbility.h"

#include "HeroArenaGameplayAbility_Sprint.generated.h"

class UAbilityTask_WaitInputRelease;
class UCharacterMovementComponent;

/**
 * Striker's locally predicted hold-to-sprint layer.
 *
 * Its dedicated action is mapped to Shift alongside Lyra's existing Dash, so a
 * press keeps the stock directional burst and flows into sprint until release.
 */
UCLASS()
class HEROARENARUNTIME_API UHeroArenaGameplayAbility_Sprint : public ULyraGameplayAbility
{
	GENERATED_BODY()

public:
	UHeroArenaGameplayAbility_Sprint(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

	UFUNCTION()
	void HandleInputReleased(float TimeHeld);

	UPROPERTY(EditDefaultsOnly, Category = "HeroArena|Sprint", meta = (ClampMin = "1.0", ClampMax = "3.0"))
	float SprintSpeedMultiplier = 1.35f;

private:
	UPROPERTY(Transient)
	TObjectPtr<UCharacterMovementComponent> ActiveMovementComponent;

	float OriginalMaxWalkSpeed = 0.0f;
	bool bAppliedSprintSpeed = false;
};
