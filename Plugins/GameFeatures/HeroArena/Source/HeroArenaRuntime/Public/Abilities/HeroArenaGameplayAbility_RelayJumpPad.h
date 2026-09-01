#pragma once

#include "AbilitySystem/Abilities/LyraGameplayAbility.h"

#include "HeroArenaGameplayAbility_RelayJumpPad.generated.h"

/** Places or recalls Relay's single team-aware movement pad. */
UCLASS()
class HEROARENARUNTIME_API UHeroArenaGameplayAbility_RelayJumpPad : public ULyraGameplayAbility
{
	GENERATED_BODY()

public:
	UHeroArenaGameplayAbility_RelayJumpPad(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo, FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
};
