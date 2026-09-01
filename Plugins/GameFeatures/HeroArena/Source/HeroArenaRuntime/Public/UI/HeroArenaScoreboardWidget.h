#pragma once

#include "CommonActivatableWidget.h"

#include "HeroArenaScoreboardWidget.generated.h"

class SBorder;
class SHorizontalBox;
class STextBlock;

/** Hero Arena scoreboard that keeps Lyra's replicated scores and adds hero/role identity. */
UCLASS()
class HEROARENARUNTIME_API UHeroArenaScoreboardWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	UHeroArenaScoreboardWidget(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	struct FPlayerRow
	{
		TSharedPtr<SBorder> Container;
		TSharedPtr<STextBlock> Player;
		TSharedPtr<STextBlock> Hero;
		TSharedPtr<STextBlock> Role;
		TSharedPtr<STextBlock> Eliminations;
		TSharedPtr<STextBlock> Deaths;
		TSharedPtr<STextBlock> Assists;
		TSharedPtr<STextBlock> Captures;
		TSharedPtr<STextBlock> Contribution;
		TSharedPtr<STextBlock> Ping;
	};

	TSharedRef<SWidget> BuildTeamPanel(int32 TeamId, const FLinearColor& TeamColor);
	TSharedRef<SWidget> BuildPlayerRow(FPlayerRow& Row, const FLinearColor& TeamColor, int32 RowIndex);
	FName GetCurrentExperienceName() const;
	FName GetTeamScoreTagName() const;
	void RefreshModePresentation();
	void RefreshScoreboard();

	TArray<FPlayerRow> BlueRows;
	TArray<FPlayerRow> RedRows;
	TSharedPtr<STextBlock> ModeTitle;
	TSharedPtr<STextBlock> ModeSubtitle;
	TSharedPtr<STextBlock> BlueScore;
	TSharedPtr<STextBlock> RedScore;
	FName CachedExperienceName;
	double NextRefreshTime = 0.0;
};
