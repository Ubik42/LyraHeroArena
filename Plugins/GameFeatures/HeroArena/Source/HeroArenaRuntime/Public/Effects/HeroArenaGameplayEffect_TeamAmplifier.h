#pragma once

#include "GameplayEffect.h"

#include "HeroArenaGameplayEffect_TeamAmplifier.generated.h"

/** Short aura refresh applied while an ally remains inside Relay's amplifier. */
UCLASS()
class HEROARENARUNTIME_API UHeroArenaGameplayEffect_TeamAmplified : public UGameplayEffect
{
	GENERATED_BODY()
public:
	UHeroArenaGameplayEffect_TeamAmplified(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};

UCLASS()
class HEROARENARUNTIME_API UHeroArenaGameplayEffect_TeamAmplifierCooldown : public UGameplayEffect
{
	GENERATED_BODY()
public:
	UHeroArenaGameplayEffect_TeamAmplifierCooldown(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
