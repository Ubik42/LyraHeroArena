#pragma once

#include "AbilitySystem/Abilities/LyraGameplayAbility.h"

#include "HeroArenaGameplayAbility_Ultimate.generated.h"

/** Shared charge gate for Hero Arena ultimates. Target validation remains in each concrete ability. */
UCLASS(Abstract)
class HEROARENARUNTIME_API UHeroArenaGameplayAbility_Ultimate : public ULyraGameplayAbility
{
	GENERATED_BODY()

protected:
	virtual bool CanActivateAbility(
		FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	bool CommitUltimateAbility(
		FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo);
};
