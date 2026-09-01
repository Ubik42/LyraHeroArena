#pragma once

#include "AbilitySystem/Abilities/LyraGameplayAbility.h"

#include "HeroArenaGameplayAbility_BotSpiralGrenade.generated.h"

/**
 * Server-only adapter that lets Lyra AI use Striker's existing grenade projectile
 * without executing player-controller camera and haptics nodes in GA_Grenade.
 */
UCLASS()
class HEROARENARUNTIME_API UHeroArenaGameplayAbility_BotSpiralGrenade : public ULyraGameplayAbility
{
	GENERATED_BODY()

public:
	UHeroArenaGameplayAbility_BotSpiralGrenade(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

private:
	UPROPERTY()
	TSubclassOf<AActor> ProjectileClass;
};
