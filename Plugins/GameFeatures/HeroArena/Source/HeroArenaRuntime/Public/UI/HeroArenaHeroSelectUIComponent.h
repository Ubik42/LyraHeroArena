#pragma once

#include "Components/ActorComponent.h"

#include "HeroArenaHeroSelectUIComponent.generated.h"

class UHeroArenaHeroSelectWidget;

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
	void TryCreateSelectionScreen();

	FTimerHandle CreateScreenTimer;
	TWeakObjectPtr<UHeroArenaHeroSelectWidget> SelectionScreen;
};
