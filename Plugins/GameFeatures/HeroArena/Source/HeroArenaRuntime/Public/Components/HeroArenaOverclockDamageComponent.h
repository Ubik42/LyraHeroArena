#pragma once

#include "Components/PlayerStateComponent.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "HeroArenaOverclockDamageComponent.generated.h"

struct FLyraVerbMessage;
class UCharacterMovementComponent;
class APlayerState;
class APawn;

/** Adds one guarded bonus-damage application from Lyra's final server damage message while this player is overclocked. */
UCLASS(Blueprintable, Meta=(BlueprintSpawnableComponent))
class HEROARENARUNTIME_API UHeroArenaOverclockDamageComponent : public UPlayerStateComponent
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void HandleDamageMessage(FGameplayTag Channel, const FLyraVerbMessage& Payload);
	void HandleOverclockTagChanged(const FGameplayTag Tag, int32 NewCount);

	UFUNCTION()
	void HandlePawnSet(APlayerState* PlayerState, APawn* NewPawn, APawn* OldPawn);

	void RefreshMovementBoost();
	void RestoreMovementBoost();

	FGameplayMessageListenerHandle ListenerHandle;
	bool bApplyingBonus = false;
	TWeakObjectPtr<UCharacterMovementComponent> ModifiedMovement;
	float OriginalMaxWalkSpeed = 0.0f;

	UPROPERTY(EditDefaultsOnly, Category="HeroArena|Overclock")
	float BonusDamageRatio = 0.25f;

	UPROPERTY(EditDefaultsOnly, Category="HeroArena|Overclock")
	float MovementSpeedMultiplier = 1.2f;
};
