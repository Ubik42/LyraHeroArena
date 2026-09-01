#pragma once

#include "Blueprint/UserWidget.h"
#include "UI/IndicatorSystem/IActorIndicatorWidget.h"

#include "HeroArenaTeammateNameplateWidget.generated.h"

class SBorder;
class SProgressBar;
class STextBlock;
class APlayerState;
class APawn;
class UHeroArenaHeroDefinition;
class UHeroArenaHeroSelectionComponent;
class ULyraHealthComponent;
class UIndicatorDescriptor;

/** Compact hero identity label hosted by Lyra's existing screen-space indicator layer. */
UCLASS()
class HEROARENARUNTIME_API UHeroArenaTeammateNameplateWidget final
	: public UUserWidget, public IIndicatorWidgetInterface
{
	GENERATED_BODY()

public:
	UHeroArenaTeammateNameplateWidget(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void BindIndicator_Implementation(UIndicatorDescriptor* Indicator) override;
	virtual void UnbindIndicator_Implementation(const UIndicatorDescriptor* Indicator) override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	virtual void NativeDestruct() override;

private:
	UFUNCTION()
	void HandleHeroChanged(const UHeroArenaHeroDefinition* OldHero, const UHeroArenaHeroDefinition* NewHero);

	UFUNCTION()
	void HandlePawnChanged(APlayerState* PlayerState, APawn* NewPawn, APawn* OldPawn);

	UFUNCTION()
	void HandleHealthChanged(ULyraHealthComponent* HealthComponent, float OldValue, float NewValue, AActor* Instigator);

	void BindSelection(UHeroArenaHeroSelectionComponent* NewSelection);
	void BindHealth(ULyraHealthComponent* NewHealth);
	void RefreshHealthBinding();
	void RefreshIdentity();
	void RefreshHealth();

	TWeakObjectPtr<UHeroArenaHeroSelectionComponent> SelectionComponent;
	TWeakObjectPtr<ULyraHealthComponent> HealthComponent;
	TSharedPtr<SBorder> RootBorder;
	TSharedPtr<SBorder> AccentRail;
	TSharedPtr<STextBlock> HeroNameText;
	TSharedPtr<STextBlock> RoleText;
	TSharedPtr<SProgressBar> HealthBar;
};
