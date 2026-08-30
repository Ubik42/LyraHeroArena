#pragma once

#include "AbilitySystem/Abilities/LyraGameplayAbility.h"
#include "HeroArenaGameplayAbility_RelayBeacon.generated.h"

/** Deploys one validated support beacon; a later activation recalls the current beacon. */
UCLASS()
class HEROARENARUNTIME_API UHeroArenaGameplayAbility_RelayBeacon : public ULyraGameplayAbility
{
	GENERATED_BODY()
public:
	UHeroArenaGameplayAbility_RelayBeacon(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
protected:
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};
