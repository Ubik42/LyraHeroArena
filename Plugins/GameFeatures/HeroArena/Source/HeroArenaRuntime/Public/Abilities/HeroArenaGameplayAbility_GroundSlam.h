#pragma once

#include "AbilitySystem/Abilities/LyraGameplayAbility.h"

#include "HeroArenaGameplayAbility_GroundSlam.generated.h"

/** Bulwark ultimate: server-authoritative front cone damage, knockback and short movement slow. */
UCLASS()
class HEROARENARUNTIME_API UHeroArenaGameplayAbility_GroundSlam : public ULyraGameplayAbility
{
	GENERATED_BODY()

public:
	UHeroArenaGameplayAbility_GroundSlam(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

private:
	bool TryAffectEnemy(AActor* TargetActor, const FVector& Origin, const FVector& Forward);

	UPROPERTY(EditDefaultsOnly, Category="HeroArena|Ground Slam", meta=(ClampMin="100.0"))
	float Range = 650.0f;

	UPROPERTY(EditDefaultsOnly, Category="HeroArena|Ground Slam", meta=(ClampMin="0.0", ClampMax="1.0"))
	float MinimumForwardDot = 0.5f;

	UPROPERTY(EditDefaultsOnly, Category="HeroArena|Ground Slam", meta=(ClampMin="0.0"))
	float VerticalTolerance = 250.0f;

	UPROPERTY(EditDefaultsOnly, Category="HeroArena|Ground Slam", meta=(ClampMin="0.0"))
	float Damage = 45.0f;

	UPROPERTY(EditDefaultsOnly, Category="HeroArena|Ground Slam", meta=(ClampMin="0.0"))
	float KnockbackStrength = 1100.0f;

	UPROPERTY(EditDefaultsOnly, Category="HeroArena|Ground Slam", meta=(ClampMin="0.0"))
	float KnockbackLift = 300.0f;

	UPROPERTY(EditDefaultsOnly, Category="HeroArena|Ground Slam", meta=(ClampMin="0.1", ClampMax="1.0"))
	float SlowMultiplier = 0.55f;

	UPROPERTY(EditDefaultsOnly, Category="HeroArena|Ground Slam", meta=(ClampMin="0.1"))
	float SlowDuration = 2.5f;
};
