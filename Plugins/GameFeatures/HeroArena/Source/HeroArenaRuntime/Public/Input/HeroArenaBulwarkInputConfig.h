#pragma once

#include "Input/LyraInputConfig.h"

#include "HeroArenaBulwarkInputConfig.generated.h"

/** Native additional input bindings for Bulwark's hero-specific actions. */
UCLASS(BlueprintType)
class HEROARENARUNTIME_API UHeroArenaBulwarkInputConfig : public ULyraInputConfig
{
	GENERATED_BODY()

public:
	UHeroArenaBulwarkInputConfig(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
