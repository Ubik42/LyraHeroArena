#pragma once

#include "AbilitySystem/LyraAbilitySet.h"

#include "HeroArenaPulsePistolAbilitySet.generated.h"

/** Reuses Lyra pistol fire/reload while the Pulse fire asset routes its effect through a team-aware execution. */
UCLASS(BlueprintType)
class HEROARENARUNTIME_API UHeroArenaPulsePistolAbilitySet : public ULyraAbilitySet
{
	GENERATED_BODY()

public:
	UHeroArenaPulsePistolAbilitySet(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
