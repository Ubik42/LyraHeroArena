#pragma once

#include "GameplayEffect.h"

#include "HeroArenaGameplayEffect_ChargeCooldown.generated.h"

/** Cooldown applied after a Bulwark charge is committed. */
UCLASS()
class HEROARENARUNTIME_API UHeroArenaGameplayEffect_ChargeCooldown : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UHeroArenaGameplayEffect_ChargeCooldown(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
