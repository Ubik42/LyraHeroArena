#include "UI/HeroArenaCombatHUDWidget.h"

#include "AbilitySystem/LyraAbilitySystemComponent.h"
#include "AbilitySystem/Abilities/LyraAbilitySimpleFailureMessage.h"
#include "Components/HeroArenaBlinkChargeComponent.h"
#include "Components/HeroArenaBattlePulseComponent.h"
#include "Components/HeroArenaControlPointRotationComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameStateBase.h"
#include "Hero/HeroArenaHeroDefinition.h"
#include "Hero/HeroArenaHeroSelectionComponent.h"
#include "HAL/IConsoleManager.h"
#include "Messages/ControlPointStatusMessage.h"
#include "Messages/LyraVerbMessage.h"
#include "Misc/CommandLine.h"
#include "Player/LyraPlayerState.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaCombatHUDWidget)

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaCombatHUD, Log, All);
CSV_DEFINE_CATEGORY(HeroArenaHUD, true);

namespace HeroArenaCombatHUD
{
	static TAutoConsoleVariable<float> CVarRefreshHz(
		TEXT("heroarena.HUDRefreshHz"),
		10.0f,
		TEXT("Hero Arena tactical HUD refresh rate. Use -HeroArenaHUDFullRate for the legacy every-frame baseline."),
		ECVF_Default);

	const FLinearColor Ink(0.018f, 0.026f, 0.034f, 0.92f);
	const FLinearColor Strip(0.045f, 0.058f, 0.070f, 0.96f);
	const FLinearColor Muted(0.57f, 0.63f, 0.68f, 1.0f);
	const FLinearColor Ready(0.76f, 0.84f, 0.87f, 1.0f);
	const FLinearColor Vanguard(0.91f, 0.55f, 0.22f, 1.0f);
	const FLinearColor Damage(0.88f, 0.30f, 0.26f, 1.0f);
	const FLinearColor Support(0.20f, 0.70f, 0.66f, 1.0f);
	const FLinearColor BlueObjective(0.025f, 0.15f, 0.42f, 0.94f);
	const FLinearColor RedObjective(0.42f, 0.035f, 0.04f, 0.94f);

	void SetTextIfChanged(const TSharedPtr<STextBlock>& Widget, const FText& Text)
	{
		if (Widget.IsValid() && !Widget->GetText().EqualTo(Text))
		{
			Widget->SetText(Text);
		}
	}

	bool IsFullRateBaseline()
	{
		static const bool bFullRate = FParse::Param(FCommandLine::Get(), TEXT("HeroArenaHUDFullRate"));
		return bFullRate;
	}

	struct FHeroLayout
	{
		const TCHAR* Role;
		const TCHAR* Keys[4];
		const TCHAR* Labels[4];
		const TCHAR* Cooldowns[4];
		bool Passive[4];
	};

	const FHeroLayout& GetLayout(const FName HeroId)
	{
		static const FHeroLayout Striker = { TEXT("突击"), {TEXT("SHIFT"), TEXT("Q"), TEXT("E"), TEXT("R")}, {TEXT("战术疾跑"), TEXT("螺旋爆弹"), TEXT("恢复场"), TEXT("武器过载")}, {TEXT(""), TEXT(""), TEXT("Cooldown.Ability.RecoveryField"), TEXT("Cooldown.Ability.WeaponOverload")}, {false, false, false, false} };
		static const FHeroLayout Bulwark = { TEXT("前排"), {TEXT("Q"), TEXT("SHIFT"), TEXT("E"), TEXT("R")}, {TEXT("正面屏障"), TEXT("推进冲锋"), TEXT("动能护盾"), TEXT("震地")}, {TEXT("Cooldown.Ability.Barrier"), TEXT("Cooldown.Ability.Charge"), TEXT("Cooldown.Ability.KineticShield"), TEXT("Cooldown.Ability.GroundSlam")}, {false, false, false, false} };
		static const FHeroLayout Pulse = { TEXT("支援"), {TEXT("Q"), TEXT("F"), TEXT("被动"), TEXT("R")}, {TEXT("双相手雷"), TEXT("净化针"), TEXT("医疗回馈"), TEXT("超频注射")}, {TEXT("Cooldown.Ability.BioticGrenade"), TEXT("Cooldown.Ability.PurificationNeedle"), TEXT(""), TEXT("Cooldown.Ability.OverclockInjection")}, {false, false, true, false} };
		static const FHeroLayout Phase = { TEXT("游击"), {TEXT("SHIFT"), TEXT("E"), TEXT("被动"), TEXT("R")}, {TEXT("闪现"), TEXT("回溯"), TEXT("动量恢复"), TEXT("相位黏弹")}, {TEXT(""), TEXT("Cooldown.Ability.Recall"), TEXT(""), TEXT("Cooldown.Ability.StickyBomb")}, {false, false, true, false} };
		static const FHeroLayout Relay = { TEXT("构筑"), {TEXT("Q"), TEXT("E"), TEXT("F"), TEXT("R")}, {TEXT("中继信标"), TEXT("推进跳板"), TEXT("快速维护"), TEXT("团队放大器")}, {TEXT("Cooldown.Ability.RelayBeacon"), TEXT("Cooldown.Ability.RelayJumpPad"), TEXT("Cooldown.Ability.RelayQuickMaintenance"), TEXT("Cooldown.Ability.RelayTeamAmplifier")}, {false, false, false, false} };
		if (HeroId == TEXT("Bulwark")) return Bulwark;
		if (HeroId == TEXT("Pulse")) return Pulse;
		if (HeroId == TEXT("Phase")) return Phase;
		if (HeroId == TEXT("Relay")) return Relay;
		return Striker;
	}
}

UHeroArenaCombatHUDWidget::UHeroArenaCombatHUDWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

TSharedRef<SWidget> UHeroArenaCombatHUDWidget::RebuildWidget()
{
	TSharedRef<SHorizontalBox> SkillRow = SNew(SHorizontalBox);
	SkillKeyTexts.Reset();
	SkillLabelTexts.Reset();
	SkillStateTexts.Reset();
	for (int32 Index = 0; Index < 4; ++Index)
	{
		TSharedPtr<STextBlock> KeyText;
		TSharedPtr<STextBlock> LabelText;
		TSharedPtr<STextBlock> StateText;
		TSharedRef<SVerticalBox> SkillInfo = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SAssignNew(LabelText, STextBlock)
				.ColorAndOpacity(FLinearColor::White)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 0.0f)
			[
				SAssignNew(StateText, STextBlock)
				.ColorAndOpacity(HeroArenaCombatHUD::Ready)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
			];
		if (Index == 3)
		{
			SkillInfo->AddSlot().AutoHeight().Padding(0.0f, 3.0f, 0.0f, 0.0f)
			[
				SAssignNew(UltimateChargeBar, SProgressBar)
				.Percent(0.0f)
				.FillColorAndOpacity(CurrentAccent)
			];
		}
		SkillRow->AddSlot().AutoWidth().Padding(Index == 0 ? 0.0f : 1.0f, 0.0f)
		[
			SNew(SBox).WidthOverride(142.0f).HeightOverride(54.0f)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(HeroArenaCombatHUD::Strip)
				.Padding(FMargin(8.0f, 6.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
					[
						SNew(SBox).WidthOverride(36.0f).HeightOverride(36.0f).VAlign(VAlign_Center).HAlign(HAlign_Center)
						[
							SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0.10f, 0.12f, 0.14f, 1.0f)).Padding(2.0f)
							[
								SAssignNew(KeyText, STextBlock).Justification(ETextJustify::Center).ColorAndOpacity(FLinearColor::White).Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
							]
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[
						SkillInfo
					]
				]
			]
		];
		SkillKeyTexts.Add(KeyText);
		SkillLabelTexts.Add(LabelText);
		SkillStateTexts.Add(StateText);
	}

	TSharedRef<SWidget> Root = SNew(SOverlay)
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0.0f, 0.0f, 0.0f, 250.0f)
		[
			SAssignNew(AbilityFailureToast, SBorder)
			.Visibility(EVisibility::Collapsed)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.12f, 0.045f, 0.025f, 0.94f))
			.Padding(FMargin(16.0f, 8.0f))
			[
				SAssignNew(AbilityFailureText, STextBlock)
				.ColorAndOpacity(FLinearColor(1.0f, 0.78f, 0.45f, 1.0f))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
			]
		]
		// Lyra's score and three control-point indicators occupy the top 200 Slate units.
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0.0f, 220.0f, 0.0f, 0.0f)
		[
			SAssignNew(ObjectiveToast, SBorder)
			.Visibility(EVisibility::Collapsed)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(HeroArenaCombatHUD::Ink)
			.Padding(FMargin(18.0f, 8.0f))
			[
				SAssignNew(ObjectiveText, STextBlock).ColorAndOpacity(FLinearColor::White).Font(FCoreStyle::GetDefaultFontStyle("Bold", 15))
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(30.0f, 0.0f, 0.0f, 142.0f)
		[
			SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(HeroArenaCombatHUD::Ink).Padding(0.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SAssignNew(AccentRail, SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(CurrentAccent).Padding(0.0f)
					[
						SNew(SBox).WidthOverride(5.0f)
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(14.0f, 9.0f, 16.0f, 9.0f).VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(112.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()[SAssignNew(HeroNameText, STextBlock).Text(NSLOCTEXT("HeroArenaHUD", "NoHero", "待命")).ColorAndOpacity(FLinearColor::White).Font(FCoreStyle::GetDefaultFontStyle("Bold", 18))]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 1.0f, 0.0f, 0.0f)[SAssignNew(RoleText, STextBlock).Text(NSLOCTEXT("HeroArenaHUD", "Role", "选择职责")).ColorAndOpacity(HeroArenaCombatHUD::Muted).Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))]
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SkillRow]
				+ SHorizontalBox::Slot().AutoWidth().Padding(14.0f, 9.0f, 14.0f, 9.0f).VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(140.0f)[SAssignNew(StatusText, STextBlock).Text(NSLOCTEXT("HeroArenaHUD", "Status", "战斗状态稳定")).ColorAndOpacity(HeroArenaCombatHUD::Muted).Font(FCoreStyle::GetDefaultFontStyle("Regular", 11)).AutoWrapText(true)]
				]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(14.0f, 0.0f, 14.0f, 6.0f)
				[
					SAssignNew(TeamCompositionText, STextBlock)
					.Text(NSLOCTEXT("HeroArenaHUD", "TeamCompositionLoading", "我方阵容正在同步"))
					.ColorAndOpacity(HeroArenaCombatHUD::Muted)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
				]
			]
		];

	if (const UHeroArenaHeroSelectionComponent* Selection = SelectionComponent.Get())
	{
		RefreshHero(Selection->GetSelectedHero());
	}
	return Root;
}

void UHeroArenaCombatHUDWidget::ReleaseSlateResources(const bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	HeroNameText.Reset(); RoleText.Reset(); StatusText.Reset(); TeamCompositionText.Reset(); ObjectiveText.Reset(); ObjectiveToast.Reset(); AccentRail.Reset(); UltimateChargeBar.Reset();
	SkillKeyTexts.Reset(); SkillLabelTexts.Reset(); SkillStateTexts.Reset();
	AbilityFailureToast.Reset(); AbilityFailureText.Reset();
}

void UHeroArenaCombatHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BindPlayerState();
	ControlPointListener = UGameplayMessageSubsystem::Get(this).RegisterListener(
		FGameplayTag::RequestGameplayTag(TEXT("ShooterGame.ControlPoint.Captured.Message")),
		this, &ThisClass::HandleControlPointCaptured);
	RespawnCompletedListener = UGameplayMessageSubsystem::Get(this).RegisterListener(
		FGameplayTag::RequestGameplayTag(TEXT("Ability.Respawn.Completed.Message")),
		this, &ThisClass::HandleRespawnCompleted);
	AbilityFailureListener = UGameplayMessageSubsystem::Get(this).RegisterListener(
		FGameplayTag::RequestGameplayTag(TEXT("Ability.UserFacingSimpleActivateFail.Message")),
		this, &ThisClass::HandleAbilityFailure);
}

void UHeroArenaCombatHUDWidget::NativeDestruct()
{
	if (UHeroArenaHeroSelectionComponent* Selection = SelectionComponent.Get())
	{
		Selection->OnHeroChanged.RemoveDynamic(this, &ThisClass::HandleHeroChanged);
	}
	SelectionComponent.Reset();
	if (ControlPointListener.IsValid())
	{
		UGameplayMessageSubsystem::Get(this).UnregisterListener(ControlPointListener);
	}
	if (RespawnCompletedListener.IsValid())
	{
		UGameplayMessageSubsystem::Get(this).UnregisterListener(RespawnCompletedListener);
	}
	if (AbilityFailureListener.IsValid())
	{
		UGameplayMessageSubsystem::Get(this).UnregisterListener(AbilityFailureListener);
	}
	AbilityFailureEndTime = 0.0;
	Super::NativeDestruct();
}

void UHeroArenaCombatHUDWidget::HandleAbilityFailure(
	FGameplayTag Channel, const FLyraAbilitySimpleFailureMessage& Message)
{
	if (RefreshMatchEndPresentation()) return;
	// World message channels are shared; never show another player's failure on this HUD.
	if (!GetOwningPlayer() || Message.PlayerController != GetOwningPlayer() ||
		Message.UserFacingReason.IsEmpty() || !AbilityFailureToast.IsValid() || !AbilityFailureText.IsValid())
	{
		return;
	}
	HeroArenaCombatHUD::SetTextIfChanged(AbilityFailureText, Message.UserFacingReason);
	AbilityFailureToast->SetVisibility(EVisibility::SelfHitTestInvisible);
	AbilityFailureEndTime = FPlatformTime::Seconds() + 1.8;
	UE_LOG(LogHeroArenaCombatHUD, Log, TEXT("Ability failure shown: player=%s reason=%s"),
		*GetNameSafe(GetOwningPlayer()), *Message.UserFacingReason.ToString());
}

void UHeroArenaCombatHUDWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	const double Now = FPlatformTime::Seconds();
	if (AbilityFailureToast.IsValid() && AbilityFailureEndTime > 0.0 && Now >= AbilityFailureEndTime)
	{
		AbilityFailureToast->SetVisibility(EVisibility::Collapsed);
		AbilityFailureEndTime = 0.0;
	}
	if (!SelectionComponent.IsValid() && Now >= NextBindAttemptTime)
	{
		NextBindAttemptTime = Now + 0.5;
		BindPlayerState();
	}
	if (ObjectiveToast.IsValid() && ToastEndTime > 0.0 && Now >= ToastEndTime)
	{
		ToastEndTime = 0.0;
		bReturnGuidanceActive = false;
		RefreshObjectiveState();
	}

	const bool bFullRate = HeroArenaCombatHUD::IsFullRateBaseline();
	const float RefreshHz = FMath::Max(1.0f, HeroArenaCombatHUD::CVarRefreshHz.GetValueOnGameThread());
	if (bFullRate || Now >= NextRuntimeRefreshTime)
	{
		NextRuntimeRefreshTime = Now + (1.0 / RefreshHz);
		CSV_SCOPED_TIMING_STAT(HeroArenaHUD, Refresh);
		CSV_CUSTOM_STAT(HeroArenaHUD, RefreshCount, 1, ECsvCustomStatOp::Accumulate);
		RefreshRuntimeState();
		if (RefreshMatchEndPresentation())
		{
			// Terminal presentation overrides capture/respawn toasts from the same frame.
		}
		else if (ToastEndTime <= 0.0)
		{
			RefreshObjectiveState();
		}
		else if (bReturnGuidanceActive)
		{
			RefreshReturnGuidance();
		}
	}
	if (Now >= NextTeamRefreshTime)
	{
		NextTeamRefreshTime = Now + 0.25;
		const UHeroArenaHeroSelectionComponent* Selection = SelectionComponent.Get();
		HeroArenaCombatHUD::SetTextIfChanged(TeamCompositionText, Selection
			? Selection->GetTeamCompositionSummary()
			: NSLOCTEXT("HeroArenaHUD", "TeamCompositionLoading2", "我方阵容正在同步"));
	}
}

FString UHeroArenaCombatHUDWidget::GetObjectiveDistanceText(
	const UHeroArenaControlPointRotationComponent* Rotation) const
{
	const APawn* Pawn = GetOwningPlayerPawn();
	if (!Pawn || !Rotation || Rotation->GetCurrentPointLocation().IsNearlyZero())
	{
		return FString();
	}
	const int32 Meters = FMath::RoundToInt(FVector::Dist2D(
		Pawn->GetActorLocation(), Rotation->GetCurrentPointLocation()) / 100.0f);
	return FString::Printf(TEXT(" · %dm"), FMath::Max(0, Meters));
}

FString UHeroArenaCombatHUDWidget::GetObjectiveOwnerLabel(const int32 OwnerTeamId) const
{
	const APlayerController* PC = GetOwningPlayer();
	const ALyraPlayerState* LocalPlayerState = PC ? PC->GetPlayerState<ALyraPlayerState>() : nullptr;
	const int32 LocalTeamId = LocalPlayerState ? LocalPlayerState->GetTeamId() : INDEX_NONE;
	if (OwnerTeamId > 0 && LocalTeamId > 0)
	{
		return OwnerTeamId == LocalTeamId ? TEXT("我方") : TEXT("敌方");
	}
	return OwnerTeamId == 1 ? TEXT("蓝队") : OwnerTeamId == 2 ? TEXT("红队") : TEXT("一方队伍");
}

FLinearColor UHeroArenaCombatHUDWidget::GetObjectiveOwnerColor(const int32 OwnerTeamId) const
{
	return OwnerTeamId == 1 ? HeroArenaCombatHUD::BlueObjective
		: OwnerTeamId == 2 ? HeroArenaCombatHUD::RedObjective : HeroArenaCombatHUD::Ink;
}

bool UHeroArenaCombatHUDWidget::RefreshMatchEndPresentation()
{
	const AGameStateBase* State = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	const UHeroArenaControlPointRotationComponent* Rotation = State
		? State->FindComponentByClass<UHeroArenaControlPointRotationComponent>() : nullptr;
	if (!Rotation || !Rotation->HasMatchEnded())
	{
		bReportedMatchEnd = false;
		return false;
	}
	if (!ObjectiveText.IsValid() || !ObjectiveToast.IsValid()) return false;
	bReturnGuidanceActive = false;
	ToastEndTime = 0.0;
	AbilityFailureEndTime = 0.0;
	if (AbilityFailureToast.IsValid()) AbilityFailureToast->SetVisibility(EVisibility::Collapsed);
	ObjectiveToast->SetBorderBackgroundColor(HeroArenaCombatHUD::Ink);
	HeroArenaCombatHUD::SetTextIfChanged(ObjectiveText, NSLOCTEXT("HeroArenaHUD", "MatchEnded", "比赛结束 · 等待下一局"));
	HeroArenaCombatHUD::SetTextIfChanged(StatusText, NSLOCTEXT("HeroArenaHUD", "MatchEndedStatus", "对局已结束"));
	ObjectiveToast->SetVisibility(EVisibility::Visible);
	if (!bReportedMatchEnd)
	{
		bReportedMatchEnd = true;
		UE_LOG(LogHeroArenaCombatHUD, Log, TEXT("HUD match-end presentation: net=%d world_time=%.3f objective=%s"),
			int32(GetWorld()->GetNetMode()), GetWorld()->GetTimeSeconds(), *ObjectiveText->GetText().ToString());
	}
	return true;
}

void UHeroArenaCombatHUDWidget::RefreshObjectiveState()
{
	if (RefreshMatchEndPresentation()) return;
	if (!ObjectiveToast.IsValid() || !ObjectiveText.IsValid()) return;
	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	const UHeroArenaControlPointRotationComponent* Rotation = GameState
		? GameState->FindComponentByClass<UHeroArenaControlPointRotationComponent>() : nullptr;
	if (!Rotation)
	{
		return;
	}
	const FString Point = Rotation->GetCurrentPointLabel().ToString();
	const FString Distance = GetObjectiveDistanceText(Rotation);
	if (Rotation->IsTransitioning())
	{
		ObjectiveToast->SetBorderBackgroundColor(GetObjectiveOwnerColor(INDEX_NONE));
		ObjectiveText->SetText(FText::FromString(FString::Printf(TEXT("下一个目标 %s%s · %.0fs"),
			*Point, *Distance, Rotation->GetSecondsUntilChange())));
	}
	else if (Rotation->IsActivePointContested())
	{
		ObjectiveToast->SetBorderBackgroundColor(GetObjectiveOwnerColor(INDEX_NONE));
		ObjectiveText->SetText(FText::FromString(FString::Printf(TEXT("当前目标 %s%s · 双方争夺中"), *Point, *Distance)));
	}
	else if (Rotation->GetCapturingTeam() > 0
		&& Rotation->GetCapturingTeam() != Rotation->GetActiveOwnerTeam())
	{
		const int32 CapturingTeam = Rotation->GetCapturingTeam();
		ObjectiveToast->SetBorderBackgroundColor(GetObjectiveOwnerColor(CapturingTeam));
		const FString Team = GetObjectiveOwnerLabel(CapturingTeam);
		const int32 Percent = FMath::RoundToInt(Rotation->GetCaptureProgress() * 100.0f);
		const TCHAR* Action = Rotation->GetActiveOwnerTeam() > 0 ? TEXT("反抢") : TEXT("占领");
		ObjectiveText->SetText(FText::FromString(FString::Printf(TEXT("当前目标 %s%s · %s%s %d%%"),
			*Point, *Distance, *Team, Action, Percent)));
	}
	else if (Rotation->GetActiveOwnerTeam() > 0)
	{
		const int32 OwnerTeamId = Rotation->GetActiveOwnerTeam();
		ObjectiveToast->SetBorderBackgroundColor(GetObjectiveOwnerColor(OwnerTeamId));
		const FString Team = GetObjectiveOwnerLabel(OwnerTeamId);
		ObjectiveText->SetText(FText::FromString(FString::Printf(TEXT("目标 %s%s · %s控制 · %.0fs 后轮换"),
			*Point, *Distance, *Team, Rotation->GetSecondsUntilChange())));
	}
	else
	{
		ObjectiveToast->SetBorderBackgroundColor(GetObjectiveOwnerColor(INDEX_NONE));
		ObjectiveText->SetText(FText::FromString(FString::Printf(TEXT("当前目标 %s%s · 待争夺"), *Point, *Distance)));
	}
	ObjectiveToast->SetVisibility(EVisibility::Visible);
	ToastEndTime = 0.0;
}

void UHeroArenaCombatHUDWidget::BindPlayerState()
{
	APlayerController* PC = GetOwningPlayer();
	ALyraPlayerState* PS = PC ? PC->GetPlayerState<ALyraPlayerState>() : nullptr;
	UHeroArenaHeroSelectionComponent* Selection = PS ? PS->FindComponentByClass<UHeroArenaHeroSelectionComponent>() : nullptr;
	if (!Selection || SelectionComponent.Get() == Selection) return;
	if (UHeroArenaHeroSelectionComponent* Old = SelectionComponent.Get()) Old->OnHeroChanged.RemoveDynamic(this, &ThisClass::HandleHeroChanged);
	SelectionComponent = Selection;
	Selection->OnHeroChanged.AddUniqueDynamic(this, &ThisClass::HandleHeroChanged);
	RefreshHero(Selection->GetSelectedHero());
	UE_LOG(LogHeroArenaCombatHUD, Log, TEXT("Tactical strip bound to %s"), *GetNameSafe(PS));
}

void UHeroArenaCombatHUDWidget::HandleHeroChanged(const UHeroArenaHeroDefinition* OldHero, const UHeroArenaHeroDefinition* NewHero)
{
	RefreshHero(NewHero);
}

void UHeroArenaCombatHUDWidget::RefreshHero(const UHeroArenaHeroDefinition* Hero)
{
	CurrentHeroId = Hero ? Hero->HeroId : NAME_None;
	const HeroArenaCombatHUD::FHeroLayout& Layout = HeroArenaCombatHUD::GetLayout(CurrentHeroId);
	CurrentAccent = Hero && Hero->Role == EHeroArenaRole::Vanguard ? HeroArenaCombatHUD::Vanguard
		: Hero && Hero->Role == EHeroArenaRole::Support ? HeroArenaCombatHUD::Support : HeroArenaCombatHUD::Damage;
	if (HeroNameText.IsValid()) HeroNameText->SetText(Hero ? Hero->DisplayName : NSLOCTEXT("HeroArenaHUD", "NoHero2", "待命"));
	if (RoleText.IsValid()) RoleText->SetText(FText::FromString(Layout.Role));
	if (AccentRail.IsValid()) AccentRail->SetBorderBackgroundColor(CurrentAccent);
	if (UltimateChargeBar.IsValid()) UltimateChargeBar->SetFillColorAndOpacity(CurrentAccent);
	CooldownTags.Reset(); PassiveSkills.Reset();
	for (int32 Index = 0; Index < 4; ++Index)
	{
		CooldownTags.Add(FName(Layout.Cooldowns[Index]));
		PassiveSkills.Add(Layout.Passive[Index]);
		if (SkillKeyTexts.IsValidIndex(Index) && SkillKeyTexts[Index].IsValid()) SkillKeyTexts[Index]->SetText(FText::FromString(Layout.Keys[Index]));
		if (SkillLabelTexts.IsValidIndex(Index) && SkillLabelTexts[Index].IsValid()) SkillLabelTexts[Index]->SetText(FText::FromString(Layout.Labels[Index]));
	}
	RefreshRuntimeState();
}

float UHeroArenaCombatHUDWidget::GetCooldownRemaining(const FName CooldownTagName) const
{
	if (CooldownTagName.IsNone()) return 0.0f;
	const APlayerController* PC = GetOwningPlayer();
	const ALyraPlayerState* PS = PC ? PC->GetPlayerState<ALyraPlayerState>() : nullptr;
	const ULyraAbilitySystemComponent* ASC = PS ? PS->GetLyraAbilitySystemComponent() : nullptr;
	if (!ASC) return 0.0f;
	FGameplayTagContainer Tags(FGameplayTag::RequestGameplayTag(CooldownTagName));
	const FGameplayEffectQuery Query = FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(Tags);
	const TArray<float> Times = ASC->GetActiveEffectsTimeRemaining(Query);
	float Remaining = 0.0f;
	for (const float Time : Times) Remaining = FMath::Max(Remaining, Time);
	return Remaining;
}

FText UHeroArenaCombatHUDWidget::GetSkillState(const int32 SkillIndex) const
{
	if (SkillIndex == 3)
	{
		const UHeroArenaHeroSelectionComponent* Selection = SelectionComponent.Get();
		if (Selection)
		{
			return Selection->IsUltimateReady()
				? NSLOCTEXT("HeroArenaHUD", "UltimateReady", "终极就绪")
				: FText::FromString(FString::Printf(TEXT("终极能量 %.0f%%"), Selection->GetUltimateCharge()));
		}
	}
	if (CurrentHeroId == TEXT("Phase") && SkillIndex == 0)
	{
		const APlayerController* PC = GetOwningPlayer();
		const APlayerState* PS = PC ? PC->PlayerState : nullptr;
		if (const UHeroArenaBlinkChargeComponent* Blink = PS ? PS->FindComponentByClass<UHeroArenaBlinkChargeComponent>() : nullptr)
		{
			const float Remaining = Blink->GetNextChargeRemaining();
			if (Remaining <= 0.05f && Blink->GetCurrentCharges() == 0)
			{
				return NSLOCTEXT("HeroArenaHUD", "BlinkAwaitingCharge", "0 格 · 等待充能");
			}
			return Remaining > 0.05f
				? FText::FromString(FString::Printf(TEXT("%d/%d · %.1fs"), Blink->GetCurrentCharges(), Blink->GetMaxCharges(), Remaining))
				: FText::FromString(FString::Printf(TEXT("%d/%d · 就绪"), Blink->GetCurrentCharges(), Blink->GetMaxCharges()));
		}
	}
	if (PassiveSkills.IsValidIndex(SkillIndex) && PassiveSkills[SkillIndex]) return NSLOCTEXT("HeroArenaHUD", "Passive", "持续生效");
	const float Remaining = CooldownTags.IsValidIndex(SkillIndex) ? GetCooldownRemaining(CooldownTags[SkillIndex]) : 0.0f;
	return Remaining > 0.05f ? FText::FromString(FString::Printf(TEXT("%.1fs"), Remaining)) : NSLOCTEXT("HeroArenaHUD", "Ready", "就绪");
}

FText UHeroArenaCombatHUDWidget::GetStatusText() const
{
	const APlayerController* PC = GetOwningPlayer();
	const ALyraPlayerState* PS = PC ? PC->GetPlayerState<ALyraPlayerState>() : nullptr;
	const ULyraAbilitySystemComponent* ASC = PS ? PS->GetLyraAbilitySystemComponent() : nullptr;
	if (!ASC) return NSLOCTEXT("HeroArenaHUD", "Waiting", "正在接入战斗状态");
	TArray<FString> States;
	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	const UHeroArenaBattlePulseComponent* BattlePulse = GameState
		? GameState->FindComponentByClass<UHeroArenaBattlePulseComponent>() : nullptr;
	if (BattlePulse && BattlePulse->IsArcadeModeActive())
	{
		States.Add(FString::Printf(TEXT("%s · %.0fs"), *BattlePulse->GetCurrentPulseLabel().ToString(),
			BattlePulse->GetSecondsRemaining()));
	}
	auto AddIfPresent = [ASC, &States](const TCHAR* TagName, const TCHAR* Label)
	{
		if (ASC->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(TagName))) States.Add(Label);
	};
	AddIfPresent(TEXT("Status.HeroArena.StickyBomb.Attached"), TEXT("黏弹附着 · 即将爆炸"));
	AddIfPresent(TEXT("Status.HeroArena.GroundSlamSlowed"), TEXT("震地减速"));
	AddIfPresent(TEXT("Status.HeroArena.WeaponOverload"), TEXT("武器过载"));
	AddIfPresent(TEXT("Status.HeroArena.KineticShield"), TEXT("动能护盾"));
	AddIfPresent(TEXT("Status.HeroArena.Overclocked"), TEXT("超频"));
	AddIfPresent(TEXT("Status.HeroArena.TeamAmplified"), TEXT("团队增幅 +15%"));
	AddIfPresent(TEXT("Status.HeroArena.HealingAmplified"), TEXT("治疗增幅"));
	AddIfPresent(TEXT("Status.HeroArena.HealingSuppressed"), TEXT("治疗受限"));
	return States.IsEmpty() ? NSLOCTEXT("HeroArenaHUD", "Stable", "战斗状态稳定") : FText::FromString(FString::Join(States, TEXT(" · ")));
}

void UHeroArenaCombatHUDWidget::RefreshRuntimeState()
{
	for (int32 Index = 0; Index < SkillStateTexts.Num(); ++Index)
	{
		HeroArenaCombatHUD::SetTextIfChanged(SkillStateTexts[Index], GetSkillState(Index));
	}
	if (const UHeroArenaHeroSelectionComponent* Selection = SelectionComponent.Get())
	{
		if (UltimateChargeBar.IsValid())
		{
			UltimateChargeBar->SetPercent(FMath::Clamp(Selection->GetUltimateCharge() / 100.0f, 0.0f, 1.0f));
		}
		if (SkillStateTexts.IsValidIndex(3) && SkillStateTexts[3].IsValid())
		{
			SkillStateTexts[3]->SetColorAndOpacity(Selection->IsUltimateReady()
				? CurrentAccent : HeroArenaCombatHUD::Ready);
		}
	}
	HeroArenaCombatHUD::SetTextIfChanged(StatusText, GetStatusText());
#if !UE_BUILD_SHIPPING
	// Opt-in presentation trace only; keep it disabled during timing benchmarks.
	static const bool bTraceHUD = FParse::Param(FCommandLine::Get(), TEXT("HeroArenaHUDTrace"));
	if (bTraceHUD && GetWorld())
	{
		TArray<FString> SkillStates;
		for (const TSharedPtr<STextBlock>& Text : SkillStateTexts)
		{
			SkillStates.Add(Text.IsValid() ? Text->GetText().ToString() : TEXT("missing"));
		}
		UE_LOG(LogHeroArenaCombatHUD, Log,
			TEXT("HUDRefreshSample game=%.6f wall=%.6f frame=%llu fullRate=%d skills=[%s]"),
			GetWorld()->GetTimeSeconds(), FPlatformTime::Seconds(), GFrameCounter,
			HeroArenaCombatHUD::IsFullRateBaseline() ? 1 : 0, *FString::Join(SkillStates, TEXT(";")));
	}
#endif
}

void UHeroArenaCombatHUDWidget::HandleControlPointCaptured(FGameplayTag Channel, const FLyraControlPointStatusMessage& Message)
{
	if (RefreshMatchEndPresentation()) return;
	if (!ObjectiveToast.IsValid() || !ObjectiveText.IsValid()) return;
	bReturnGuidanceActive = false;
	const FString TeamText = GetObjectiveOwnerLabel(Message.OwnerTeamID);
	ObjectiveToast->SetBorderBackgroundColor(GetObjectiveOwnerColor(Message.OwnerTeamID));
	ObjectiveText->SetText(FText::FromString(FString::Printf(TEXT("%s已控制目标"), *TeamText)));
	ObjectiveToast->SetVisibility(EVisibility::Visible);
	ToastEndTime = FPlatformTime::Seconds() + 3.0;
}

void UHeroArenaCombatHUDWidget::RefreshReturnGuidance()
{
	if (RefreshMatchEndPresentation()) return;
	if (!ObjectiveText.IsValid())
	{
		return;
	}
	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	const UHeroArenaControlPointRotationComponent* Rotation = GameState
		? GameState->FindComponentByClass<UHeroArenaControlPointRotationComponent>() : nullptr;
	if (Rotation)
	{
		if (ObjectiveToast.IsValid())
		{
			ObjectiveToast->SetBorderBackgroundColor(GetObjectiveOwnerColor(INDEX_NONE));
		}
		ObjectiveText->SetText(FText::FromString(FString::Printf(TEXT("返回战场 · 目标 %s%s"),
			*Rotation->GetCurrentPointLabel().ToString(), *GetObjectiveDistanceText(Rotation))));
	}
}

void UHeroArenaCombatHUDWidget::HandleRespawnCompleted(FGameplayTag Channel, const FLyraVerbMessage& Message)
{
	if (RefreshMatchEndPresentation()) return;
	const APlayerController* PC = GetOwningPlayer();
	if (!PC || Message.Instigator != PC->PlayerState || !ObjectiveToast.IsValid() || !ObjectiveText.IsValid())
	{
		return;
	}
	bReturnGuidanceActive = true;
	RefreshReturnGuidance();
	ObjectiveToast->SetVisibility(EVisibility::Visible);
	ToastEndTime = FPlatformTime::Seconds() + 3.0;
}
