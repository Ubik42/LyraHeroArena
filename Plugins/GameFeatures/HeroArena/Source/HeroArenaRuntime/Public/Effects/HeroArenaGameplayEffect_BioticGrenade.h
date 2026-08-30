#pragma once

#include "GameplayEffect.h"
#include "HeroArenaGameplayEffect_BioticGrenade.generated.h"

UCLASS()
class HEROARENARUNTIME_API UHeroArenaGameplayEffect_BioticGrenadeAmplify : public UGameplayEffect
{
	GENERATED_BODY()
public:
	UHeroArenaGameplayEffect_BioticGrenadeAmplify(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};

UCLASS()
class HEROARENARUNTIME_API UHeroArenaGameplayEffect_BioticGrenadeSuppress : public UGameplayEffect
{
	GENERATED_BODY()
public:
	UHeroArenaGameplayEffect_BioticGrenadeSuppress(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};

UCLASS()
class HEROARENARUNTIME_API UHeroArenaGameplayEffect_BioticGrenadeCooldown : public UGameplayEffect
{
	GENERATED_BODY()
public:
	UHeroArenaGameplayEffect_BioticGrenadeCooldown(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
