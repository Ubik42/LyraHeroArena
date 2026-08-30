// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystem/Abilities/LyraGameplayAbility.h"

#include "HeroArenaGameplayAbility_PhasePistolTuning.generated.h"

/**
 * Actor-scoped Phase pistol tuning. It changes only the owning player's runtime
 * pistol fire ability instance and restores that instance when the hero grant is removed.
 */
UCLASS()
class HEROARENARUNTIME_API UHeroArenaGameplayAbility_PhasePistolTuning : public ULyraGameplayAbility
{
	GENERATED_BODY()

public:
	UHeroArenaGameplayAbility_PhasePistolTuning(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void ActivateAbility(
		FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(
		FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

private:
	struct FFloatOverride
	{
		TWeakObjectPtr<UObject> Object;
		FName PropertyName;
		double OriginalValue = 0.0;
		double AppliedValue = 0.0;
	};

	void RefreshPistolOverride();
	void RestorePistolOverride();
	bool ApplyFloatMultiplier(UObject* Object, FName PropertyName, double Multiplier);
	bool IsPistolFireAbility(const UGameplayAbility* Ability) const;

	UPROPERTY(EditDefaultsOnly, Category="Hero Arena|Phase|Pistol", meta=(ClampMin="0.1", ClampMax="1.0"))
	float FireIntervalMultiplier = 0.70f;

	UPROPERTY(EditDefaultsOnly, Category="Hero Arena|Phase|Pistol", meta=(ClampMin="0.05", Units="s"))
	float RefreshInterval = 0.25f;

	TArray<FFloatOverride> FloatOverrides;
	FTimerHandle RefreshTimerHandle;
};
