#pragma once

#include "CoreMinimal.h"

class UAbilitySystemComponent;

/** Central opt-in purification contract. Effects are removable only when they grant the purifiable marker tag. */
class HEROARENARUNTIME_API UHeroArenaPurificationRules
{
public:
	static bool HasPurifiableEffect(const UAbilitySystemComponent* TargetASC);
	static int32 RemovePurifiableEffects(UAbilitySystemComponent* TargetASC);
};
