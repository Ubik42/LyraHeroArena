#include "UI/HeroArenaHeroSelectWidget.h"

#include "CommonInputModeTypes.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Hero/HeroArenaHeroDefinition.h"
#include "Hero/HeroArenaHeroRoster.h"
#include "Input/Reply.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaHeroSelectWidget)

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaHeroSelectUI, Log, All);

namespace HeroArenaHeroSelectUI
{
	const FLinearColor BackdropColor(0.008f, 0.015f, 0.035f, 0.78f);
	const FLinearColor PanelColor(0.025f, 0.055f, 0.11f, 0.98f);
	const FLinearColor CardColor(0.055f, 0.12f, 0.22f, 1.0f);
	const FLinearColor AccentColor(0.12f, 0.72f, 1.0f, 1.0f);
	const FLinearColor SuccessColor(0.22f, 0.95f, 0.55f, 1.0f);
	const FLinearColor FailureColor(1.0f, 0.32f, 0.28f, 1.0f);
}

UHeroArenaHeroSelectWidget::UHeroArenaHeroSelectWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bAutoActivate = true;
	SetIsFocusable(true);
}

TOptional<FUIInputConfig> UHeroArenaHeroSelectWidget::GetDesiredInputConfig() const
{
	return FUIInputConfig(ECommonInputMode::Menu, EMouseCaptureMode::NoCapture);
}

TSharedRef<SWidget> UHeroArenaHeroSelectWidget::RebuildWidget()
{
	TSharedRef<SOverlay> Root = SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(HeroArenaHeroSelectUI::BackdropColor)
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.Padding(48.0f)
		[
			SNew(SBox)
			.WidthOverride(1040.0f)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(HeroArenaHeroSelectUI::PanelColor)
				.Padding(FMargin(44.0f, 32.0f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(NSLOCTEXT("HeroArenaHeroSelect", "Title", "选择英雄"))
						.ColorAndOpacity(FLinearColor::White)
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 34))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 4.0f, 0.0f, 20.0f)
					[
						SNew(STextBlock)
						.Text(NSLOCTEXT("HeroArenaHeroSelect", "Subtitle", "选择你的战斗职责 · 服务器确认后立即生效"))
						.ColorAndOpacity(FLinearColor(0.58f, 0.72f, 0.88f, 1.0f))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 17))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 7.0f)
					[
						SAssignNew(ContextText, STextBlock)
						.Text(NSLOCTEXT("HeroArenaHeroSelect", "OpeningContext", "开局阵容 · 选择后即可进入战场"))
						.ColorAndOpacity(FLinearColor(0.92f, 0.62f, 0.28f, 1.0f))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
					[
						SAssignNew(TeamCompositionText, STextBlock)
						.Text(NSLOCTEXT("HeroArenaHeroSelect", "TeamCompositionLoading", "我方阵容正在同步"))
						.ColorAndOpacity(HeroArenaHeroSelectUI::AccentColor)
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 18.0f)
					[
						SAssignNew(TeamHeroListText, STextBlock)
						.ColorAndOpacity(FLinearColor(0.66f, 0.74f, 0.82f, 1.0f))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 13))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SAssignNew(HeroCardsBox, SVerticalBox)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 20.0f, 0.0f, 0.0f)
					[
						SAssignNew(CurrentHeroText, STextBlock)
						.ColorAndOpacity(HeroArenaHeroSelectUI::AccentColor)
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 18))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 18.0f)
					[
						SAssignNew(StatusText, STextBlock)
						.Text(NSLOCTEXT("HeroArenaHeroSelect", "Ready", "请选择一名英雄"))
						.ColorAndOpacity(FLinearColor(0.78f, 0.84f, 0.92f, 1.0f))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 16))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Right)
					[
						SNew(SBox)
						.WidthOverride(210.0f)
						.HeightOverride(52.0f)
						[
							SNew(SButton)
							.ButtonColorAndOpacity(HeroArenaHeroSelectUI::AccentColor)
							.ContentPadding(FMargin(18.0f, 10.0f))
							.OnClicked_UObject(this, &ThisClass::HandleContinuePressed)
							[
								SNew(STextBlock)
								.Text(NSLOCTEXT("HeroArenaHeroSelect", "Continue", "继续战斗"))
								.Justification(ETextJustify::Center)
								.ColorAndOpacity(FLinearColor::White)
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 18))
							]
						]
					]
				]
			]
		];

	RebuildHeroCards();
	RefreshSelectionText();
	RefreshTeamComposition();
	return Root;
}

void UHeroArenaHeroSelectWidget::ReleaseSlateResources(const bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	HeroCardsBox.Reset();
	CurrentHeroText.Reset();
	StatusText.Reset();
	ContextText.Reset();
	TeamCompositionText.Reset();
	TeamHeroListText.Reset();
	HeroButtons.Reset();
	HeroIds.Reset();
}

void UHeroArenaHeroSelectWidget::OpenForRespawn(const float RespawnDuration)
{
	bRespawnContext = true;
	if (ContextText.IsValid())
	{
		ContextText->SetText(FText::FromString(FString::Printf(
			TEXT("重生窗口 %.0f 秒 · 现在可调整英雄"), FMath::Max(0.0f, RespawnDuration))));
	}
	if (StatusText.IsValid())
	{
		StatusText->SetText(NSLOCTEXT("HeroArenaHeroSelect", "RespawnSelection", "选择会由服务器确认，并应用到下一名 Pawn"));
		StatusText->SetColorAndOpacity(HeroArenaHeroSelectUI::AccentColor);
	}
	RefreshSelectionText();
	ActivateWidget();
	SetFocus();
}

void UHeroArenaHeroSelectWidget::CloseRespawnSelection()
{
	if (!bRespawnContext)
	{
		return;
	}
	bRespawnContext = false;
	if (ContextText.IsValid())
	{
		ContextText->SetText(NSLOCTEXT("HeroArenaHeroSelect", "OpeningContext2", "开局阵容 · 选择后即可进入战场"));
	}
	DeactivateWidget();
}

void UHeroArenaHeroSelectWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BindSelectionComponent();
	RebuildHeroCards();
	RefreshSelectionText();
	RefreshTeamComposition();
}

void UHeroArenaHeroSelectWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	const double Now = FPlatformTime::Seconds();
	if (Now >= NextTeamRefreshTime)
	{
		NextTeamRefreshTime = Now + 0.25;
		RefreshTeamComposition();
	}
}

void UHeroArenaHeroSelectWidget::NativeDestruct()
{
	if (UHeroArenaHeroSelectionComponent* Selection = SelectionComponent.Get())
	{
		Selection->OnHeroChanged.RemoveDynamic(this, &ThisClass::HandleHeroChanged);
		Selection->OnSelectionLockChanged.RemoveDynamic(this, &ThisClass::HandleSelectionLockChanged);
		Selection->OnSelectionRequestResolved.RemoveDynamic(this, &ThisClass::HandleSelectionRequestResolved);
	}
	SelectionComponent.Reset();
	Super::NativeDestruct();
}

void UHeroArenaHeroSelectWidget::NativeOnActivated()
{
	Super::NativeOnActivated();
	FocusedHeroIndex = 0;
	SetFocus();
}

bool UHeroArenaHeroSelectWidget::NativeOnHandleBackAction()
{
	DeactivateWidget();
	return true;
}

FReply UHeroArenaHeroSelectWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Escape)
	{
		DeactivateWidget();
		return FReply::Handled();
	}
	if (HeroButtons.Num() == 0 || HeroIds.Num() != HeroButtons.Num())
	{
		return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
	}
	if (Key == EKeys::Left || Key == EKeys::Up)
	{
		FocusedHeroIndex = (FocusedHeroIndex - 1 + HeroButtons.Num()) % HeroButtons.Num();
		FSlateApplication::Get().SetKeyboardFocus(HeroButtons[FocusedHeroIndex], EFocusCause::Navigation);
		return FReply::Handled();
	}
	if (Key == EKeys::Right || Key == EKeys::Down)
	{
		FocusedHeroIndex = (FocusedHeroIndex + 1) % HeroButtons.Num();
		FSlateApplication::Get().SetKeyboardFocus(HeroButtons[FocusedHeroIndex], EFocusCause::Navigation);
		return FReply::Handled();
	}
	if (Key == EKeys::Enter)
	{
		return HandleHeroPressed(HeroIds[FocusedHeroIndex]);
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UHeroArenaHeroSelectWidget::BindSelectionComponent()
{
	APlayerController* PlayerController = GetOwningPlayer();
	APlayerState* PlayerState = PlayerController ? PlayerController->PlayerState : nullptr;
	UHeroArenaHeroSelectionComponent* Selection = PlayerState
		? PlayerState->FindComponentByClass<UHeroArenaHeroSelectionComponent>()
		: nullptr;
	if (!Selection || SelectionComponent.Get() == Selection)
	{
		return;
	}

	SelectionComponent = Selection;
	Selection->OnHeroChanged.AddUniqueDynamic(this, &ThisClass::HandleHeroChanged);
	Selection->OnSelectionLockChanged.AddUniqueDynamic(this, &ThisClass::HandleSelectionLockChanged);
	Selection->OnSelectionRequestResolved.AddUniqueDynamic(this, &ThisClass::HandleSelectionRequestResolved);
	UE_LOG(LogHeroArenaHeroSelectUI, Log, TEXT("Hero Select UI bound to %s"), *GetNameSafe(PlayerState));
}

void UHeroArenaHeroSelectWidget::RebuildHeroCards()
{
	if (!HeroCardsBox.IsValid())
	{
		return;
	}
	HeroCardsBox->ClearChildren();
	HeroButtons.Reset();
	HeroIds.Reset();

	const UHeroArenaHeroSelectionComponent* Selection = SelectionComponent.Get();
	const UHeroArenaHeroRoster* Roster = Selection ? Selection->GetRoster() : nullptr;
	if (!Roster)
	{
		HeroCardsBox->AddSlot().AutoHeight()
		[
			SNew(STextBlock)
			.Text(NSLOCTEXT("HeroArenaHeroSelect", "RosterLoading", "正在同步英雄列表…"))
			.ColorAndOpacity(FLinearColor(0.8f, 0.85f, 0.92f, 1.0f))
		];
		return;
	}

	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	for (const UHeroArenaHeroDefinition* Hero : Roster->Heroes)
	{
		if (!Hero)
		{
			continue;
		}
		const FName HeroId = Hero->HeroId;
		TSharedPtr<SButton> HeroButton;
		Row->AddSlot()
		.FillWidth(1.0f)
		.Padding(7.0f)
		[
			SNew(SBox)
			.MinDesiredHeight(205.0f)
			[
				SAssignNew(HeroButton, SButton)
				.ButtonColorAndOpacity(HeroArenaHeroSelectUI::CardColor)
				.ContentPadding(FMargin(18.0f, 20.0f))
				.IsEnabled(!Selection->IsSelectionLocked())
				.OnClicked_Lambda([WeakThis = TWeakObjectPtr<UHeroArenaHeroSelectWidget>(this), HeroId]()
				{
					return WeakThis.IsValid() ? WeakThis->HandleHeroPressed(HeroId) : FReply::Unhandled();
				})
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(Hero->DisplayName)
						.ColorAndOpacity(FLinearColor::White)
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 24))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 5.0f, 0.0f, 12.0f)
					[
						SNew(STextBlock)
						.Text(GetRoleText(Hero))
						.ColorAndOpacity(HeroArenaHeroSelectUI::AccentColor)
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 15))
					]
					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					[
						SNew(STextBlock)
						.Text(Hero->ShortDescription)
						.AutoWrapText(true)
						.ColorAndOpacity(FLinearColor(0.72f, 0.8f, 0.9f, 1.0f))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 15))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 14.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(FText::Format(NSLOCTEXT("HeroArenaHeroSelect", "SelectHero", "选择 {0}"), Hero->DisplayName))
						.ColorAndOpacity(FLinearColor(0.9f, 0.96f, 1.0f, 1.0f))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
					]
				]
			]
		];
		HeroButtons.Add(HeroButton);
		HeroIds.Add(HeroId);
	}
	HeroCardsBox->AddSlot().AutoHeight()[Row];
}

void UHeroArenaHeroSelectWidget::RefreshSelectionText()
{
	if (!CurrentHeroText.IsValid())
	{
		return;
	}
	const UHeroArenaHeroDefinition* SelectedHero = SelectionComponent.IsValid()
		? SelectionComponent->GetSelectedHero()
		: nullptr;
	CurrentHeroText->SetText(SelectedHero
		? FText::Format(bRespawnContext
			? NSLOCTEXT("HeroArenaHeroSelect", "RespawnHero", "将以 {0} 返回战场")
			: NSLOCTEXT("HeroArenaHeroSelect", "CurrentHero", "当前英雄：{0}"), SelectedHero->DisplayName)
		: NSLOCTEXT("HeroArenaHeroSelect", "NoCurrentHero", "当前英雄：等待服务器同步"));
}

void UHeroArenaHeroSelectWidget::RefreshTeamComposition()
{
	const UHeroArenaHeroSelectionComponent* Selection = SelectionComponent.Get();
	const FText Summary = Selection
		? Selection->GetTeamCompositionSummary()
		: NSLOCTEXT("HeroArenaHeroSelect", "TeamCompositionLoading2", "我方阵容正在同步");
	if (TeamCompositionText.IsValid())
	{
		TeamCompositionText->SetText(Summary);
	}
	if (TeamHeroListText.IsValid())
	{
		TeamHeroListText->SetText(Selection ? Selection->GetTeamHeroList() : FText::GetEmpty());
	}
	const FString SummaryString = Summary.ToString();
	if (Selection && !SummaryString.IsEmpty() && SummaryString != LastTeamCompositionLog)
	{
		LastTeamCompositionLog = SummaryString;
		UE_LOG(LogHeroArenaHeroSelectUI, Log, TEXT("Team composition updated: %s | %s"),
			*SummaryString, *Selection->GetTeamHeroList().ToString());
	}
}

FReply UHeroArenaHeroSelectWidget::HandleHeroPressed(const FName HeroId)
{
	if (UHeroArenaHeroSelectionComponent* Selection = SelectionComponent.Get())
	{
		if (StatusText.IsValid())
		{
			StatusText->SetText(NSLOCTEXT("HeroArenaHeroSelect", "Requesting", "正在等待服务器确认…"));
			StatusText->SetColorAndOpacity(HeroArenaHeroSelectUI::AccentColor);
		}
		Selection->RequestHeroSelection(HeroId);
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply UHeroArenaHeroSelectWidget::HandleContinuePressed()
{
	DeactivateWidget();
	return FReply::Handled();
}

FText UHeroArenaHeroSelectWidget::GetRoleText(const UHeroArenaHeroDefinition* Hero) const
{
	if (!Hero)
	{
		return FText::GetEmpty();
	}
	switch (Hero->Role)
	{
	case EHeroArenaRole::Vanguard:
		return NSLOCTEXT("HeroArenaHeroSelect", "RoleVanguard", "前排 · 阵地推进");
	case EHeroArenaRole::Support:
		return NSLOCTEXT("HeroArenaHeroSelect", "RoleSupport", "支援 · 团队强化");
	case EHeroArenaRole::Damage:
	default:
		return NSLOCTEXT("HeroArenaHeroSelect", "RoleDamage", "输出 · 持续火力");
	}
}

void UHeroArenaHeroSelectWidget::HandleHeroChanged(
	const UHeroArenaHeroDefinition* OldHero, const UHeroArenaHeroDefinition* NewHero)
{
	RefreshSelectionText();
	RefreshTeamComposition();
}

void UHeroArenaHeroSelectWidget::HandleSelectionLockChanged(const bool bIsLocked)
{
	RebuildHeroCards();
	if (StatusText.IsValid() && bIsLocked)
	{
		StatusText->SetText(UHeroArenaHeroSelectionComponent::GetSelectionResultText(
			EHeroArenaHeroSelectionResult::SelectionLocked));
		StatusText->SetColorAndOpacity(HeroArenaHeroSelectUI::FailureColor);
	}
}

void UHeroArenaHeroSelectWidget::HandleSelectionRequestResolved(
	const FName HeroId, const EHeroArenaHeroSelectionResult Result)
{
	RefreshSelectionText();
	if (StatusText.IsValid())
	{
		StatusText->SetText(UHeroArenaHeroSelectionComponent::GetSelectionResultText(Result));
		const bool bSuccess = Result == EHeroArenaHeroSelectionResult::Success ||
			Result == EHeroArenaHeroSelectionResult::AlreadySelected;
		StatusText->SetColorAndOpacity(bSuccess
			? HeroArenaHeroSelectUI::SuccessColor
			: HeroArenaHeroSelectUI::FailureColor);
	}
}
