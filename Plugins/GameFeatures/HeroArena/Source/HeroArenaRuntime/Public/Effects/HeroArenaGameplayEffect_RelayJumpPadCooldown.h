#pragma once

#include "GameplayEffect.h"

#include "HeroArenaGameplayEffect_RelayJumpPadCooldown.generated.h"

UCLASS()
class HEROARENARUNTIME_API UHeroArenaGameplayEffect_RelayJumpPadCooldown : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UHeroArenaGameplayEffect_RelayJumpPadCooldown(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
