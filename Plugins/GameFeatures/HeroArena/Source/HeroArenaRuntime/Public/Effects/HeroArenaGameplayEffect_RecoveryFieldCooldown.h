// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayEffect.h"

#include "HeroArenaGameplayEffect_RecoveryFieldCooldown.generated.h"

/** Cooldown owned by Striker's recovery field rather than the reused grenade cooldown. */
UCLASS()
class HEROARENARUNTIME_API UHeroArenaGameplayEffect_RecoveryFieldCooldown : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UHeroArenaGameplayEffect_RecoveryFieldCooldown(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
