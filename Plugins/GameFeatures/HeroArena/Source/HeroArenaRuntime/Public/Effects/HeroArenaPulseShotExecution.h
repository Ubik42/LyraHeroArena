#pragma once

#include "GameplayEffectExecutionCalculation.h"

#include "HeroArenaPulseShotExecution.generated.h"

/** Routes one authoritative weapon hit to Lyra damage or healing according to team relationship. */
UCLASS()
class HEROARENARUNTIME_API UHeroArenaPulseShotExecution : public UGameplayEffectExecutionCalculation
{
	GENERATED_BODY()

public:
	UHeroArenaPulseShotExecution();

protected:
	virtual void Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams,
		FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const override;

private:
	UPROPERTY(EditDefaultsOnly, Category = "Pulse")
	float AllyHealMultiplier = 0.8f;

	UPROPERTY(EditDefaultsOnly, Category = "Relay")
	float RelayAllyRepairMultiplier = 1.1f;

	UPROPERTY(EditDefaultsOnly, Category = "Relay")
	float RelayDeployableRepairMultiplier = 1.65f;

	UPROPERTY(EditDefaultsOnly, Category = "Relay")
	float RelayEnemyDamageMultiplier = 0.55f;
};
