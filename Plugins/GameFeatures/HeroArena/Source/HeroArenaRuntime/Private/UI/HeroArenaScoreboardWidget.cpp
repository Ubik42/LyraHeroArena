#include "UI/HeroArenaScoreboardWidget.h"

#include "GameFramework/GameStateBase.h"
#include "GameModes/LyraExperienceDefinition.h"
#include "GameModes/LyraExperienceManagerComponent.h"
#include "Hero/HeroArenaHeroDefinition.h"
#include "Hero/HeroArenaHeroSelectionComponent.h"
#include "Player/LyraPlayerState.h"
#include "Styling/CoreStyle.h"
#include "Teams/LyraTeamSubsystem.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaScoreboardWidget)

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaScoreboard, Log, All);

namespace HeroArenaScoreboard
{
	const FLinearColor Backdrop(0.012f, 0.019f, 0.025f, 0.96f);
	const FLinearColor Panel(0.035f, 0.047f, 0.057f, 0.97f);
	const FLinearColor RowA(0.055f, 0.068f, 0.078f, 0.94f);
	const FLinearColor RowB(0.041f, 0.053f, 0.062f, 0.94f);
	const FLinearColor Muted(0.53f, 0.59f, 0.63f, 1.0f);
	const FLinearColor Blue(0.18f, 0.72f, 0.82f, 1.0f);
	const FLinearColor Red(0.92f, 0.31f, 0.24f, 1.0f);
	const FLinearColor Vanguard(0.91f, 0.55f, 0.22f, 1.0f);
	const FLinearColor Damage(0.88f, 0.30f, 0.26f, 1.0f);
	const FLinearColor Support(0.20f, 0.70f, 0.66f, 1.0f);

	FText GetRoleName(const EHeroArenaRole Role)
	{
		switch (Role)
		{
		case EHeroArenaRole::Vanguard: return NSLOCTEXT("HeroArenaScoreboard", "Vanguard", "前排");
		case EHeroArenaRole::Support: return NSLOCTEXT("HeroArenaScoreboard", "Support", "支援");
		default: return NSLOCTEXT("HeroArenaScoreboard", "Damage", "输出");
		}
	}
}

UHeroArenaScoreboardWidget::UHeroArenaScoreboardWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

TSharedRef<SWidget> UHeroArenaScoreboardWidget::RebuildWidget()
{
	BlueRows.SetNum(5);
	RedRows.SetNum(5);

	TSharedRef<SWidget> Root = SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(HeroArenaScoreboard::Backdrop)
			.Padding(FMargin(42.0f, 34.0f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 22.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SAssignNew(ModeTitle, STextBlock)
							.Text(NSLOCTEXT("HeroArenaScoreboard", "Title", "英雄竞技场"))
							.ColorAndOpacity(FLinearColor::White)
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 26))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
						[
							SAssignNew(ModeSubtitle, STextBlock)
							.Text(NSLOCTEXT("HeroArenaScoreboard", "Subtitle", "轮换控制点 · 阵容职责与比赛数据"))
							.ColorAndOpacity(HeroArenaScoreboard::Muted)
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)
					[
						SNew(STextBlock)
						.Text(NSLOCTEXT("HeroArenaScoreboard", "Hint", "松开 TAB 返回战斗"))
						.ColorAndOpacity(HeroArenaScoreboard::Muted)
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
					]
				]
				+ SVerticalBox::Slot().FillHeight(1.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 7.0f, 0.0f)
					[
						BuildTeamPanel(1, HeroArenaScoreboard::Blue)
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(7.0f, 0.0f, 0.0f, 0.0f)
					[
						BuildTeamPanel(2, HeroArenaScoreboard::Red)
					]
				]
			]
		];

	RefreshModePresentation();
	RefreshScoreboard();
	return Root;
}

FName UHeroArenaScoreboardWidget::GetCurrentExperienceName() const
{
	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	const ULyraExperienceManagerComponent* ExperienceManager = GameState
		? GameState->FindComponentByClass<ULyraExperienceManagerComponent>() : nullptr;
	if (!ExperienceManager || !ExperienceManager->IsExperienceLoaded())
	{
		return NAME_None;
	}
	return ExperienceManager->GetCurrentExperienceChecked()->GetPrimaryAssetId().PrimaryAssetName;
}

FName UHeroArenaScoreboardWidget::GetTeamScoreTagName() const
{
	return CachedExperienceName.ToString().Contains(TEXT("Elimination"))
		? FName(TEXT("ShooterGame.Score.Eliminations"))
		: FName(TEXT("ShooterGame.ControlPoint.TeamScore"));
}

void UHeroArenaScoreboardWidget::RefreshModePresentation()
{
	const FName ExperienceName = GetCurrentExperienceName();
	if (ExperienceName.IsNone() || ExperienceName == CachedExperienceName)
	{
		return;
	}
	CachedExperienceName = ExperienceName;
	const FString Experience = ExperienceName.ToString();
	FText Subtitle = NSLOCTEXT("HeroArenaScoreboard", "ControlPointSubtitle",
		"轮换控制点 · 阵容职责与比赛数据");
	if (Experience.Contains(TEXT("ArcadeControlPoints")))
	{
		Subtitle = NSLOCTEXT("HeroArenaScoreboard", "BattlePulseSubtitle",
			"战场脉冲 · 周期规则与控制点数据");
	}
	else if (Experience.Contains(TEXT("Elimination")))
	{
		Subtitle = NSLOCTEXT("HeroArenaScoreboard", "EliminationSubtitle",
			"团队淘汰 · 生存、击杀与职责贡献");
	}
	if (ModeTitle.IsValid())
	{
		ModeTitle->SetText(NSLOCTEXT("HeroArenaScoreboard", "ModeTitle", "英雄竞技场"));
	}
	if (ModeSubtitle.IsValid())
	{
		ModeSubtitle->SetText(Subtitle);
	}
	UE_LOG(LogHeroArenaScoreboard, Log, TEXT("Scoreboard mode resolved: experience=%s team_score=%s"),
		*Experience, *GetTeamScoreTagName().ToString());
}

TSharedRef<SWidget> UHeroArenaScoreboardWidget::BuildTeamPanel(const int32 TeamId, const FLinearColor& TeamColor)
{
	TArray<FPlayerRow>& Rows = TeamId == 1 ? BlueRows : RedRows;
	TSharedPtr<STextBlock>& Score = TeamId == 1 ? BlueScore : RedScore;
	TSharedRef<SVerticalBox> PlayerRows = SNew(SVerticalBox);
	for (int32 RowIndex = 0; RowIndex < Rows.Num(); ++RowIndex)
	{
		PlayerRows->AddSlot().AutoHeight().Padding(0.0f, RowIndex == 0 ? 0.0f : 2.0f, 0.0f, 0.0f)
		[
			BuildPlayerRow(Rows[RowIndex], TeamColor, RowIndex)
		];
	}

	return SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(HeroArenaScoreboard::Panel)
		.Padding(0.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(TeamColor.CopyWithNewOpacity(0.18f))
				.Padding(FMargin(16.0f, 12.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(TeamId == 1
							? NSLOCTEXT("HeroArenaScoreboard", "BlueTeam", "蓝队")
							: NSLOCTEXT("HeroArenaScoreboard", "RedTeam", "红队"))
						.ColorAndOpacity(TeamColor)
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 18))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SAssignNew(Score, STextBlock)
						.Text(FText::AsNumber(0))
						.ColorAndOpacity(FLinearColor::White)
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 26))
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(16.0f, 8.0f, 16.0f, 6.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f)[SNew(STextBlock).Text(NSLOCTEXT("HeroArenaScoreboard", "IdentityHeader", "玩家  /  英雄职责")).ColorAndOpacity(HeroArenaScoreboard::Muted).Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))]
				+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(232.0f)[SNew(STextBlock).Text(NSLOCTEXT("HeroArenaScoreboard", "StatsHeader", "击杀   死亡   助攻   占点   贡献   延迟")).Justification(ETextJustify::Right).ColorAndOpacity(HeroArenaScoreboard::Muted).Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))]]
			]
			+ SVerticalBox::Slot().FillHeight(1.0f).Padding(8.0f, 0.0f, 8.0f, 8.0f)
			[
				PlayerRows
			]
		];
}

TSharedRef<SWidget> UHeroArenaScoreboardWidget::BuildPlayerRow(
	FPlayerRow& Row, const FLinearColor& TeamColor, const int32 RowIndex)
{
	auto StatCell = [](TSharedPtr<STextBlock>& Text)
	{
		return SNew(SBox).WidthOverride(38.0f).HAlign(HAlign_Right)
		[
			SAssignNew(Text, STextBlock)
			.Text(FText::GetEmpty())
			.Justification(ETextJustify::Right)
			.ColorAndOpacity(FLinearColor(0.80f, 0.84f, 0.86f, 1.0f))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
		];
	};

	return SAssignNew(Row.Container, SBorder)
		.Visibility(EVisibility::Collapsed)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(RowIndex % 2 == 0 ? HeroArenaScoreboard::RowA : HeroArenaScoreboard::RowB)
		.Padding(FMargin(0.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(TeamColor).Padding(0.0f)
				[
					SNew(SBox).WidthOverride(4.0f)
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(11.0f, 8.0f)
			[
				SAssignNew(Row.Player, STextBlock).ColorAndOpacity(FLinearColor::White).Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(96.0f)[SAssignNew(Row.Hero, STextBlock).ColorAndOpacity(FLinearColor(0.82f, 0.86f, 0.88f, 1.0f)).Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(48.0f)[SAssignNew(Row.Role, STextBlock).ColorAndOpacity(HeroArenaScoreboard::Muted).Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[StatCell(Row.Eliminations)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[StatCell(Row.Deaths)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[StatCell(Row.Assists)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[StatCell(Row.Captures)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[StatCell(Row.Contribution)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[StatCell(Row.Ping)]
		];
}

void UHeroArenaScoreboardWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	const double Now = FPlatformTime::Seconds();
	if (Now >= NextRefreshTime)
	{
		NextRefreshTime = Now + 0.25;
		RefreshScoreboard();
	}
}

void UHeroArenaScoreboardWidget::RefreshScoreboard()
{
	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (!GameState)
	{
		return;
	}

	const FGameplayTag EliminationsTag = FGameplayTag::RequestGameplayTag(TEXT("ShooterGame.Score.Eliminations"));
	const FGameplayTag DeathsTag = FGameplayTag::RequestGameplayTag(TEXT("ShooterGame.Score.Deaths"));
	const FGameplayTag AssistsTag = FGameplayTag::RequestGameplayTag(TEXT("ShooterGame.Score.Assists"));
	const FGameplayTag CapturesTag = FGameplayTag::RequestGameplayTag(TEXT("ShooterGame.Score.ControlPointCapture"));
	const FGameplayTag ContributionTag = FGameplayTag::RequestGameplayTag(TEXT("ShooterGame.Score.RoleContribution"));
	RefreshModePresentation();
	const FGameplayTag TeamScoreTag = FGameplayTag::RequestGameplayTag(GetTeamScoreTagName());

	if (const ULyraTeamSubsystem* Teams = GetWorld()->GetSubsystem<ULyraTeamSubsystem>())
	{
		if (BlueScore.IsValid()) BlueScore->SetText(FText::AsNumber(Teams->GetTeamTagStackCount(1, TeamScoreTag)));
		if (RedScore.IsValid()) RedScore->SetText(FText::AsNumber(Teams->GetTeamTagStackCount(2, TeamScoreTag)));
	}

	TArray<const ALyraPlayerState*> PlayersByTeam[2];
	for (const APlayerState* PlayerState : GameState->PlayerArray)
	{
		if (const ALyraPlayerState* LyraPlayerState = Cast<ALyraPlayerState>(PlayerState))
		{
			const int32 TeamIndex = LyraPlayerState->GetTeamId() - 1;
			if (TeamIndex >= 0 && TeamIndex < 2)
			{
				PlayersByTeam[TeamIndex].Add(LyraPlayerState);
			}
		}
	}
	for (TArray<const ALyraPlayerState*>& TeamPlayers : PlayersByTeam)
	{
		TeamPlayers.Sort([&](const ALyraPlayerState& A, const ALyraPlayerState& B)
		{
			const int32 AContribution = A.GetStatTagStackCount(ContributionTag);
			const int32 BContribution = B.GetStatTagStackCount(ContributionTag);
			if (AContribution != BContribution)
			{
				return AContribution > BContribution;
			}

			const int32 ACaptures = A.GetStatTagStackCount(CapturesTag);
			const int32 BCaptures = B.GetStatTagStackCount(CapturesTag);
			if (ACaptures != BCaptures)
			{
				return ACaptures > BCaptures;
			}

			const int32 ACombatScore = A.GetStatTagStackCount(EliminationsTag) * 5 +
				A.GetStatTagStackCount(AssistsTag);
			const int32 BCombatScore = B.GetStatTagStackCount(EliminationsTag) * 5 +
				B.GetStatTagStackCount(AssistsTag);
			return ACombatScore > BCombatScore;
		});
	}

	APlayerState* LocalPlayerState = GetOwningPlayer() ? GetOwningPlayer()->PlayerState : nullptr;
	auto RefreshRows = [&](TArray<FPlayerRow>& Rows, const TArray<const ALyraPlayerState*>& TeamPlayers)
	{
		for (int32 Index = 0; Index < Rows.Num(); ++Index)
		{
			FPlayerRow& Row = Rows[Index];
			const ALyraPlayerState* PlayerState = TeamPlayers.IsValidIndex(Index) ? TeamPlayers[Index] : nullptr;
			if (Row.Container.IsValid())
			{
				Row.Container->SetVisibility(PlayerState ? EVisibility::Visible : EVisibility::Collapsed);
			}
			if (!PlayerState) continue;

			const UHeroArenaHeroSelectionComponent* Selection =
				PlayerState->FindComponentByClass<UHeroArenaHeroSelectionComponent>();
			const UHeroArenaHeroDefinition* Hero = Selection ? Selection->GetSelectedHero() : nullptr;
			const FString Prefix = PlayerState == LocalPlayerState ? TEXT("你 · ") : FString();
			Row.Player->SetText(FText::FromString(Prefix + PlayerState->GetPlayerName()));
			Row.Hero->SetText(Hero ? Hero->DisplayName : NSLOCTEXT("HeroArenaScoreboard", "Selecting", "选择中"));
			Row.Role->SetText(Hero ? HeroArenaScoreboard::GetRoleName(Hero->Role) : FText::GetEmpty());
			Row.Role->SetColorAndOpacity(Hero ?
				(Hero->Role == EHeroArenaRole::Vanguard ? HeroArenaScoreboard::Vanguard :
				 Hero->Role == EHeroArenaRole::Support ? HeroArenaScoreboard::Support : HeroArenaScoreboard::Damage)
				: HeroArenaScoreboard::Muted);
			Row.Eliminations->SetText(FText::AsNumber(PlayerState->GetStatTagStackCount(EliminationsTag)));
			Row.Deaths->SetText(FText::AsNumber(PlayerState->GetStatTagStackCount(DeathsTag)));
			Row.Assists->SetText(FText::AsNumber(PlayerState->GetStatTagStackCount(AssistsTag)));
			Row.Captures->SetText(FText::AsNumber(PlayerState->GetStatTagStackCount(CapturesTag)));
			Row.Contribution->SetText(FText::AsNumber(PlayerState->GetStatTagStackCount(ContributionTag)));
			Row.Ping->SetText(FText::AsNumber(FMath::RoundToInt(PlayerState->GetPingInMilliseconds())));
		}
	};

	RefreshRows(BlueRows, PlayersByTeam[0]);
	RefreshRows(RedRows, PlayersByTeam[1]);
}

void UHeroArenaScoreboardWidget::ReleaseSlateResources(const bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	BlueRows.Reset();
	RedRows.Reset();
	ModeTitle.Reset();
	ModeSubtitle.Reset();
	BlueScore.Reset();
	RedScore.Reset();
}
