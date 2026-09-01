#pragma once

#include "AbilitySystem/Abilities/LyraGameplayAbility.h"

#include "HeroArenaGameplayAbility_RelayMaintenanceTuning.generated.h"

/**
 * Replaces Relay's equipped Lyra pistol fire grant with the proven Pulse support-shot
 * implementation while preserving the equipment source object, input tag and reload path.
 */
UCLASS()
class HEROARENARUNTIME_API UHeroArenaGameplayAbility_RelayMaintenanceTuning : public ULyraGameplayAbility
{
	GENERATED_BODY()

public:
	UHeroArenaGameplayAbility_RelayMaintenanceTuning(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void ActivateAbility(
		FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(
		FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

private:
	void RefreshWeaponAbility();
	void RemoveReplacementAbility();

	UPROPERTY()
	TSubclassOf<UGameplayAbility> MaintenanceFireAbilityClass;

	UPROPERTY(EditDefaultsOnly, Category="Hero Arena|Relay|Weapon", meta=(ClampMin="0.05", Units="s"))
	float RefreshInterval = 0.25f;

	FGameplayAbilitySpecHandle ReplacementAbilityHandle;
	TWeakObjectPtr<UObject> ReplacementSourceObject;
	FTimerHandle RefreshTimerHandle;
};
