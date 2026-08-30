#pragma once

#include "GameplayEffect.h"
#include "HeroArenaGameplayEffect_MedicalFeedbackCooldown.generated.h"

UCLASS()
class HEROARENARUNTIME_API UHeroArenaGameplayEffect_MedicalFeedbackCooldown : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UHeroArenaGameplayEffect_MedicalFeedbackCooldown(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
