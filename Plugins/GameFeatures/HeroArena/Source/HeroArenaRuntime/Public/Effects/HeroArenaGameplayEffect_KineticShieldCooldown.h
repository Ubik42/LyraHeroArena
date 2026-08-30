#pragma once

#include "GameplayEffect.h"

#include "HeroArenaGameplayEffect_KineticShieldCooldown.generated.h"

/** Cooldown applied when Bulwark commits the kinetic shield. */
UCLASS()
class HEROARENARUNTIME_API UHeroArenaGameplayEffect_KineticShieldCooldown : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UHeroArenaGameplayEffect_KineticShieldCooldown(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
