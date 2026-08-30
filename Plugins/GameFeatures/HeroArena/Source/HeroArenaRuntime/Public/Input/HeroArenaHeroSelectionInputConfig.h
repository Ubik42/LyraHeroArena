#pragma once

#include "Input/LyraInputConfig.h"

#include "HeroArenaHeroSelectionInputConfig.generated.h"

/** Temporary keyboard-accessible selection surface; formal roster UI can call the same server component API. */
UCLASS(BlueprintType)
class HEROARENARUNTIME_API UHeroArenaHeroSelectionInputConfig : public ULyraInputConfig
{
	GENERATED_BODY()

public:
	UHeroArenaHeroSelectionInputConfig(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
