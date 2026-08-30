#pragma once

#include "AbilitySystem/LyraAbilitySet.h"

#include "HeroArenaPhaseAbilitySet.generated.h"

/** Native Phase grants: runtime Pistol tuning, Blink charges and authoritative Recall. */
UCLASS(BlueprintType)
class HEROARENARUNTIME_API UHeroArenaPhaseAbilitySet : public ULyraAbilitySet
{
	GENERATED_BODY()

public:
	UHeroArenaPhaseAbilitySet(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
