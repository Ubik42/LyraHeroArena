#pragma once

#include "AbilitySystem/Abilities/LyraGameplayAbility.h"
#include "HeroArenaGameplayAbility_StickyBomb.generated.h"

/** Phase ultimate: predicts activation/cooldown locally; the server owns projectile state and damage. */
UCLASS()
class HEROARENARUNTIME_API UHeroArenaGameplayAbility_StickyBomb : public ULyraGameplayAbility
{
	GENERATED_BODY()

public:
	UHeroArenaGameplayAbility_StickyBomb(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};
