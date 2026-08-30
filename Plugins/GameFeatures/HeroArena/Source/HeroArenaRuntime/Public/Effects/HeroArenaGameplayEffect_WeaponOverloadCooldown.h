// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayEffect.h"

#include "HeroArenaGameplayEffect_WeaponOverloadCooldown.generated.h"

/** Independent cooldown for Striker's first ultimate prototype. */
UCLASS()
class HEROARENARUNTIME_API UHeroArenaGameplayEffect_WeaponOverloadCooldown : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UHeroArenaGameplayEffect_WeaponOverloadCooldown(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
