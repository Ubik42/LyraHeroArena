#pragma once

#include "Components/PlayerStateComponent.h"
#include "GameplayAbilitySpecHandle.h"
#include "GameFramework/GameplayMessageSubsystem.h"

#include "HeroArenaScoreboardGrantComponent.generated.h"

/** Replaces ShooterCore's generic control-point scoreboard ability while Hero Arena is active. */
UCLASS(Blueprintable, Meta=(BlueprintSpawnableComponent))
class HEROARENARUNTIME_API UHeroArenaScoreboardGrantComponent : public UPlayerStateComponent
{
	GENERATED_BODY()

public:
	UHeroArenaScoreboardGrantComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void TryReplaceScoreboardAbility();
	void HandleDamageMessage(FGameplayTag Channel, const struct FLyraVerbMessage& Payload);

	FTimerHandle RetryTimer;
	FGameplayAbilitySpecHandle GrantedHandle;
	FGameplayMessageListenerHandle DamageListener;
};
