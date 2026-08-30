#pragma once

#include "AbilitySystem/LyraAbilitySet.h"

#include "HeroArenaBulwarkAbilitySet.generated.h"

/** Native seed set for Bulwark's first vertical slice. */
UCLASS(BlueprintType)
class HEROARENARUNTIME_API UHeroArenaBulwarkAbilitySet : public ULyraAbilitySet
{
	GENERATED_BODY()

public:
	UHeroArenaBulwarkAbilitySet(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
