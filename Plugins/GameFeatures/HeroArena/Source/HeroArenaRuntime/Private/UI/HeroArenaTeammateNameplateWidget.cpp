#include "UI/HeroArenaTeammateNameplateWidget.h"

#include "Character/LyraHealthComponent.h"
#include "GameFramework/PlayerState.h"
#include "Hero/HeroArenaHeroDefinition.h"
#include "Hero/HeroArenaHeroSelectionComponent.h"
#include "Styling/CoreStyle.h"
#include "UI/IndicatorSystem/IndicatorDescriptor.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaTeammateNameplateWidget)

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaTeammateNameplate, Log, All);

namespace HeroArenaTeammateNameplate
{
	const FLinearColor Background(0.012f, 0.022f, 0.028f, 0.82f);
	const FLinearColor Muted(0.67f, 0.74f, 0.77f, 1.0f);
	const FLinearColor Vanguard(0.91f, 0.55f, 0.22f, 1.0f);
	const FLinearColor Damage(0.88f, 0.30f, 0.26f, 1.0f);
	const FLinearColor Support(0.20f, 0.70f, 0.66f, 1.0f);
	const FLinearColor Health(0.26f, 0.82f, 0.48f, 1.0f);

	FText GetRoleText(const EHeroArenaRole Role)
	{
		switch (Role)
		{
		case EHeroArenaRole::Vanguard:
			return NSLOCTEXT("HeroArenaNameplate", "Vanguard", "前排");
		case EHeroArenaRole::Support:
			return NSLOCTEXT("HeroArenaNameplate", "Support", "支援");
		case EHeroArenaRole::Damage:
		default:
			return NSLOCTEXT("HeroArenaNameplate", "Damage", "输出");
		}
	}

	FLinearColor GetRoleColor(const EHeroArenaRole Role)
	{
		return Role == EHeroArenaRole::Vanguard ? Vanguard
			: Role == EHeroArenaRole::Support ? Support : Damage;
	}
}

UHeroArenaTeammateNameplateWidget::UHeroArenaTeammateNameplateWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

TSharedRef<SWidget> UHeroArenaTeammateNameplateWidget::RebuildWidget()
{
	TSharedRef<SWidget> Root = SAssignNew(RootBorder, SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(HeroArenaTeammateNameplate::Background)
		.Padding(0.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SAssignNew(AccentRail, SBorder)
						.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
						.BorderBackgroundColor(HeroArenaTeammateNameplate::Damage)
						.Padding(0.0f)
					[
						SNew(SBox).WidthOverride(3.0f)
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(7.0f, 4.0f, 8.0f, 4.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SAssignNew(HeroNameText, STextBlock)
						.ColorAndOpacity(FLinearColor::White)
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SAssignNew(RoleText, STextBlock)
						.ColorAndOpacity(HeroArenaTeammateNameplate::Muted)
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox).HeightOverride(3.0f)
				[
					SAssignNew(HealthBar, SProgressBar)
					.Percent(1.0f)
					.FillColorAndOpacity(HeroArenaTeammateNameplate::Health)
				]
			]
		];
	RefreshIdentity();
	RefreshHealth();
	return Root;
}

void UHeroArenaTeammateNameplateWidget::ReleaseSlateResources(const bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	RootBorder.Reset();
	AccentRail.Reset();
	HeroNameText.Reset();
	RoleText.Reset();
	HealthBar.Reset();
}

void UHeroArenaTeammateNameplateWidget::NativeDestruct()
{
	BindSelection(nullptr);
	Super::NativeDestruct();
}

void UHeroArenaTeammateNameplateWidget::BindIndicator_Implementation(UIndicatorDescriptor* Indicator)
{
	BindSelection(Indicator
		? Cast<UHeroArenaHeroSelectionComponent>(Indicator->GetDataObject()) : nullptr);
}

void UHeroArenaTeammateNameplateWidget::UnbindIndicator_Implementation(const UIndicatorDescriptor* Indicator)
{
	BindSelection(nullptr);
}

void UHeroArenaTeammateNameplateWidget::BindSelection(UHeroArenaHeroSelectionComponent* NewSelection)
{
	if (UHeroArenaHeroSelectionComponent* PreviousSelection = SelectionComponent.Get())
	{
		PreviousSelection->OnHeroChanged.RemoveDynamic(this, &ThisClass::HandleHeroChanged);
		PreviousSelection->OnPawnChanged.RemoveDynamic(this, &ThisClass::HandlePawnChanged);
	}
	BindHealth(nullptr);
	SelectionComponent = NewSelection;
	if (NewSelection)
	{
		NewSelection->OnHeroChanged.AddUniqueDynamic(this, &ThisClass::HandleHeroChanged);
		NewSelection->OnPawnChanged.AddUniqueDynamic(this, &ThisClass::HandlePawnChanged);
	}
	RefreshIdentity();
	RefreshHealthBinding();
}

void UHeroArenaTeammateNameplateWidget::HandleHeroChanged(
	const UHeroArenaHeroDefinition* OldHero, const UHeroArenaHeroDefinition* NewHero)
{
	RefreshIdentity();
}

void UHeroArenaTeammateNameplateWidget::HandlePawnChanged(
	APlayerState* PlayerState, APawn* NewPawn, APawn* OldPawn)
{
	RefreshHealthBinding();
}

void UHeroArenaTeammateNameplateWidget::HandleHealthChanged(
	ULyraHealthComponent* ChangedHealth, const float OldValue, const float NewValue, AActor* Instigator)
{
	RefreshHealth();
}

void UHeroArenaTeammateNameplateWidget::BindHealth(ULyraHealthComponent* NewHealth)
{
	if (ULyraHealthComponent* PreviousHealth = HealthComponent.Get())
	{
		PreviousHealth->OnHealthChanged.RemoveDynamic(this, &ThisClass::HandleHealthChanged);
		PreviousHealth->OnMaxHealthChanged.RemoveDynamic(this, &ThisClass::HandleHealthChanged);
	}
	HealthComponent = NewHealth;
	if (NewHealth)
	{
		NewHealth->OnHealthChanged.AddUniqueDynamic(this, &ThisClass::HandleHealthChanged);
		NewHealth->OnMaxHealthChanged.AddUniqueDynamic(this, &ThisClass::HandleHealthChanged);
		UE_LOG(LogHeroArenaTeammateNameplate, Log,
			TEXT("Teammate health bar bound: pawn=%s health=%.0f/%.0f"),
			*GetNameSafe(NewHealth->GetOwner()), NewHealth->GetHealth(), NewHealth->GetMaxHealth());
	}
	RefreshHealth();
}

void UHeroArenaTeammateNameplateWidget::RefreshHealthBinding()
{
	const UHeroArenaHeroSelectionComponent* Selection = SelectionComponent.Get();
	const APlayerState* PlayerState = Selection ? Cast<APlayerState>(Selection->GetOwner()) : nullptr;
	BindHealth(ULyraHealthComponent::FindHealthComponent(PlayerState ? PlayerState->GetPawn() : nullptr));
}

void UHeroArenaTeammateNameplateWidget::RefreshIdentity()
{
	const UHeroArenaHeroSelectionComponent* Selection = SelectionComponent.Get();
	const UHeroArenaHeroDefinition* Hero = Selection ? Selection->GetSelectedHero() : nullptr;
	if (RootBorder.IsValid())
	{
		RootBorder->SetVisibility(Hero ? EVisibility::Visible : EVisibility::Collapsed);
	}
	if (!Hero)
	{
		return;
	}
	if (HeroNameText.IsValid())
	{
		HeroNameText->SetText(Hero->DisplayName);
	}
	if (RoleText.IsValid())
	{
		RoleText->SetText(HeroArenaTeammateNameplate::GetRoleText(Hero->Role));
	}
	if (AccentRail.IsValid())
	{
		AccentRail->SetBorderBackgroundColor(HeroArenaTeammateNameplate::GetRoleColor(Hero->Role));
	}
}

void UHeroArenaTeammateNameplateWidget::RefreshHealth()
{
	if (!HealthBar.IsValid())
	{
		return;
	}
	const ULyraHealthComponent* Health = HealthComponent.Get();
	HealthBar->SetVisibility(Health ? EVisibility::Visible : EVisibility::Collapsed);
	HealthBar->SetPercent(Health ? TOptional<float>(Health->GetHealthNormalized()) : TOptional<float>());
}
