#pragma once

#include "GameplayEffect.h"

#include "HeroArenaGameplayEffect_GroundSlamCooldown.generated.h"

UCLASS()
class HEROARENARUNTIME_API UHeroArenaGameplayEffect_GroundSlamCooldown : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UHeroArenaGameplayEffect_GroundSlamCooldown(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
