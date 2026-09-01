#pragma once

#include "Components/GameStateComponent.h"

#include "HeroArenaPerformancePolicyComponent.generated.h"

/** Applies Hero Arena's measured runtime policies while this experience is active. */
UCLASS(Meta=(BlueprintSpawnableComponent))
class HEROARENARUNTIME_API UHeroArenaPerformancePolicyComponent : public UGameStateComponent
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	bool bAnimationPolicyApplied = false;
	bool bWeaponVFXPolicyApplied = false;
	bool bWeaponPadVFXPolicyApplied = false;
};
