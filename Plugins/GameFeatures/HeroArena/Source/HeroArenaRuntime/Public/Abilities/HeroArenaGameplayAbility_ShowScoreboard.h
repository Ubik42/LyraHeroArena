#pragma once

#include "AbilitySystem/Abilities/LyraGameplayAbility.h"

#include "HeroArenaGameplayAbility_ShowScoreboard.generated.h"

class UCommonActivatableWidget;

/** Local-only held-input ability that opens the Hero Arena scoreboard on Lyra's game-menu layer. */
UCLASS()
class HEROARENARUNTIME_API UHeroArenaGameplayAbility_ShowScoreboard : public ULyraGameplayAbility
{
	GENERATED_BODY()

public:
	UHeroArenaGameplayAbility_ShowScoreboard(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	UFUNCTION()
	void HandleInputReleased(float TimeHeld);

	UPROPERTY(Transient)
	TObjectPtr<UCommonActivatableWidget> ScoreboardWidget;
};
