#pragma once

#include "CommonActivatableWidget.h"
#include "Hero/HeroArenaHeroSelectionComponent.h"

#include "HeroArenaHeroSelectWidget.generated.h"

class SButton;
class STextBlock;
class SVerticalBox;
class UHeroArenaHeroDefinition;

/** Code-native CommonUI screen so every HeroArena experience gets the same roster-driven selection flow. */
UCLASS()
class HEROARENARUNTIME_API UHeroArenaHeroSelectWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	UHeroArenaHeroSelectWidget(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;

	/** Reopens the roster while Lyra's existing auto-respawn countdown is active. */
	void OpenForRespawn(float RespawnDuration);
	void CloseRespawnSelection();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void NativeOnActivated() override;
	virtual bool NativeOnHandleBackAction() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	void BindSelectionComponent();
	void RebuildHeroCards();
	void RefreshSelectionText();
	void RefreshTeamComposition();
	FReply HandleHeroPressed(FName HeroId);
	FReply HandleContinuePressed();
	FText GetRoleText(const UHeroArenaHeroDefinition* Hero) const;

	UFUNCTION()
	void HandleHeroChanged(const UHeroArenaHeroDefinition* OldHero, const UHeroArenaHeroDefinition* NewHero);

	UFUNCTION()
	void HandleSelectionLockChanged(bool bIsLocked);

	UFUNCTION()
	void HandleSelectionRequestResolved(FName HeroId, EHeroArenaHeroSelectionResult Result);

	TWeakObjectPtr<UHeroArenaHeroSelectionComponent> SelectionComponent;
	TSharedPtr<SVerticalBox> HeroCardsBox;
	TSharedPtr<STextBlock> CurrentHeroText;
	TSharedPtr<STextBlock> StatusText;
	TSharedPtr<STextBlock> ContextText;
	TSharedPtr<STextBlock> TeamCompositionText;
	TSharedPtr<STextBlock> TeamHeroListText;
	TArray<TSharedPtr<SButton>> HeroButtons;
	TArray<FName> HeroIds;
	int32 FocusedHeroIndex = 0;
	double NextTeamRefreshTime = 0.0;
	FString LastTeamCompositionLog;
	bool bRespawnContext = false;
};
