#pragma once

#include "Blueprint/UserWidget.h"
#include "GameFramework/GameplayMessageSubsystem.h"

#include "HeroArenaCombatHUDWidget.generated.h"

class SBorder;
class SProgressBar;
class STextBlock;
class UHeroArenaHeroDefinition;
class UHeroArenaHeroSelectionComponent;
struct FLyraControlPointStatusMessage;
struct FLyraVerbMessage;

/** Compact Hero Arena-specific combat feedback layered beside Lyra's native HUD. */
UCLASS()
class HEROARENARUNTIME_API UHeroArenaCombatHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UHeroArenaCombatHUDWidget(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	UFUNCTION()
	void HandleHeroChanged(const UHeroArenaHeroDefinition* OldHero, const UHeroArenaHeroDefinition* NewHero);

	void BindPlayerState();
	void RefreshHero(const UHeroArenaHeroDefinition* Hero);
	void RefreshRuntimeState();
	void HandleControlPointCaptured(FGameplayTag Channel, const FLyraControlPointStatusMessage& Message);
	void HandleRespawnCompleted(FGameplayTag Channel, const FLyraVerbMessage& Message);
	void RefreshObjectiveState();
	void RefreshReturnGuidance();
	FString GetObjectiveDistanceText(const class UHeroArenaControlPointRotationComponent* Rotation) const;
	FString GetObjectiveOwnerLabel(int32 OwnerTeamId) const;
	FLinearColor GetObjectiveOwnerColor(int32 OwnerTeamId) const;
	float GetCooldownRemaining(FName CooldownTagName) const;
	FText GetSkillState(int32 SkillIndex) const;
	FText GetStatusText() const;

	TWeakObjectPtr<UHeroArenaHeroSelectionComponent> SelectionComponent;
	FGameplayMessageListenerHandle ControlPointListener;
	FGameplayMessageListenerHandle RespawnCompletedListener;
	FName CurrentHeroId;
	TArray<FName> CooldownTags;
	TArray<bool> PassiveSkills;

	TSharedPtr<STextBlock> HeroNameText;
	TSharedPtr<STextBlock> RoleText;
	TSharedPtr<STextBlock> StatusText;
	TSharedPtr<STextBlock> TeamCompositionText;
	TSharedPtr<STextBlock> ObjectiveText;
	TSharedPtr<SBorder> ObjectiveToast;
	TSharedPtr<SBorder> AccentRail;
	TSharedPtr<SProgressBar> UltimateChargeBar;
	TArray<TSharedPtr<STextBlock>> SkillKeyTexts;
	TArray<TSharedPtr<STextBlock>> SkillLabelTexts;
	TArray<TSharedPtr<STextBlock>> SkillStateTexts;
	FLinearColor CurrentAccent = FLinearColor(0.20f, 0.68f, 0.80f, 1.0f);
	double ToastEndTime = 0.0;
	double NextBindAttemptTime = 0.0;
	double NextRuntimeRefreshTime = 0.0;
	double NextTeamRefreshTime = 0.0;
	bool bReturnGuidanceActive = false;
};
