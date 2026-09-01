#pragma once

#include "GameplayEffect.h"

#include "HeroArenaGameplayEffect_RecallCooldown.generated.h"

/** Owner-visible GAS cooldown mirroring Phase Recall's server history lockout. */
UCLASS()
class HEROARENARUNTIME_API UHeroArenaGameplayEffect_RecallCooldown : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UHeroArenaGameplayEffect_RecallCooldown(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
