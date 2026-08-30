#pragma once

#include "GameplayEffect.h"
#include "HeroArenaGameplayEffect_StickyBomb.generated.h"

/** Marks a living enemy as carrying Phase's purifiable sticky bomb. */
UCLASS()
class HEROARENARUNTIME_API UHeroArenaGameplayEffect_StickyBombAttached : public UGameplayEffect
{
	GENERATED_BODY()
public:
	UHeroArenaGameplayEffect_StickyBombAttached(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};

/** Cooldown paid when the authoritative sticky bomb is thrown. */
UCLASS()
class HEROARENARUNTIME_API UHeroArenaGameplayEffect_StickyBombCooldown : public UGameplayEffect
{
	GENERATED_BODY()
public:
	UHeroArenaGameplayEffect_StickyBombCooldown(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
