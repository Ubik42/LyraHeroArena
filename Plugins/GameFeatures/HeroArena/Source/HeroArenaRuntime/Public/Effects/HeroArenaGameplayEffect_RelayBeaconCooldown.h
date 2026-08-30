#pragma once

#include "GameplayEffect.h"
#include "HeroArenaGameplayEffect_RelayBeaconCooldown.generated.h"

UCLASS()
class HEROARENARUNTIME_API UHeroArenaGameplayEffect_RelayBeaconCooldown : public UGameplayEffect
{
	GENERATED_BODY()
public:
	UHeroArenaGameplayEffect_RelayBeaconCooldown(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
