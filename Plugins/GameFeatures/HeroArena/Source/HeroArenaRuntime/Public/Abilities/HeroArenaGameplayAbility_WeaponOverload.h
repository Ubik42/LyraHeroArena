// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Abilities/HeroArenaGameplayAbility_Ultimate.h"

#include "HeroArenaGameplayAbility_WeaponOverload.generated.h"

class AHeroArenaWeaponOverloadIndicator;
class ULyraRangedWeaponInstance;

/** Local-predicted timed Rifle modifier. Server remains authoritative for activation and target data. */
UCLASS()
class HEROARENARUNTIME_API UHeroArenaGameplayAbility_WeaponOverload : public UHeroArenaGameplayAbility_Ultimate
{
	GENERATED_BODY()

public:
	UHeroArenaGameplayAbility_WeaponOverload(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual bool CanActivateAbility(
		FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

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
	struct FFloatOverride
	{
		TWeakObjectPtr<UObject> Object;
		FName PropertyName;
		double OriginalValue = 0.0;
	};

	void RefreshWeaponOverrides();
	void FinishOverload();
	void RestoreWeaponOverrides();
	bool ApplyFloatMultiplier(UObject* Object, FName PropertyName, double Multiplier);

	UPROPERTY(EditDefaultsOnly, Category="Weapon Overload", meta=(ClampMin="0.1", Units="s"))
	float OverloadDuration = 8.0f;

	UPROPERTY(EditDefaultsOnly, Category="Weapon Overload", meta=(ClampMin="0.1", ClampMax="1.0"))
	float FireIntervalMultiplier = 0.67f;

	UPROPERTY(EditDefaultsOnly, Category="Weapon Overload", meta=(ClampMin="0.05", ClampMax="1.0"))
	float SpreadMultiplier = 0.55f;

	TArray<FFloatOverride> FloatOverrides;
	TWeakObjectPtr<ULyraRangedWeaponInstance> OverloadedWeapon;
	TWeakObjectPtr<AHeroArenaWeaponOverloadIndicator> Indicator;
	FTimerHandle DurationTimerHandle;
	FTimerHandle RefreshTimerHandle;
};
