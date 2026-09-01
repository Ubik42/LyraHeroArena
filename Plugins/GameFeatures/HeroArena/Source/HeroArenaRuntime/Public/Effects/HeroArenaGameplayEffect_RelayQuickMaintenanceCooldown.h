#pragma once

#include "GameplayEffect.h"

#include "HeroArenaGameplayEffect_RelayQuickMaintenanceCooldown.generated.h"

UCLASS()
class HEROARENARUNTIME_API UHeroArenaGameplayEffect_RelayQuickMaintenanceCooldown : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UHeroArenaGameplayEffect_RelayQuickMaintenanceCooldown(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
