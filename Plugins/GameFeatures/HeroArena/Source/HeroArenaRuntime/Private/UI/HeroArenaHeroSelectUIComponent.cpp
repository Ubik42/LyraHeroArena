#include "UI/HeroArenaHeroSelectUIComponent.h"

#include "AbilitySystem/LyraAbilitySystemComponent.h"
#include "CommonUIExtensions.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "Hero/HeroArenaHeroSelectionComponent.h"
#include "Interaction/LyraInteractionDurationMessage.h"
#include "Messages/LyraVerbMessage.h"
#include "NativeGameplayTags.h"
#include "UI/HeroArenaCombatHUDWidget.h"
#include "UI/HeroArenaHeroSelectWidget.h"
#include "UI/HeroArenaTeammateNameplateWidget.h"
#include "UI/LyraHUD.h"
#include "UI/IndicatorSystem/IndicatorDescriptor.h"
#include "UI/IndicatorSystem/LyraIndicatorManagerComponent.h"
#include "Player/LyraPlayerState.h"

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
	RespawnStartedListener = UGameplayMessageSubsystem::Get(this).RegisterListener(
		FGameplayTag::RequestGameplayTag(TEXT("Ability.Respawn.Duration.Message")),
		this, &ThisClass::HandleRespawnStarted);
	RespawnCompletedListener = UGameplayMessageSubsystem::Get(this).RegisterListener(
		FGameplayTag::RequestGameplayTag(TEXT("Ability.Respawn.Completed.Message")),
		this, &ThisClass::HandleRespawnCompleted);
	TryCreateHeroArenaUI();
	GetWorld()->GetTimerManager().SetTimer(TeammateIndicatorTimer, this,
		&ThisClass::RefreshTeammateIndicators, 0.5f, true, 0.25f);
	if (!SelectionScreen.IsValid() || !CombatHUD.IsValid())
	{
		GetWorld()->GetTimerManager().SetTimer(CreateScreenTimer, this,
			&ThisClass::TryCreateHeroArenaUI, 0.2f, true);
	}
}

void UHeroArenaHeroSelectUIComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorld()->GetTimerManager().ClearTimer(CreateScreenTimer);
	GetWorld()->GetTimerManager().ClearTimer(TeammateIndicatorTimer);
	ClearTeammateIndicators();
	if (ULyraAbilitySystemComponent* ASC = AbilitySystemComponent.Get(); AbilityInputBlockedHandle.IsValid() && ASC)
	{
		ASC->RegisterGameplayTagEvent(
			FGameplayTag::RequestGameplayTag(TEXT("Gameplay.AbilityInputBlocked")),
			EGameplayTagEventType::NewOrRemoved).Remove(AbilityInputBlockedHandle);
	}
	AbilityInputBlockedHandle.Reset();
	AbilitySystemComponent.Reset();
	if (RespawnStartedListener.IsValid())
	{
		UGameplayMessageSubsystem::Get(this).UnregisterListener(RespawnStartedListener);
	}
	if (RespawnCompletedListener.IsValid())
	{
		UGameplayMessageSubsystem::Get(this).UnregisterListener(RespawnCompletedListener);
	}
	if (UHeroArenaHeroSelectWidget* Screen = SelectionScreen.Get())
	{
		Screen->DeactivateWidget();
	}
	SelectionScreen.Reset();
	if (UHeroArenaCombatHUDWidget* Widget = CombatHUD.Get())
	{
		Widget->RemoveFromParent();
	}
	CombatHUD.Reset();
	Super::EndPlay(EndPlayReason);
}

void UHeroArenaHeroSelectUIComponent::ClearTeammateIndicators()
{
	for (const TPair<TObjectPtr<APlayerState>, TObjectPtr<UIndicatorDescriptor>>& Entry : TeammateIndicators)
	{
		if (UIndicatorDescriptor* Descriptor = Entry.Value)
		{
			Descriptor->UnregisterIndicator();
		}
	}
	TeammateIndicators.Reset();
	LastTeammateIndicatorCount = INDEX_NONE;
}

void UHeroArenaHeroSelectUIComponent::RefreshTeammateIndicators()
{
	const ALyraHUD* HUD = Cast<ALyraHUD>(GetOwner());
	APlayerController* PlayerController = HUD ? HUD->GetOwningPlayerController() : nullptr;
	const ALyraPlayerState* LocalPlayerState = PlayerController
		? PlayerController->GetPlayerState<ALyraPlayerState>() : nullptr;
	ULyraIndicatorManagerComponent* IndicatorManager = PlayerController
		? ULyraIndicatorManagerComponent::GetComponent(PlayerController) : nullptr;
	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (!LocalPlayerState || LocalPlayerState->GetTeamId() <= 0 || !IndicatorManager || !GameState)
	{
		if (!TeammateIndicators.IsEmpty())
		{
			ClearTeammateIndicators();
		}
		return;
	}

	TSet<APlayerState*> ActiveTeammates;
	for (APlayerState* PlayerState : GameState->PlayerArray)
	{
		ALyraPlayerState* TeammateState = Cast<ALyraPlayerState>(PlayerState);
		if (!TeammateState || TeammateState == LocalPlayerState
			|| TeammateState->GetTeamId() != LocalPlayerState->GetTeamId())
		{
			continue;
		}

		UHeroArenaHeroSelectionComponent* Selection =
			TeammateState->FindComponentByClass<UHeroArenaHeroSelectionComponent>();
		APawn* Pawn = TeammateState->GetPawn();
		USceneComponent* SceneComponent = Pawn ? Pawn->GetRootComponent() : nullptr;
		if (!Selection || !Selection->GetSelectedHero() || !SceneComponent)
		{
			continue;
		}

		ActiveTeammates.Add(TeammateState);
		UIndicatorDescriptor* Descriptor = TeammateIndicators.FindRef(TeammateState);
		if (!Descriptor)
		{
			Descriptor = NewObject<UIndicatorDescriptor>(this);
			Descriptor->SetDataObject(Selection);
			Descriptor->SetIndicatorClass(UHeroArenaTeammateNameplateWidget::StaticClass());
			Descriptor->SetProjectionMode(EActorCanvasProjectionMode::ActorBoundingBox);
			Descriptor->SetBoundingBoxAnchor(FVector(0.5, 0.5, 1.0));
			Descriptor->SetScreenSpaceOffset(FVector2D(0.0, -22.0));
			Descriptor->SetHAlign(HAlign_Center);
			Descriptor->SetVAlign(VAlign_Bottom);
			Descriptor->SetClampToScreen(false);
			Descriptor->SetPriority(5);
			IndicatorManager->AddIndicator(Descriptor);
			TeammateIndicators.Add(TeammateState, Descriptor);
		}
		Descriptor->SetSceneComponent(SceneComponent);
	}

	for (auto It = TeammateIndicators.CreateIterator(); It; ++It)
	{
		if (!ActiveTeammates.Contains(It.Key()))
		{
			if (UIndicatorDescriptor* Descriptor = It.Value())
			{
				Descriptor->UnregisterIndicator();
			}
			It.RemoveCurrent();
		}
	}

	if (LastTeammateIndicatorCount != TeammateIndicators.Num())
	{
		LastTeammateIndicatorCount = TeammateIndicators.Num();
		UE_LOG(LogHeroArenaHeroSelectUIComponent, Log,
			TEXT("Teammate hero indicators active: %d"), LastTeammateIndicatorCount);
	}
}

bool UHeroArenaHeroSelectUIComponent::IsMessageForOwningPlayer(const UObject* Instigator) const
{
	const ALyraHUD* HUD = Cast<ALyraHUD>(GetOwner());
	const APlayerController* PlayerController = HUD ? HUD->GetOwningPlayerController() : nullptr;
	return PlayerController && Instigator == PlayerController->PlayerState;
}

void UHeroArenaHeroSelectUIComponent::HandleRespawnStarted(
	FGameplayTag Channel, const FLyraInteractionDurationMessage& Message)
{
	if (!IsMessageForOwningPlayer(Message.Instigator) || IsAbilityInputBlocked())
	{
		return;
	}
	TryCreateHeroArenaUI();
	UHeroArenaHeroSelectWidget* Screen = SelectionScreen.Get();
	if (!Screen || !Screen->IsActivated())
	{
		Screen = PushSelectionScreen();
	}
	if (Screen)
	{
		Screen->OpenForRespawn(Message.Duration);
		UE_LOG(LogHeroArenaHeroSelectUIComponent, Log,
			TEXT("Respawn hero selection opened for %s (duration=%.1f)"),
			*GetNameSafe(Message.Instigator), Message.Duration);
	}
}

void UHeroArenaHeroSelectUIComponent::BindAbilityInputBlockedState()
{
	if (AbilitySystemComponent.IsValid())
	{
		return;
	}
	const ALyraHUD* HUD = Cast<ALyraHUD>(GetOwner());
	const APlayerController* PlayerController = HUD ? HUD->GetOwningPlayerController() : nullptr;
	const APlayerState* PlayerState = PlayerController ? PlayerController->PlayerState : nullptr;
	ULyraAbilitySystemComponent* ASC = PlayerState
		? PlayerState->FindComponentByClass<ULyraAbilitySystemComponent>() : nullptr;
	if (!ASC)
	{
		return;
	}

	AbilitySystemComponent = ASC;
	AbilityInputBlockedHandle = ASC->RegisterGameplayTagEvent(
		FGameplayTag::RequestGameplayTag(TEXT("Gameplay.AbilityInputBlocked")),
		EGameplayTagEventType::NewOrRemoved).AddUObject(
			this, &ThisClass::HandleAbilityInputBlockedChanged);
	if (IsAbilityInputBlocked())
	{
		HandleAbilityInputBlockedChanged(
			FGameplayTag::RequestGameplayTag(TEXT("Gameplay.AbilityInputBlocked")), 1);
	}
}

void UHeroArenaHeroSelectUIComponent::HandleAbilityInputBlockedChanged(const FGameplayTag Tag, const int32 NewCount)
{
	if (NewCount <= 0)
	{
		return;
	}
	if (UHeroArenaHeroSelectWidget* Screen = SelectionScreen.Get(); Screen && Screen->IsActivated())
	{
		Screen->CloseRespawnSelection();
		UE_LOG(LogHeroArenaHeroSelectUIComponent, Log,
			TEXT("Respawn hero selection closed because gameplay input is blocked"));
	}
}

bool UHeroArenaHeroSelectUIComponent::IsAbilityInputBlocked() const
{
	const ULyraAbilitySystemComponent* ASC = AbilitySystemComponent.Get();
	return ASC && ASC->HasMatchingGameplayTag(
		FGameplayTag::RequestGameplayTag(TEXT("Gameplay.AbilityInputBlocked")));
}

UHeroArenaHeroSelectWidget* UHeroArenaHeroSelectUIComponent::PushSelectionScreen()
{
	const ALyraHUD* HUD = Cast<ALyraHUD>(GetOwner());
	APlayerController* PlayerController = HUD ? HUD->GetOwningPlayerController() : nullptr;
	ULocalPlayer* LocalPlayer = PlayerController ? Cast<ULocalPlayer>(PlayerController->Player) : nullptr;
	if (!LocalPlayer)
	{
		return nullptr;
	}

	UHeroArenaHeroSelectWidget* Screen = Cast<UHeroArenaHeroSelectWidget>(
		UCommonUIExtensions::PushContentToLayer_ForPlayer(
			LocalPlayer, TAG_UI_LAYER_MENU, UHeroArenaHeroSelectWidget::StaticClass()));
	SelectionScreen = Screen;
	return Screen;
}

void UHeroArenaHeroSelectUIComponent::HandleRespawnCompleted(
	FGameplayTag Channel, const FLyraVerbMessage& Message)
{
	if (!IsMessageForOwningPlayer(Message.Instigator))
	{
		return;
	}
	if (UHeroArenaHeroSelectWidget* Screen = SelectionScreen.Get())
	{
		Screen->CloseRespawnSelection();
		UE_LOG(LogHeroArenaHeroSelectUIComponent, Log,
			TEXT("Respawn hero selection closed for %s"), *GetNameSafe(Message.Instigator));
	}
}

void UHeroArenaHeroSelectUIComponent::TryCreateHeroArenaUI()
{
	BindAbilityInputBlockedState();
	if (SelectionScreen.IsValid() && CombatHUD.IsValid())
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

	if (!SelectionScreen.IsValid())
	{
		SelectionScreen = PushSelectionScreen();
		if (SelectionScreen.IsValid())
		{
			UE_LOG(LogHeroArenaHeroSelectUIComponent, Log, TEXT("Hero Select UI added for %s"),
				*GetNameSafe(PlayerState));
		}
	}

	if (!CombatHUD.IsValid())
	{
		UHeroArenaCombatHUDWidget* Widget = CreateWidget<UHeroArenaCombatHUDWidget>(
			PlayerController, UHeroArenaCombatHUDWidget::StaticClass());
		if (Widget && Widget->AddToPlayerScreen(40))
		{
			CombatHUD = Widget;
			UE_LOG(LogHeroArenaHeroSelectUIComponent, Log, TEXT("Hero Arena tactical strip added for %s"),
				*GetNameSafe(PlayerState));
		}
	}

	if (SelectionScreen.IsValid() && CombatHUD.IsValid())
	{
		GetWorld()->GetTimerManager().ClearTimer(CreateScreenTimer);
	}
}
