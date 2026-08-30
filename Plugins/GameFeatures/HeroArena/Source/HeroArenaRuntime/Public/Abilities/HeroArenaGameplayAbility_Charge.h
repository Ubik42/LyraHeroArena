#pragma once

#include "AbilitySystem/Abilities/LyraGameplayAbility.h"

#include "HeroArenaGameplayAbility_Charge.generated.h"

class UAbilityTask_ApplyRootMotionConstantForce;
class UAnimMontage;

/** Predicted Bulwark charge; CharacterMovement handles world collision, while the server resolves hero hits. */
UCLASS()
class HEROARENARUNTIME_API UHeroArenaGameplayAbility_Charge : public ULyraGameplayAbility
{
	GENERATED_BODY()

public:
	UHeroArenaGameplayAbility_Charge(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	UFUNCTION()
	void HandleChargeFinished();

	void CheckServerChargeContact();
	bool TryHitEnemy(AActor* TargetActor, const FHitResult& HitResult);

	UPROPERTY(EditDefaultsOnly, Category = "HeroArena|Charge", meta = (ClampMin = "100.0"))
	float ChargeStrength = 1750.0f;

	UPROPERTY(EditDefaultsOnly, Category = "HeroArena|Charge", meta = (ClampMin = "0.1"))
	float ChargeDuration = 0.7f;

	UPROPERTY(EditDefaultsOnly, Category = "HeroArena|Charge", meta = (ClampMin = "1.0"))
	float ContactRadius = 95.0f;

	UPROPERTY(EditDefaultsOnly, Category = "HeroArena|Charge", meta = (ClampMin = "0.0"))
	float Damage = 35.0f;

	UPROPERTY(EditDefaultsOnly, Category = "HeroArena|Charge", meta = (ClampMin = "0.0"))
	float KnockbackStrength = 900.0f;

	UPROPERTY(EditDefaultsOnly, Category = "HeroArena|Charge", meta = (ClampMin = "0.0"))
	float KnockbackLift = 220.0f;

	/** Lyra's existing forward dash montage; gameplay movement remains task-driven rather than montage-driven. */
	UPROPERTY(EditDefaultsOnly, Category = "HeroArena|Charge")
	TObjectPtr<UAnimMontage> ChargeMontage;

	UPROPERTY()
	TObjectPtr<UAbilityTask_ApplyRootMotionConstantForce> ActiveRootMotionTask;

	FVector ChargeDirection = FVector::ForwardVector;
	FVector PreviousServerLocation = FVector::ZeroVector;
	FTimerHandle ServerContactTimer;
	double ServerChargeStartTime = 0.0;
	int32 ConsecutiveStalledChecks = 0;
};
