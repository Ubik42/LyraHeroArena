#pragma once

#include "Abilities/HeroArenaGameplayAbility_Ultimate.h"

#include "HeroArenaGameplayAbility_RelayTeamAmplifier.generated.h"

UCLASS()
class HEROARENARUNTIME_API UHeroArenaGameplayAbility_RelayTeamAmplifier : public UHeroArenaGameplayAbility_Ultimate
{
	GENERATED_BODY()
public:
	UHeroArenaGameplayAbility_RelayTeamAmplifier(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
protected:
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo, FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
};
