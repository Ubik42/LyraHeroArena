#pragma once

#include "GameplayEffect.h"
#include "HeroArenaGameplayEffect_OverclockInjection.generated.h"

UCLASS()
class HEROARENARUNTIME_API UHeroArenaGameplayEffect_OverclockInjection : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UHeroArenaGameplayEffect_OverclockInjection(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};

UCLASS()
class HEROARENARUNTIME_API UHeroArenaGameplayEffect_OverclockInjectionCooldown : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UHeroArenaGameplayEffect_OverclockInjectionCooldown(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
