#include "UI/HeroArenaHeroSelectUIComponent.h"

#include "CommonUIExtensions.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Hero/HeroArenaHeroSelectionComponent.h"
#include "NativeGameplayTags.h"
#include "UI/HeroArenaHeroSelectWidget.h"
#include "UI/LyraHUD.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaHeroSelectUIComponent)

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaHeroSelectUIComponent, Log, All);

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_UI_LAYER_MENU, "UI.Layer.Menu");

UHeroArenaHeroSelectUIComponent::UHeroArenaHeroSelectUIComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UHeroArenaHeroSelectUIComponent::BeginPlay()
{
	Super::BeginPlay();
	TryCreateSelectionScreen();
	if (!SelectionScreen.IsValid())
	{
		GetWorld()->GetTimerManager().SetTimer(CreateScreenTimer, this,
			&ThisClass::TryCreateSelectionScreen, 0.2f, true);
	}
}

void UHeroArenaHeroSelectUIComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorld()->GetTimerManager().ClearTimer(CreateScreenTimer);
	if (UHeroArenaHeroSelectWidget* Screen = SelectionScreen.Get())
	{
		Screen->DeactivateWidget();
	}
	SelectionScreen.Reset();
	Super::EndPlay(EndPlayReason);
}

void UHeroArenaHeroSelectUIComponent::TryCreateSelectionScreen()
{
	if (SelectionScreen.IsValid())
	{
		GetWorld()->GetTimerManager().ClearTimer(CreateScreenTimer);
		return;
	}

	const ALyraHUD* HUD = Cast<ALyraHUD>(GetOwner());
	APlayerController* PlayerController = HUD ? HUD->GetOwningPlayerController() : nullptr;
	ULocalPlayer* LocalPlayer = PlayerController ? Cast<ULocalPlayer>(PlayerController->Player) : nullptr;
	APlayerState* PlayerState = PlayerController ? PlayerController->PlayerState : nullptr;
	const UHeroArenaHeroSelectionComponent* Selection = PlayerState
		? PlayerState->FindComponentByClass<UHeroArenaHeroSelectionComponent>()
		: nullptr;
	if (!LocalPlayer || !Selection || !Selection->GetRoster())
	{
		return;
	}

	SelectionScreen = Cast<UHeroArenaHeroSelectWidget>(UCommonUIExtensions::PushContentToLayer_ForPlayer(
		LocalPlayer, TAG_UI_LAYER_MENU, UHeroArenaHeroSelectWidget::StaticClass()));
	if (SelectionScreen.IsValid())
	{
		GetWorld()->GetTimerManager().ClearTimer(CreateScreenTimer);
		UE_LOG(LogHeroArenaHeroSelectUIComponent, Log, TEXT("Hero Select UI added for %s"),
			*GetNameSafe(PlayerState));
	}
}
