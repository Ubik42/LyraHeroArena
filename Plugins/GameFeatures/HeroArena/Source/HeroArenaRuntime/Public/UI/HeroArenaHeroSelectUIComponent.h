#pragma once

#include "Components/ActorComponent.h"
#include "GameFramework/GameplayMessageSubsystem.h"

#include "HeroArenaHeroSelectUIComponent.generated.h"

class UHeroArenaHeroSelectWidget;
class UHeroArenaCombatHUDWidget;
class UIndicatorDescriptor;
class ULyraAbilitySystemComponent;
class APlayerState;
struct FLyraInteractionDurationMessage;
struct FLyraVerbMessage;

/** HUD-side bridge that adds the HeroArena selection screen to Lyra's CommonUI Menu layer. */
UCLASS(Meta = (BlueprintSpawnableComponent))
class HEROARENARUNTIME_API UHeroArenaHeroSelectUIComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHeroArenaHeroSelectUIComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void TryCreateHeroArenaUI();
	UHeroArenaHeroSelectWidget* PushSelectionScreen();
	void HandleRespawnStarted(FGameplayTag Channel, const FLyraInteractionDurationMessage& Message);
	void HandleRespawnCompleted(FGameplayTag Channel, const FLyraVerbMessage& Message);
	void BindAbilityInputBlockedState();
	void RefreshTeammateIndicators();
	void ClearTeammateIndicators();
	void HandleAbilityInputBlockedChanged(const FGameplayTag Tag, int32 NewCount);
	bool IsAbilityInputBlocked() const;
	bool IsMessageForOwningPlayer(const UObject* Instigator) const;

	FTimerHandle CreateScreenTimer;
	FTimerHandle TeammateIndicatorTimer;
	FGameplayMessageListenerHandle RespawnStartedListener;
	FGameplayMessageListenerHandle RespawnCompletedListener;
	FDelegateHandle AbilityInputBlockedHandle;
	TWeakObjectPtr<ULyraAbilitySystemComponent> AbilitySystemComponent;
	TWeakObjectPtr<UHeroArenaHeroSelectWidget> SelectionScreen;
	TWeakObjectPtr<UHeroArenaCombatHUDWidget> CombatHUD;

	UPROPERTY(Transient)
	TMap<TObjectPtr<APlayerState>, TObjectPtr<UIndicatorDescriptor>> TeammateIndicators;

	int32 LastTeammateIndicatorCount = INDEX_NONE;
};
