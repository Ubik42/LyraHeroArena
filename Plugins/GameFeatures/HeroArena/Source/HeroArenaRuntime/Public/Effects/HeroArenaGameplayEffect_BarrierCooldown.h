#pragma once

#include "GameplayEffect.h"

#include "HeroArenaGameplayEffect_BarrierCooldown.generated.h"

/** Prevents held/repeated input from creating overlapping Bulwark barriers. */
UCLASS()
class HEROARENARUNTIME_API UHeroArenaGameplayEffect_BarrierCooldown : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UHeroArenaGameplayEffect_BarrierCooldown(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
