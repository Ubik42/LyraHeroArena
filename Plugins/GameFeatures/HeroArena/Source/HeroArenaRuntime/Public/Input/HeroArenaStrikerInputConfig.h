#pragma once

#include "Input/LyraInputConfig.h"

#include "HeroArenaStrikerInputConfig.generated.h"

/** Input binding seed for Striker's additional ability actions. */
UCLASS(BlueprintType)
class HEROARENARUNTIME_API UHeroArenaStrikerInputConfig : public ULyraInputConfig
{
	GENERATED_BODY()

public:
	UHeroArenaStrikerInputConfig(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
