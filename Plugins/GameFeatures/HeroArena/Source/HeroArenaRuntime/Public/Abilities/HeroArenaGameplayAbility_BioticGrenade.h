#pragma once

#include "AbilitySystem/Abilities/LyraGameplayAbility.h"
#include "HeroArenaGameplayAbility_BioticGrenade.generated.h"

UCLASS()
class HEROARENARUNTIME_API UHeroArenaGameplayAbility_BioticGrenade : public ULyraGameplayAbility
{
	GENERATED_BODY()
public:
	UHeroArenaGameplayAbility_BioticGrenade(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};
