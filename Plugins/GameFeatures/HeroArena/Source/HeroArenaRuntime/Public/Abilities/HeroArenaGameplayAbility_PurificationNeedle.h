#pragma once

#include "AbilitySystem/Abilities/LyraGameplayAbility.h"
#include "HeroArenaGameplayAbility_PurificationNeedle.generated.h"

UCLASS()
class HEROARENARUNTIME_API UHeroArenaGameplayAbility_PurificationNeedle : public ULyraGameplayAbility
{
	GENERATED_BODY()

public:
	UHeroArenaGameplayAbility_PurificationNeedle(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

private:
	UPROPERTY(EditDefaultsOnly, Category="HeroArena|Purification")
	float TraceRange = 5000.0f;
};
