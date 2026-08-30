#pragma once

#include "Input/LyraInputConfig.h"

#include "HeroArenaBulwarkChargeInputConfig.generated.h"

/** Isolated binding asset so adding charge never rewrites Bulwark's existing barrier config. */
UCLASS(BlueprintType)
class HEROARENARUNTIME_API UHeroArenaBulwarkChargeInputConfig : public ULyraInputConfig
{
	GENERATED_BODY()

public:
	UHeroArenaBulwarkChargeInputConfig(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
