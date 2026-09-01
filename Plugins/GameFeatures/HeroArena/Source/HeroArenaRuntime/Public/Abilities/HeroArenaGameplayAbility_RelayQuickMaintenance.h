#pragma once

#include "AbilitySystem/Abilities/LyraGameplayAbility.h"

#include "HeroArenaGameplayAbility_RelayQuickMaintenance.generated.h"

/** Repairs one aimed allied barrier or beacon through the canonical healing pipeline. */
UCLASS()
class HEROARENARUNTIME_API UHeroArenaGameplayAbility_RelayQuickMaintenance : public ULyraGameplayAbility
{
	GENERATED_BODY()

public:
	UHeroArenaGameplayAbility_RelayQuickMaintenance(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo, FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

private:
	AActor* FindMaintenanceTarget(const AActor* Source, const FVector& ViewOrigin,
		const FVector& ViewDirection) const;

	UPROPERTY(EditDefaultsOnly, Category="Hero Arena|Relay|Maintenance", meta=(ClampMin="0.0", Units="cm"))
	float MaintenanceRange = 700.0f;
	UPROPERTY(EditDefaultsOnly, Category="Hero Arena|Relay|Maintenance", meta=(ClampMin="0.0", Units="cm"))
	float AimAssistRadius = 145.0f;
	UPROPERTY(EditDefaultsOnly, Category="Hero Arena|Relay|Maintenance", meta=(ClampMin="0.0"))
	float RepairAmount = 45.0f;
};
