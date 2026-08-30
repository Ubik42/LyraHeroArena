#pragma once

#include "GameplayEffect.h"
#include "HeroArenaGameplayEffect_PurificationNeedleCooldown.generated.h"

UCLASS()
class HEROARENARUNTIME_API UHeroArenaGameplayEffect_PurificationNeedleCooldown : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UHeroArenaGameplayEffect_PurificationNeedleCooldown(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
