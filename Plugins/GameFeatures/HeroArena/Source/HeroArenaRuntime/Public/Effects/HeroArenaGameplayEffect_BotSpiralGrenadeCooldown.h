#pragma once

#include "GameplayEffect.h"

#include "HeroArenaGameplayEffect_BotSpiralGrenadeCooldown.generated.h"

/** Cooldown for the AI-only adapter; the player ability keeps Lyra's original cooldown. */
UCLASS()
class HEROARENARUNTIME_API UHeroArenaGameplayEffect_BotSpiralGrenadeCooldown : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UHeroArenaGameplayEffect_BotSpiralGrenadeCooldown(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
