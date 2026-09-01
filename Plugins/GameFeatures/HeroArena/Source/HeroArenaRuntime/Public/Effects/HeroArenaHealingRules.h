#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "HeroArenaHealingRules.generated.h"

class UAbilitySystemComponent;
class AActor;

/** Shared incoming-healing rule; suppression wins if both statuses exist. */
UCLASS()
class HEROARENARUNTIME_API UHeroArenaHealingRules : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintPure, Category="Hero Arena|Healing")
	static float GetIncomingHealingMultiplier(const UAbilitySystemComponent* TargetASC);

	/** Applies Lyra's canonical SetByCaller heal to Pawns or ASC-backed deployables. */
	static float ApplyStandardHealing(UAbilitySystemComponent* SourceASC, AActor* TargetActor,
		UObject* SourceObject, float BaseHeal);
};
