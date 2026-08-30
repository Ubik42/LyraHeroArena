#pragma once

#include "AbilitySystem/Abilities/LyraGameplayAbility.h"

#include "HeroArenaGameplayAbility_KineticShield.generated.h"

class AHeroArenaKineticShieldIndicator;
class ULyraHealthSet;

/**
 * A temporary overshield implemented on top of Lyra's existing Health/MaxHealth damage path.
 * The server tracks the remaining temporary layer and removes only its unspent remainder.
 */
UCLASS()
class HEROARENARUNTIME_API UHeroArenaGameplayAbility_KineticShield : public ULyraGameplayAbility
{
	GENERATED_BODY()

public:
	UHeroArenaGameplayAbility_KineticShield(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	void HandleHealthChanged(AActor* DamageInstigator, AActor* DamageCauser,
		const FGameplayEffectSpec* DamageEffectSpec, float DamageMagnitude, float OldValue, float NewValue);
	void FinishShield();

	UPROPERTY(EditDefaultsOnly, Category="Kinetic Shield", meta=(ClampMin="1.0"))
	float ShieldAmount = 75.0f;

	UPROPERTY(EditDefaultsOnly, Category="Kinetic Shield", meta=(ClampMin="0.1", Units="s"))
	float ShieldDuration = 6.0f;

	float BaseMaxHealth = 0.0f;
	float RemainingShield = 0.0f;
	bool bShieldApplied = false;
	TWeakObjectPtr<const ULyraHealthSet> BoundHealthSet;
	TWeakObjectPtr<AHeroArenaKineticShieldIndicator> Indicator;
	FTimerHandle DurationTimerHandle;
};
