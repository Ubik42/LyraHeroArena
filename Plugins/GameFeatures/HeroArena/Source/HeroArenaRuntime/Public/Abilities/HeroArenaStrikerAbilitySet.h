#pragma once

#include "AbilitySystem/LyraAbilitySet.h"

#include "HeroArenaStrikerAbilitySet.generated.h"

/** Native-authored seed set used to bootstrap Striker without duplicating Lyra PawnData. */
UCLASS(BlueprintType)
class HEROARENARUNTIME_API UHeroArenaStrikerAbilitySet : public ULyraAbilitySet
{
	GENERATED_BODY()

public:
	UHeroArenaStrikerAbilitySet(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
