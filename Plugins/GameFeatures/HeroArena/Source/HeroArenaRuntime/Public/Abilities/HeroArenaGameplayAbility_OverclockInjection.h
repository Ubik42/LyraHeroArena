#pragma once

#include "Abilities/HeroArenaGameplayAbility_Ultimate.h"
#include "HeroArenaGameplayAbility_OverclockInjection.generated.h"

UCLASS()
class HEROARENARUNTIME_API UHeroArenaGameplayAbility_OverclockInjection : public UHeroArenaGameplayAbility_Ultimate
{
	GENERATED_BODY()

public:
	UHeroArenaGameplayAbility_OverclockInjection(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

private:
	UPROPERTY(EditDefaultsOnly, Category="HeroArena|Overclock")
	float TraceRange = 5000.0f;
};
