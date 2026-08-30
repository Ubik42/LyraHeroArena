#pragma once

#include "AbilitySystem/LyraAbilitySet.h"
#include "HeroArenaRelayAbilitySet.generated.h"

UCLASS(BlueprintType)
class HEROARENARUNTIME_API UHeroArenaRelayAbilitySet : public ULyraAbilitySet
{
	GENERATED_BODY()
public:
	UHeroArenaRelayAbilitySet(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
