#include "PerformanceWorkbenchModule.h"
#include "PerformanceExperimentCatalog.h"
#include "PerformanceExperimentLauncher.h"
#include "PerformanceNetworkSessionLauncher.h"
#include "PerformanceResultAggregator.h"
#include "PerformanceTimelineView.h"

#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Interfaces/IPluginManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/DateTime.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "PerformanceWorkbench"

namespace PerformanceWorkbench
{
    const FName TabName(TEXT("PerformanceWorkbench"));

    FString FormatMetricNumber(double Value, const FString& Unit)
    {
        if (Unit == TEXT("ms")) return FString::Printf(TEXT("%.3f"), Value);
        if (Unit == TEXT("B/s") || Unit == TEXT("B/帧")) return FString::Printf(TEXT("%.0f"), Value);
        return FString::Printf(TEXT("%.1f"), Value);
    }

    FString FormatMetricValue(double Value, const FString& Unit)
    {
        return FString::Printf(TEXT("%s %s"), *FormatMetricNumber(Value, Unit), *Unit);
    }

    FString FormatMetricRangeValues(double MinValue, double MaxValue, const FString& Unit)
    {
        return FString::Printf(TEXT("%s–%s %s"),
            *FormatMetricNumber(MinValue, Unit), *FormatMetricNumber(MaxValue, Unit), *Unit);
    }

    FString FormatMetricRange(double MinValue, double MaxValue, const FString& Unit)
    {
        return TEXT("范围 ") + FormatMetricRangeValues(MinValue, MaxValue, Unit);
    }

    FSlateColor DeltaColor(const FPerformanceMetricComparisonView& Metric)
    {
        const double SignedBenefit = Metric.bLowerIsBetter ? -Metric.DeltaPercent : Metric.DeltaPercent;
        if (Metric.bSampleRangesOverlap) return FLinearColor(0.95f, 0.72f, 0.25f);
        if (SignedBenefit >= 2.0) return FLinearColor(0.20f, 0.82f, 0.46f);
        if (SignedBenefit <= -2.0) return FLinearColor(1.0f, 0.32f, 0.28f);
        return FLinearColor(0.95f, 0.72f, 0.25f);
    }

    FString DeltaAssessment(const FPerformanceMetricComparisonView& Metric)
    {
        const double SignedBenefit = Metric.bLowerIsBetter ? -Metric.DeltaPercent : Metric.DeltaPercent;
        if (SignedBenefit >= 2.0) return Metric.bSampleRangesOverlap ? TEXT("改善 · 范围重叠") : TEXT("改善");
        if (SignedBenefit <= -2.0) return Metric.bSampleRangesOverlap ? TEXT("回退 · 范围重叠") : TEXT("回退");
        return TEXT("基本持平");
    }

	FString BuildTrackResultSummary(const FPerformanceBatchComparisonView& Comparison)
	{
		const bool bFormal = Comparison.BaselineRunCount >= 3 && Comparison.CandidateRunCount >= 3;
		const FString SampleLabel = bFormal
			? (Comparison.bSingleBatch ? TEXT("正式 3+3") : TEXT("同条件 3+3"))
			: FString::Printf(TEXT("A/B %d+%d"), Comparison.BaselineRunCount, Comparison.CandidateRunCount);
		if (Comparison.Metrics.IsEmpty())
		{
			return SampleLabel;
		}

		const FPerformanceMetricComparisonView& Metric = Comparison.Metrics[0];
		return FString::Printf(TEXT("%s · %s %s → %s（%+.1f%%%s）"),
			*SampleLabel, *Metric.Label,
			*FormatMetricValue(Metric.BaselineValue, Metric.Unit),
			*FormatMetricValue(Metric.CandidateValue, Metric.Unit),
			Metric.DeltaPercent,
			Metric.bSampleRangesOverlap ? TEXT("，范围重叠") : TEXT(""));
	}

    TSharedRef<SWidget> BuildMetricSampleCell(double MedianValue, double MinValue, double MaxValue, const FString& Unit)
    {
        return SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(STextBlock).Text(FText::FromString(FormatMetricValue(MedianValue, Unit)))
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
            [
                SNew(STextBlock)
                .Text(FText::FromString(FormatMetricRange(MinValue, MaxValue, Unit)))
                .Font(FAppStyle::GetFontStyle("SmallText"))
                .ColorAndOpacity(FSlateColor::UseSubduedForeground())
            ];
    }

	FString EscapeMarkdownCell(FString Value)
	{
		Value.ReplaceInline(TEXT("|"), TEXT("\\|"));
		Value.ReplaceInline(TEXT("\r"), TEXT(" "));
		Value.ReplaceInline(TEXT("\n"), TEXT(" "));
		return Value;
	}

	FString BuildComparisonMarkdown(const FPerformanceBatchComparisonView& Comparison, const FString& GroupId)
	{
		FString Markdown = FString::Printf(
			TEXT("# %s\n\n")
			TEXT("- 采集批次：`%s`\n")
			TEXT("- 源码版本：`%s`\n")
			TEXT("- 采集引擎：`%s`\n")
			TEXT("- 样本数量：改动前 %d，调整后 %d\n")
			TEXT("- 对比状态：%s\n\n")
			TEXT("## 实验条件\n\n%s\n\n")
			TEXT("## 场景状态\n\n%s\n\n"),
			*Comparison.Title, *GroupId, *Comparison.SourceRevision,
			Comparison.EngineVersion.IsEmpty() ? TEXT("历史样本未记录") : *Comparison.EngineVersion,
			Comparison.BaselineRunCount, Comparison.CandidateRunCount,
			*Comparison.StatusSummary, *Comparison.ContractSummary, *Comparison.QualitySummary);

		if (Comparison.bComparable && !Comparison.Metrics.IsEmpty())
		{
			Markdown += TEXT("## 指标对比\n\n");
			Markdown += TEXT("| 指标 | 改动前中位数 | 改动前范围 | 调整后中位数 | 调整后范围 | 变化 | 结果 |\n");
			Markdown += TEXT("| --- | ---: | ---: | ---: | ---: | ---: | --- |\n");
			for (const FPerformanceMetricComparisonView& Metric : Comparison.Metrics)
			{
				Markdown += FString::Printf(TEXT("| %s | %s | %s | %s | %s | %+.2f%% | %s |\n"),
					*EscapeMarkdownCell(Metric.Label),
					*FormatMetricValue(Metric.BaselineValue, Metric.Unit),
					*FormatMetricRangeValues(Metric.BaselineMinValue, Metric.BaselineMaxValue, Metric.Unit),
					*FormatMetricValue(Metric.CandidateValue, Metric.Unit),
					*FormatMetricRangeValues(Metric.CandidateMinValue, Metric.CandidateMaxValue, Metric.Unit),
					Metric.DeltaPercent, *DeltaAssessment(Metric));
			}
		}
		else if (!Comparison.Problems.IsEmpty())
		{
			Markdown += TEXT("## 当前缺少的条件\n\n");
			for (const FString& Problem : Comparison.Problems)
			{
				Markdown += FString::Printf(TEXT("- %s\n"), *Problem);
			}
		}
		return Markdown;
	}

    FLinearColor DimensionColor(const FString& Dimension)
    {
        if (Dimension == TEXT("Network")) return FLinearColor(0.30f, 0.67f, 0.92f);
        if (Dimension == TEXT("AIDecision")) return FLinearColor(0.95f, 0.59f, 0.22f);
        if (Dimension == TEXT("Navigation")) return FLinearColor(0.46f, 0.78f, 0.47f);
        if (Dimension == TEXT("AnimationLOD") || Dimension == TEXT("HeroArenaAnimationLOD")) return FLinearColor(0.70f, 0.52f, 0.92f);
        if (Dimension == TEXT("RenderingLOD")) return FLinearColor(0.93f, 0.40f, 0.38f);
        if (Dimension == TEXT("Rendering")) return FLinearColor(0.96f, 0.48f, 0.33f);
        if (Dimension == TEXT("HeroArenaGameplayTick") || Dimension == TEXT("GameplayTick")) return FLinearColor(0.91f, 0.68f, 0.24f);
		if (Dimension == TEXT("HeroArenaHUD") || Dimension == TEXT("UI")) return FLinearColor(0.32f, 0.78f, 0.74f);
        if (Dimension == TEXT("Physics")) return FLinearColor(0.24f, 0.76f, 0.82f);
        if (Dimension == TEXT("Niagara")) return FLinearColor(0.88f, 0.40f, 0.72f);
        if (Dimension == TEXT("HeroArenaWeaponVFX") || Dimension == TEXT("WeaponVFX")
            || Dimension == TEXT("HeroArenaLiveWeaponVFX") || Dimension == TEXT("LiveWeaponVFX")) return FLinearColor(0.95f, 0.59f, 0.22f);
		if (Dimension == TEXT("HeroArenaGunPadVFX") || Dimension == TEXT("GunPadVFX")) return FLinearColor(0.32f, 0.78f, 0.74f);
        return FLinearColor(0.88f, 0.72f, 0.26f);
    }

    FString ProcessRoleLabel(const FString& Role)
    {
        if (Role == TEXT("Server")) return TEXT("服务器");
        if (Role.StartsWith(TEXT("Client"))) return TEXT("客户端 ") + Role.RightChop(6);
        if (Role == TEXT("Standalone")) return TEXT("单机进程");
        return Role;
    }

    FString ExperimentDimensionLabel(const FString& Dimension)
    {
        if (Dimension == TEXT("Rendering")) return TEXT("角色附加通道 LOD");
        if (Dimension == TEXT("Network")) return TEXT("多人网络");
        if (Dimension == TEXT("AIDecision")) return TEXT("AI 状态机与目标发现");
        if (Dimension == TEXT("Navigation")) return TEXT("寻路重规划");
        if (Dimension == TEXT("AnimationLOD") || Dimension == TEXT("HeroArenaAnimationLOD")) return TEXT("动画 LOD");
		if (Dimension == TEXT("RenderingLOD")) return TEXT("角色光追场景 LOD");
        if (Dimension == TEXT("Physics")) return TEXT("物理 LOD");
        if (Dimension == TEXT("Niagara")) return TEXT("Niagara 生命周期");
        if (Dimension == TEXT("HeroArenaWeaponVFX") || Dimension == TEXT("WeaponVFX")) return TEXT("武器表现预算");
        if (Dimension == TEXT("HeroArenaLiveWeaponVFX") || Dimension == TEXT("LiveWeaponVFX")) return TEXT("真实对局武器表现");
		if (Dimension == TEXT("HeroArenaGunPadVFX") || Dimension == TEXT("GunPadVFX")) return TEXT("拾取点常驻特效");
        if (Dimension == TEXT("HeroArenaGameplayTick")) return TEXT("拾取点更新");
		if (Dimension == TEXT("HeroArenaHUD") || Dimension == TEXT("UI")) return TEXT("战术 HUD 更新");
        return Dimension;
    }

    FString ExperimentVariantLabel(const FString& Variant)
    {
        return Variant == TEXT("Baseline") ? TEXT("改动前")
            : Variant == TEXT("Optimized") ? TEXT("调整后") : Variant;
    }

    FString ExperimentSubjectLabel(const FString& PresetId)
    {
        if (PresetId.StartsWith(TEXT("crowd-200"))) return TEXT("多角色负载");
        if (PresetId.StartsWith(TEXT("hero-arena"))) return TEXT("英雄场景");
        return PresetId.IsEmpty() ? TEXT("未命名实验") : PresetId;
    }

	FString ExperimentCatalogLabel(const FString& LaunchDimension)
	{
		if (const FPerformanceExperimentDefinition* Definition =
			PerformanceExperimentCatalog::FindByLaunchDimension(LaunchDimension))
		{
			return FString::Printf(TEXT("%02d · %s"), Definition->TrackNumber, *Definition->Title);
		}
		return LaunchDimension;
	}

	TSharedRef<SWidget> BuildExperimentSectionHeader(const FText& Title, const FText& Detail, const FLinearColor& Accent)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 2, 12, 2)
			[
				SNew(SBox).WidthOverride(4.0f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
					.BorderBackgroundColor(Accent)
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text(Title).Font(FAppStyle::GetFontStyle("HeadingSmall"))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)
				[
					SNew(STextBlock).Text(Detail).ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
			];
	}

    FString TimelineGroupLabel(const FString& GroupId)
    {
        FString Subject = TEXT("实验记录");
        if (GroupId.Contains(TEXT("spawner"), ESearchCase::IgnoreCase)) Subject = TEXT("Hero Arena · 拾取点更新");
		else if (GroupId.Contains(TEXT("hud-refresh"), ESearchCase::IgnoreCase)) Subject = TEXT("Hero Arena · 战术 HUD 更新");
        else if (GroupId.Contains(TEXT("5v5-animation"), ESearchCase::IgnoreCase)) Subject = TEXT("Hero Arena · 动画 LOD");
        else if (GroupId.Contains(TEXT("customdepth"), ESearchCase::IgnoreCase)) Subject = TEXT("Hero Arena · 角色附加通道 LOD");
        else if (GroupId.Contains(TEXT("gpu-shadow-pass"), ESearchCase::IgnoreCase)) Subject = TEXT("Hero Arena · GPU 阶段诊断");
        else if (GroupId.Contains(TEXT("shadow"), ESearchCase::IgnoreCase) || GroupId.Contains(TEXT("character-rendering"), ESearchCase::IgnoreCase)) Subject = TEXT("Hero Arena · 角色阴影");
        else if (GroupId.Contains(TEXT("network"), ESearchCase::IgnoreCase)) Subject = TEXT("Hero Arena · 多人网络");
        else if (GroupId.Contains(TEXT("physics"), ESearchCase::IgnoreCase)) Subject = TEXT("受控负载 · 物理 LOD");
        else if (GroupId.Contains(TEXT("live-weapon-vfx"), ESearchCase::IgnoreCase)) Subject = TEXT("Hero Arena · 真实对局武器表现");
        else if (GroupId.Contains(TEXT("weapon-vfx"), ESearchCase::IgnoreCase)) Subject = TEXT("受控负载 · 武器表现预算");
        else if (GroupId.Contains(TEXT("niagara"), ESearchCase::IgnoreCase)) Subject = TEXT("受控负载 · Niagara 生命周期");
        else if (GroupId.Contains(TEXT("navigation"), ESearchCase::IgnoreCase) || GroupId.Contains(TEXT("nav-"), ESearchCase::IgnoreCase)) Subject = TEXT("多角色 · 寻路重规划");
		else if (GroupId.Contains(TEXT("render"), ESearchCase::IgnoreCase)) Subject = TEXT("多角色 · 角色光追场景 LOD");
        else if (GroupId.Contains(TEXT("animation"), ESearchCase::IgnoreCase)) Subject = TEXT("多角色 · 动画 LOD");
        else if (GroupId.Contains(TEXT("crowd-state"), ESearchCase::IgnoreCase) || GroupId.Contains(TEXT("ai-"), ESearchCase::IgnoreCase)) Subject = TEXT("多角色 · AI 状态机与目标发现");

        const FString Stage = GroupId.Contains(TEXT("formal"), ESearchCase::IgnoreCase) ? TEXT("正式 3+3")
            : GroupId.Contains(TEXT("short"), ESearchCase::IgnoreCase) ? TEXT("短测")
            : GroupId.Contains(TEXT("smoke"), ESearchCase::IgnoreCase) ? TEXT("链路短测") : TEXT("采集");
        TArray<FString> Parts;
        GroupId.ParseIntoArray(Parts, TEXT("-"), true);
        FString Suffix = Parts.IsEmpty() ? FString() : Parts.Last();
        if (Suffix.Len() > 8)
        {
            Suffix = Suffix.Right(8);
        }
        return Suffix.IsEmpty() ? FString::Printf(TEXT("%s · %s"), *Subject, *Stage)
            : FString::Printf(TEXT("%s · %s · %s"), *Subject, *Stage, *Suffix);
    }

    TSharedRef<SWidget> BuildComparisonPanel(const FPerformanceBatchComparisonView& Comparison)
    {
        TSharedRef<SVerticalBox> Content = SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(Comparison.Title))
                    .Font(FAppStyle::GetFontStyle("HeadingMedium"))
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(Comparison.bComparable
                        ? (Comparison.bSingleBatch ? TEXT("同批次") : TEXT("同条件跨批次"))
                        : TEXT("待补样本")))
                    .ColorAndOpacity(Comparison.bComparable
                        ? FSlateColor(FLinearColor(0.20f, 0.82f, 0.46f))
                        : FSlateColor(FLinearColor(0.95f, 0.72f, 0.25f)))
                    .Font(FAppStyle::GetFontStyle("HeadingSmall"))
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
            [
                SNew(STextBlock)
                .Text(FText::FromString(Comparison.StatusSummary))
                .ColorAndOpacity(FSlateColor::UseSubduedForeground())
                .AutoWrapText(true)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
            [
                SNew(STextBlock)
                .Text(FText::FromString(Comparison.ContractSummary))
                .AutoWrapText(true)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 10)
            [
                SNew(STextBlock)
                .Text(FText::FromString(Comparison.QualitySummary))
                .ColorAndOpacity(FSlateColor(Comparison.bAllGuardsPassing
                    ? FLinearColor(0.43f, 0.75f, 1.0f)
                    : FLinearColor(1.0f, 0.45f, 0.30f)))
                .AutoWrapText(true)
            ];

        if (Comparison.bComparable)
        {
            TSharedRef<SGridPanel> Grid = SNew(SGridPanel).FillColumn(0, 1.0f);
            const FSlateFontInfo HeaderFont = FAppStyle::GetFontStyle("SmallText");
            Grid->AddSlot(0, 0).Padding(4, 5)[SNew(STextBlock).Text(LOCTEXT("MetricHeader", "指标")).Font(HeaderFont).ColorAndOpacity(FSlateColor::UseSubduedForeground())];
            Grid->AddSlot(1, 0).Padding(12, 5)[SNew(STextBlock).Text(LOCTEXT("BaselineHeader", "改动前中位数")).Font(HeaderFont).ColorAndOpacity(FSlateColor::UseSubduedForeground())];
            Grid->AddSlot(2, 0).Padding(12, 5)[SNew(STextBlock).Text(LOCTEXT("CandidateHeader", "调整后中位数")).Font(HeaderFont).ColorAndOpacity(FSlateColor::UseSubduedForeground())];
            Grid->AddSlot(3, 0).Padding(12, 5)[SNew(STextBlock).Text(LOCTEXT("DeltaHeader", "变化")).Font(HeaderFont).ColorAndOpacity(FSlateColor::UseSubduedForeground())];
            Grid->AddSlot(4, 0).Padding(12, 5)[SNew(STextBlock).Text(LOCTEXT("AssessmentHeader", "判断")).Font(HeaderFont).ColorAndOpacity(FSlateColor::UseSubduedForeground())];

            int32 Row = 1;
            for (const FPerformanceMetricComparisonView& Metric : Comparison.Metrics)
            {
                const FSlateColor Color = DeltaColor(Metric);
                Grid->AddSlot(0, Row).Padding(4, 6)[SNew(STextBlock).Text(FText::FromString(Metric.Label))];
                Grid->AddSlot(1, Row).Padding(12, 6)[BuildMetricSampleCell(
                    Metric.BaselineValue, Metric.BaselineMinValue, Metric.BaselineMaxValue, Metric.Unit)];
                Grid->AddSlot(2, Row).Padding(12, 6)[BuildMetricSampleCell(
                    Metric.CandidateValue, Metric.CandidateMinValue, Metric.CandidateMaxValue, Metric.Unit)];
                Grid->AddSlot(3, Row).Padding(12, 6)[SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%+.2f%%"), Metric.DeltaPercent))).ColorAndOpacity(Color)];
                Grid->AddSlot(4, Row).Padding(12, 6)[SNew(STextBlock).Text(FText::FromString(DeltaAssessment(Metric))).ColorAndOpacity(Color)];
                ++Row;
            }
            Content->AddSlot().AutoHeight()[Grid];
        }
        else
        {
            for (const FString& Problem : Comparison.Problems)
            {
                Content->AddSlot().AutoHeight().Padding(0, 2)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("• ") + Problem))
                    .ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.45f, 0.30f)))
                    .AutoWrapText(true)
                ];
            }
        }

        return SNew(SBorder)
            .Padding(0)
            .BorderImage(FAppStyle::GetBrush(Comparison.bComparable ? "Brushes.Header" : "Brushes.Recessed"))
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SBox).WidthOverride(5.0f)
                    [
                        SNew(SBorder)
                        .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
                        .BorderBackgroundColor(DimensionColor(Comparison.Dimension))
                    ]
                ]
                + SHorizontalBox::Slot().FillWidth(1.0f).Padding(16)
                [
                    Content
                ]
            ];
    }
}

struct FPerformanceWorkbenchNetworkLaunchState
{
    TOptional<TFuture<FPerformanceNetworkSessionLaunchResult>> Future;
    TSharedPtr<TAtomic<bool>, ESPMode::ThreadSafe> CancelRequested;
    bool bFormalBatch = false;
};

struct FPerformanceWorkbenchFormalBatchState
{
    FString Dimension;
    FString BatchId;
    FString SourceRevision;
    TArray<FString> Variants;
    int32 NextRunIndex = 0;
    int32 InvalidRunCount = 0;
    uint32 ProcessId = 0;
    FString CurrentRunId;
    double ProcessDeadlineSeconds = 0.0;
    double ResultDeadlineSeconds = 0.0;
    double WarmupOverride = -1.0;
    double DurationOverride = -1.0;
    FString CurrentNetworkSessionId;
    FString LastCompletedGroupId;
    int32 NetworkClientCount = 2;
    bool bCancelRequested = false;
    bool bExitWhenComplete = false;
};

struct FPerformanceWorkbenchBatchControllerState
{
    FString Dimension;
    FString LogPath;
    FProcHandle ProcessHandle;
    uint32 ProcessId = 0;
    double DeadlineSeconds = 0.0;
    bool bRunAfterHostExit = true;
};

struct FPerformanceWorkbenchPendingControllerBatchState
{
    FString Dimension;
    double WarmupOverride = -1.0;
    double DurationOverride = -1.0;
    int32 RunCount = 6;
    bool bExitWhenComplete = true;
    FString VariantOverride;
    uint32 WaitForProcessId = 0;
    double WaitDeadlineSeconds = 0.0;
};

struct FPerformanceWorkbenchComparisonCache
{
	TArray<FPerformanceBatchComparisonView> Comparisons;
};

void FPerformanceWorkbenchModule::StartupModule()
{
    NetworkLaunchState = MakeUnique<FPerformanceWorkbenchNetworkLaunchState>();
	ComparisonCache = MakeUnique<FPerformanceWorkbenchComparisonCache>();
    NetworkLaunchCommand = IConsoleManager::Get().RegisterConsoleCommand(
        TEXT("PerfWorkbench.LaunchNetwork"),
        TEXT("启动后台网络实验。参数：Variant ClientCount Port [FixtureCount Warmup Duration]。"),
        FConsoleCommandWithArgsDelegate::CreateRaw(this, &FPerformanceWorkbenchModule::LaunchNetworkSession),
        ECVF_Default);
    FormalBatchCommand = IConsoleManager::Get().RegisterConsoleCommand(
        TEXT("PerfWorkbench.LaunchFormal"),
        TEXT("启动隔离的 A/B 采集。参数：Dimension [Warmup Duration RunCount Exit Variant]。"),
        FConsoleCommandWithArgsDelegate::CreateRaw(this, &FPerformanceWorkbenchModule::LaunchFormalBatchCommand),
        ECVF_Default);
	OpenWorkbenchCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("PerfWorkbench.Open"),
		TEXT("打开性能工作台。参数：实验方向 [采集批次 进程角色] [exit]；exit 用于后台验证完成后直接退出。"),
		FConsoleCommandWithArgsDelegate::CreateLambda([this](const TArray<FString>& Arguments)
		{
			TArray<FString> OpenArguments = Arguments;
			const bool bExitAfterOpen = OpenArguments.RemoveAll([](const FString& Argument)
			{
				return Argument.Equals(TEXT("exit"), ESearchCase::IgnoreCase);
			}) > 0;
			if (OpenArguments.Num() >= 2)
			{
				SelectResultGroup(OpenArguments[0], OpenArguments[1],
					OpenArguments.IsValidIndex(2) ? OpenArguments[2] : FString());
			}
			else if (!OpenArguments.IsEmpty())
			{
				SelectLatestResult(OpenArguments[0]);
			}
			WorkbenchTab = FGlobalTabmanager::Get()->TryInvokeTab(PerformanceWorkbench::TabName);
			const FString RunsRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PerformanceLab"), TEXT("Runs"));
			const FPerformanceTimelineView TimelineView = SelectedTimelineGroup.IsValid()
				? PerformanceWorkbenchTimeline::LoadGroup(RunsRoot, *SelectedTimelineGroup) : FPerformanceTimelineView();
			const FPerformanceExperimentDefinition* SelectedDefinition = SelectedTimelineExperiment.IsValid()
				? PerformanceExperimentCatalog::FindByLaunchDimension(*SelectedTimelineExperiment) : nullptr;
			TOptional<FPerformanceBatchComparisonView> SelectedComparison;
			if (SelectedDefinition && SelectedTimelineGroup.IsValid())
			{
				SelectedComparison = FPerformanceResultAggregator::LoadComparisonForGroup(
					RunsRoot, SelectedDefinition->PresetId, SelectedDefinition->ResultDimension, *SelectedTimelineGroup);
			}
			UE_LOG(LogTemp, Display, TEXT("[PerformanceWorkbench] 工作台已打开 · 实验=%s · 批次=%s · 结果=%s · %s · 热点=%d · 首项=%s · Effects来源=%d · 首个系统=%s"),
				SelectedTimelineExperiment.IsValid() ? **SelectedTimelineExperiment : TEXT("无"),
				SelectedTimelineGroup.IsValid() ? **SelectedTimelineGroup : TEXT("无"),
				SelectedComparison.IsSet() ? *SelectedComparison->Title : TEXT("无"),
				SelectedComparison.IsSet() ? *SelectedComparison->StatusSummary : TEXT("尚无对比"),
				TimelineView.Hotspots.Num(),
				TimelineView.Hotspots.IsEmpty() ? TEXT("无") : *TimelineView.Hotspots[0].Label,
				TimelineView.EffectSystems.Num(),
				TimelineView.EffectSystems.IsEmpty() ? TEXT("无") : *TimelineView.EffectSystems[0].Label);
			if (bExitAfterOpen)
			{
				FPlatformMisc::RequestExit(false);
			}
		}),
		ECVF_Default);
	ExportComparisonConsoleCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("PerfWorkbench.Export"),
		TEXT("保存当前性能对比报告。可选参数：实验方向 采集批次 [进程角色]。"),
		FConsoleCommandWithArgsDelegate::CreateRaw(this, &FPerformanceWorkbenchModule::ExportComparisonCommand),
		ECVF_Default);
    CaptureUICommand = IConsoleManager::Get().RegisterConsoleCommand(
        TEXT("PerfWorkbench.CaptureUI"),
        TEXT("打开性能工作台并保存 Slate 截图。第一个参数选择视图：gputimeline、aitimeline、animationtimeline、network、networktimeline、networkclienttimeline、navigationtimeline、niagaratimeline；追加 exit 可在保存后退出。"),
        FConsoleCommandWithArgsDelegate::CreateRaw(this, &FPerformanceWorkbenchModule::CaptureWorkbenchUI),
        ECVF_Default);
    NetworkLaunchTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateRaw(this, &FPerformanceWorkbenchModule::PollNetworkLaunch),
        0.25f);

    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        PerformanceWorkbench::TabName,
        FOnSpawnTab::CreateRaw(this, &FPerformanceWorkbenchModule::SpawnWorkbenchTab))
        .SetDisplayName(LOCTEXT("TabTitle", "性能工作台"))
        .SetTooltipText(LOCTEXT("TabTooltip", "运行并比较固定条件的 Lyra 性能实验"))
        .SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Profiler.Tab"));

    UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FPerformanceWorkbenchModule::RegisterMenus));
}

void FPerformanceWorkbenchModule::ShutdownModule()
{
    if (BatchControllerState && BatchControllerState->bRunAfterHostExit)
    {
        if (BatchControllerState->ProcessHandle.IsValid())
        {
            FPlatformProcess::CloseProc(BatchControllerState->ProcessHandle);
        }
        BatchControllerState.Reset();
    }
    else
    {
        CancelFormalBatch();
    }
    PerformanceWorkbenchTimeline::InvalidateIndex();
    if (NetworkLaunchState && NetworkLaunchState->Future.IsSet())
    {
        if (NetworkLaunchState->CancelRequested.IsValid())
        {
            NetworkLaunchState->CancelRequested->Store(true);
        }
        NetworkLaunchState->Future->Get();
        NetworkLaunchState->Future.Reset();
        NetworkLaunchState->CancelRequested.Reset();
    }
    if (NetworkLaunchTickerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(NetworkLaunchTickerHandle);
        NetworkLaunchTickerHandle.Reset();
    }
    if (NetworkLaunchCommand)
    {
        IConsoleManager::Get().UnregisterConsoleObject(NetworkLaunchCommand);
        NetworkLaunchCommand = nullptr;
    }
    if (CaptureUICommand)
    {
        IConsoleManager::Get().UnregisterConsoleObject(CaptureUICommand);
        CaptureUICommand = nullptr;
    }
	if (OpenWorkbenchCommand)
	{
		IConsoleManager::Get().UnregisterConsoleObject(OpenWorkbenchCommand);
		OpenWorkbenchCommand = nullptr;
	}
	if (ExportComparisonConsoleCommand)
	{
		IConsoleManager::Get().UnregisterConsoleObject(ExportComparisonConsoleCommand);
		ExportComparisonConsoleCommand = nullptr;
	}
    if (FormalBatchCommand)
    {
        IConsoleManager::Get().UnregisterConsoleObject(FormalBatchCommand);
        FormalBatchCommand = nullptr;
    }
    NetworkLaunchState.Reset();
    UToolMenus::UnRegisterStartupCallback(this);
    UToolMenus::UnregisterOwner(this);
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(PerformanceWorkbench::TabName);
}

void FPerformanceWorkbenchModule::CaptureWorkbenchUI(const TArray<FString>& Arguments)
{
    WorkbenchTab = FGlobalTabmanager::Get()->TryInvokeTab(PerformanceWorkbench::TabName);
    bExitAfterScreenshot = Arguments.ContainsByPredicate([](const FString& Argument)
    {
        return Argument.Equals(TEXT("exit"), ESearchCase::IgnoreCase);
    });
    const bool bNetworkView = Arguments.IsValidIndex(0) && Arguments[0].Equals(TEXT("network"), ESearchCase::IgnoreCase);
    const bool bNetworkTimeline = Arguments.IsValidIndex(0) && Arguments[0].Equals(TEXT("networktimeline"), ESearchCase::IgnoreCase);
    const bool bNetworkClientTimeline = Arguments.IsValidIndex(0) && Arguments[0].Equals(TEXT("networkclienttimeline"), ESearchCase::IgnoreCase);
    const bool bNavigationTimeline = Arguments.IsValidIndex(0) && Arguments[0].Equals(TEXT("navigationtimeline"), ESearchCase::IgnoreCase);
    const bool bNiagaraTimeline = Arguments.IsValidIndex(0) && Arguments[0].Equals(TEXT("niagaratimeline"), ESearchCase::IgnoreCase);
    const bool bGpuTimeline = Arguments.IsValidIndex(0) && Arguments[0].Equals(TEXT("gputimeline"), ESearchCase::IgnoreCase);
    const bool bAITimeline = Arguments.IsValidIndex(0) && Arguments[0].Equals(TEXT("aitimeline"), ESearchCase::IgnoreCase);
    const bool bAnimationTimeline = Arguments.IsValidIndex(0) && Arguments[0].Equals(TEXT("animationtimeline"), ESearchCase::IgnoreCase);
	const bool bLaunchConsole = Arguments.IsValidIndex(0) && Arguments[0].Equals(TEXT("console"), ESearchCase::IgnoreCase);
	if (bNetworkTimeline || bNetworkClientTimeline) SelectLatestResult(TEXT("Network"));
	else if (bNavigationTimeline) SelectLatestResult(TEXT("Navigation"));
	else if (bNiagaraTimeline) SelectLatestResult(TEXT("Niagara"));
	else if (bGpuTimeline) SelectLatestResult(TEXT("Rendering"));
	else if (bAITimeline) SelectLatestResult(TEXT("AIDecision"));
	else if (bAnimationTimeline) SelectLatestResult(TEXT("AnimationLOD"));
    const FString TimelineToken = (bNetworkTimeline || bNetworkClientTimeline) ? TEXT("network")
        : bNavigationTimeline ? TEXT("nav-")
        : bNiagaraTimeline ? TEXT("niagara")
        : bGpuTimeline ? TEXT("character-customdepth-lod")
        : bAITimeline ? TEXT("crowd-200-2")
        : FString();
    if (!TimelineToken.IsEmpty())
    {
        TSharedPtr<FString> FallbackTimelineGroup;
        const FString RunsRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PerformanceLab"), TEXT("Runs"));
        for (const TSharedPtr<FString>& Option : TimelineGroupOptions)
        {
            if (Option.IsValid() && Option->Contains(TimelineToken, ESearchCase::IgnoreCase))
            {
                if (!FallbackTimelineGroup.IsValid())
                {
                    FallbackTimelineGroup = Option;
                }
                if (PerformanceWorkbenchTimeline::LoadGroup(RunsRoot, *Option).bHasComparison)
                {
                    SelectedTimelineGroup = Option;
                    break;
                }
            }
        }
        if (!SelectedTimelineGroup.IsValid() || !SelectedTimelineGroup->Contains(TimelineToken, ESearchCase::IgnoreCase))
        {
            SelectedTimelineGroup = FallbackTimelineGroup;
        }
        RefreshTimeline();
        if (bNetworkClientTimeline)
        {
            for (const TSharedPtr<FString>& Role : TimelineRoleOptions)
            {
                if (Role.IsValid() && *Role == TEXT("Client1"))
                {
                    SelectedTimelineRole = Role;
                    RefreshTimeline();
                    break;
                }
            }
        }
    }
    PendingScreenshotPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(
        FPaths::ProjectDir(), TEXT("Documentation"), TEXT("Images"), TEXT("PerformanceWorkbench"),
        bNetworkClientTimeline ? TEXT("性能工作台-网络客户端时间轨道.png")
            : bNetworkTimeline ? TEXT("性能工作台-网络时间轨道.png")
            : bNavigationTimeline ? TEXT("性能工作台-寻路重规划时间轨道.png")
            : bNiagaraTimeline ? TEXT("性能工作台-Niagara生命周期时间轨道.png")
            : bGpuTimeline ? TEXT("性能工作台-GPU分阶段时间轨道.png")
            : bAITimeline ? TEXT("性能工作台-多角色AI决策时间轨道.png")
            : bAnimationTimeline ? TEXT("性能工作台-多角色动画LOD时间轨道.png")
			: bLaunchConsole ? TEXT("性能工作台-采集控制台.png")
            : bNetworkView ? TEXT("性能工作台-网络AB对比.png") : TEXT("性能工作台-渲染AB对比.png")));
    if (MainScrollBox.IsValid())
    {
		MainScrollBox->SetScrollOffset(bLaunchConsole ? 5600.0f : bNetworkView ? 400.0f : 0.0f);
    }
    ScreenshotReadyTime = FPlatformTime::Seconds() + 2.0;
    UE_LOG(LogTemp, Display, TEXT("[PerformanceWorkbench] 已打开真实工作台，等待 Slate 布局稳定后截图：%s"), *PendingScreenshotPath);
}

void FPerformanceWorkbenchModule::LaunchNetworkSession(const TArray<FString>& Arguments)
{
    if (!NetworkLaunchState)
    {
        return;
    }
    if (IsAnyExperimentRunning())
    {
        LastLaunchStatus = TEXT("已有采集正在运行");
        return;
    }

    const FString Variant = Arguments.IsValidIndex(0) && !Arguments[0].IsEmpty() ? Arguments[0] : TEXT("Baseline");
    const int32 ClientCount = Arguments.IsValidIndex(1) ? FMath::Clamp(FCString::Atoi(*Arguments[1]), 1, 8) : 2;
    const int32 ServerPort = Arguments.IsValidIndex(2) ? FMath::Clamp(FCString::Atoi(*Arguments[2]), 1024, 65535) : 7777;
    const int32 FixtureCount = Arguments.IsValidIndex(3) ? FMath::Clamp(FCString::Atoi(*Arguments[3]), 1, 512) : 64;
    const double WarmupOverride = Arguments.IsValidIndex(4) ? FMath::Max(0.0, FCString::Atod(*Arguments[4])) : -1.0;
    const double DurationOverride = Arguments.IsValidIndex(5) ? FMath::Max(1.0, FCString::Atod(*Arguments[5])) : -1.0;
    FPerformanceNetworkSessionLaunchRequest Request = BuildNetworkSessionRequest(
        Variant, ClientCount, ServerPort, FixtureCount, WarmupOverride, DurationOverride);
    NetworkLaunchState->CancelRequested = MakeShared<TAtomic<bool>, ESPMode::ThreadSafe>(false);
    NetworkLaunchState->bFormalBatch = false;
    Request.CancelRequested = NetworkLaunchState->CancelRequested;

    LastLaunchStatus = FString::Printf(TEXT("正在启动网络实验：%s · %d 客户端 · 端口 %d"), *Request.Variant, Request.ClientCount, Request.ServerPort);
    UE_LOG(LogTemp, Display, TEXT("[PerformanceWorkbench] %s"), *LastLaunchStatus);
    NetworkLaunchState->Future.Emplace(FPerformanceNetworkSessionLauncher::LaunchAsync(MoveTemp(Request)));
}

FPerformanceNetworkSessionLaunchRequest FPerformanceWorkbenchModule::BuildNetworkSessionRequest(
    const FString& Variant, int32 ClientCount, int32 ServerPort, int32 FixtureCount,
    double WarmupOverride, double DurationOverride) const
{
    FPerformanceNetworkSessionLaunchRequest Request;
    Request.Variant = Variant;
    Request.ClientCount = FMath::Clamp(ClientCount, 1, 8);
    Request.ServerPort = FMath::Clamp(ServerPort, 1024, 65535);
    if (WarmupOverride >= 0.0)
    {
        Request.Preset.Capture.WarmupSeconds = WarmupOverride;
    }
    if (DurationOverride >= 0.0)
    {
        Request.Preset.Capture.DurationSeconds = DurationOverride;
        Request.ClientJoinGraceSeconds = 5.0;
    }
    Request.Preset.ExtraCommandLine = FString::Printf(
        TEXT("-PerfLabFixtureClass=/Script/HeroArenaRuntime.HeroArenaRelayBeacon -PerfLabFixtureCount=%d -PerfLabFixtureSpacing=4000"),
        FMath::Clamp(FixtureCount, 1, 512));
    if (Variant.Equals(TEXT("Baseline"), ESearchCase::IgnoreCase))
    {
        Request.Preset.ExtraCommandLine += TEXT(" -HeroArenaRelayAlwaysRelevant");
    }
    if (Variant.Equals(TEXT("RepGraphSmoke"), ESearchCase::IgnoreCase))
    {
        Request.Preset.ExtraCommandLine +=
            TEXT(" -ini:Game:[/Script/LyraGame.LyraReplicationGraphSettings]:bDisableReplicationGraph=False");
    }
    return Request;
}

bool FPerformanceWorkbenchModule::PollNetworkLaunch(float DeltaSeconds)
{
    PollFormalBatch(DeltaSeconds);
    UpdateNetworkBatchStatus();

    if (!PendingScreenshotPath.IsEmpty() && FPlatformTime::Seconds() >= ScreenshotReadyTime)
    {
        const TSharedPtr<SDockTab> Tab = WorkbenchTab.Pin();
        TArray<FColor> Pixels;
        FIntVector ImageSize = FIntVector::ZeroValue;
        bool bSaved = false;
        if (Tab.IsValid() && FSlateApplication::IsInitialized()
            && FSlateApplication::Get().TakeScreenshot(Tab->GetContent(), Pixels, ImageSize)
            && ImageSize.X > 0 && ImageSize.Y > 0 && Pixels.Num() == ImageSize.X * ImageSize.Y)
        {
            IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
            const TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
            if (ImageWrapper.IsValid() && ImageWrapper->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor),
                ImageSize.X, ImageSize.Y, ERGBFormat::BGRA, 8))
            {
                const TArray64<uint8> Compressed = ImageWrapper->GetCompressed();
                IFileManager::Get().MakeDirectory(*FPaths::GetPath(PendingScreenshotPath), true);
                if (TUniquePtr<FArchive> Writer(IFileManager::Get().CreateFileWriter(*PendingScreenshotPath)); Writer.IsValid())
                {
                    Writer->Serialize(const_cast<uint8*>(Compressed.GetData()), Compressed.Num());
                    Writer->Close();
                    bSaved = !Writer->IsError();
                }
            }
        }
        if (bSaved)
        {
            UE_LOG(LogTemp, Display, TEXT("[PerformanceWorkbench] Slate UI 截图已保存：%s (%dx%d)"),
                *PendingScreenshotPath, ImageSize.X, ImageSize.Y);
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("[PerformanceWorkbench] Slate UI 截图失败：%s (%dx%d)"),
                *PendingScreenshotPath, ImageSize.X, ImageSize.Y);
        }
        PendingScreenshotPath.Reset();
        if (bExitAfterScreenshot)
        {
            bExitAfterScreenshot = false;
            FPlatformMisc::RequestExit(false);
        }
    }

    if (!NetworkLaunchState || !NetworkLaunchState->Future.IsSet() || !NetworkLaunchState->Future->IsReady())
    {
        return true;
    }

    const FPerformanceNetworkSessionLaunchResult Result = NetworkLaunchState->Future->Get();
    const bool bFormalBatch = NetworkLaunchState->bFormalBatch;
    NetworkLaunchState->Future.Reset();
    NetworkLaunchState->CancelRequested.Reset();
    NetworkLaunchState->bFormalBatch = false;
    if (bFormalBatch && FormalBatchState && FormalBatchState->Dimension == TEXT("Network"))
    {
        if (Result.bCancelled || FormalBatchState->bCancelRequested)
        {
            FormalBatchState.Reset();
            LastLaunchStatus = TEXT("已停止正式 3+3：多人网络");
        }
        else
        {
            const bool bValid = Result.bStarted && Result.bCompleted && Result.bAllSummariesValid;
            FormalBatchState->LastCompletedGroupId = Result.SessionId;
            LastLaunchStatus = bValid
                ? FString::Printf(TEXT("网络第 %d/%d 次完成 · Server + %d Clients"),
                    FormalBatchState->NextRunIndex + 1, FormalBatchState->Variants.Num(),
                    FormalBatchState->NetworkClientCount)
                : FString::Printf(TEXT("网络第 %d/%d 次需要检查 · %s"),
                    FormalBatchState->NextRunIndex + 1, FormalBatchState->Variants.Num(),
                    Result.Error.IsEmpty() ? TEXT("存在无效结果") : *Result.Error);
            AdvanceFormalBatch(bValid);
        }
    }
    else if (Result.bCancelled)
    {
        LastLaunchStatus = TEXT("网络实验已停止");
    }
    else if (Result.bCompleted)
    {
        LastLaunchStatus = Result.bAllSummariesValid
            ? FString::Printf(TEXT("网络实验完成：%s · %d 个进程结果有效"), *Result.SessionId, Result.Processes.Num())
            : FString::Printf(TEXT("网络实验完成：%s · %s 结果需要检查"), *Result.SessionId,
                *FString::Join(Result.InvalidProcessRoles, TEXT("、")));
    }
    else
    {
        LastLaunchStatus = FString::Printf(TEXT("网络实验未完成：%s"), *Result.Error);
    }
    UE_LOG(LogTemp, Display, TEXT("[PerformanceWorkbench] %s"), *LastLaunchStatus);
    return true;
}

void FPerformanceWorkbenchModule::RegisterMenus()
{
    FToolMenuOwnerScoped Owner(this);
    UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
    FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("PerformanceTools"));
    Section.AddMenuEntry(
        TEXT("OpenPerformanceWorkbench"),
        LOCTEXT("MenuLabel", "性能工作台"),
        LOCTEXT("MenuTooltip", "打开 Lyra 性能实验、A/B 对比与回归工作区"),
        FSlateIcon(FAppStyle::GetAppStyleSetName(), "Profiler.Tab"),
        FUIAction(FExecuteAction::CreateLambda([]
        {
            FGlobalTabmanager::Get()->TryInvokeTab(PerformanceWorkbench::TabName);
        })));
}

TSharedRef<SDockTab> FPerformanceWorkbenchModule::SpawnWorkbenchTab(const FSpawnTabArgs& Args)
{
    TSharedRef<SDockTab> Tab = SNew(SDockTab).TabRole(ETabRole::NomadTab);
    WorkbenchTab = Tab;
    TSharedRef<SHorizontalBox> BatchSequenceStrip = SNew(SHorizontalBox);
    const TArray<FString> BatchVariants = {
        TEXT("Baseline"), TEXT("Optimized"), TEXT("Optimized"),
        TEXT("Baseline"), TEXT("Baseline"), TEXT("Optimized")
    };
    for (int32 Index = 0; Index < BatchVariants.Num(); ++Index)
    {
        const FString Variant = BatchVariants[Index];
        BatchSequenceStrip->AddSlot().AutoWidth().Padding(Index == 0 ? 0.0f : 3.0f, 0.0f)
        [
            SNew(SBox).WidthOverride(28.0f).HeightOverride(24.0f)
            [
                SNew(SBorder)
                .Padding(0.0f)
                .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
                .BorderBackgroundColor_Lambda([this, Index]
                {
                    if (!FormalBatchState)
                    {
                        return FSlateColor(FLinearColor(0.28f, 0.30f, 0.34f, 0.35f));
                    }
                    if (Index < FormalBatchState->NextRunIndex)
                    {
                        return FSlateColor(FLinearColor(0.20f, 0.62f, 0.38f, 0.45f));
                    }
                    if (Index == FormalBatchState->NextRunIndex)
                    {
                        return FSlateColor(PerformanceWorkbench::DimensionColor(FormalBatchState->Dimension).CopyWithNewOpacity(0.78f));
                    }
                    return FSlateColor(FLinearColor(0.28f, 0.30f, 0.34f, 0.35f));
                })
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(Variant == TEXT("Baseline") ? TEXT("前") : TEXT("后")))
                    .Justification(ETextJustify::Center)
                    .Font(FAppStyle::GetFontStyle("SmallText"))
                ]
            ]
        ];
    }

	TSharedRef<SVerticalBox> ExperimentCatalogBox = SNew(SVerticalBox);
	TOptional<EPerformanceExperimentSection> PreviousSection;
	for (const FPerformanceExperimentDefinition& Definition : PerformanceExperimentCatalog::All())
	{
		if (!PreviousSection.IsSet() || PreviousSection.GetValue() != Definition.Section)
		{
			const bool bHeroArena = Definition.Section == EPerformanceExperimentSection::HeroArena;
			const bool bControlledLoad = Definition.Section == EPerformanceExperimentSection::ControlledLoad;
			ExperimentCatalogBox->AddSlot().AutoHeight().Padding(0, PreviousSection.IsSet() ? 18.0f : 8.0f, 0, 8)
			[
				PerformanceWorkbench::BuildExperimentSectionHeader(
					bHeroArena ? LOCTEXT("HeroArenaSection", "Hero Arena 实战")
						: bControlledLoad ? LOCTEXT("ControlledLoadSection", "受控子系统负载")
						: LOCTEXT("CrowdSection", "200 角色压力场"),
					bHeroArena ? LOCTEXT("HeroArenaSectionDetail", "真实 5v5 对局、拾取点、角色表现与多进程网络")
						: bControlledLoad ? LOCTEXT("ControlledLoadSectionDetail", "固定事件与实例数量，单独比较物理和特效策略")
						: LOCTEXT("CrowdSectionDetail", "同一固定镜头与角色负载，分别观察 AI、寻路、动画/IK 和角色渲染"),
					bHeroArena ? FLinearColor(0.24f, 0.76f, 0.82f)
						: bControlledLoad ? FLinearColor(0.70f, 0.52f, 0.92f)
						: FLinearColor(0.91f, 0.68f, 0.24f))
			];
			PreviousSection = Definition.Section;
		}

		ExperimentCatalogBox->AddSlot().AutoHeight()
		[
			BuildExperimentRow(Definition.TrackNumber, FText::FromString(Definition.Title),
				FText::FromString(Definition.Detail), Definition.LaunchDimension)
		];
	}

    Tab->SetContent(
        SNew(SBorder)
        .Padding(20.0f)
        .BorderImage(FAppStyle::GetBrush("Brushes.Panel"))
        [
            SAssignNew(MainScrollBox, SScrollBox)
            + SScrollBox::Slot()
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Bottom)
                    [
                        SNew(STextBlock)
                        .Text(LOCTEXT("Heading", "性能工作台"))
                        .Font(FAppStyle::GetFontStyle("HeadingExtraLarge"))
                    ]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(16, 0, 0, 4)
                    [
                        SNew(STextBlock)
                        .Text(LOCTEXT("WorkflowScale", "场景负载  →  单一变量  →  运行结果"))
                        .ColorAndOpacity(FSlateColor::UseSubduedForeground())
                        .Font(FAppStyle::GetFontStyle("SmallText"))
                    ]
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 18)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("Contract", "把场景负载、帧时间与各系统指标放在同一组测量轨道中，对照改动前后的运行结果"))
                    .ColorAndOpacity(FSlateColor::UseSubduedForeground())
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
                [
                    SNew(SBorder)
                    .Padding(12)
                    .BorderImage(FAppStyle::GetBrush("Brushes.Header"))
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().Padding(0, 1, 10, 1)
                        [
                            SNew(SBox).WidthOverride(4.0f)
                            [
                                SNew(SBorder)
                                .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
                                .BorderBackgroundColor(FLinearColor(0.24f, 0.76f, 0.82f))
                            ]
                        ]
                        + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                        [
                            SNew(STextBlock)
                            .Text_Lambda([this] { return FText::FromString(LastLaunchStatus); })
                            .AutoWrapText(true)
                        ]
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(18, 0, 0, 0)
                        [
                            SNew(SHorizontalBox)
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
                            [
                                SNew(STextBlock)
                                .Text(LOCTEXT("BatchSequenceLabel", "采集顺序"))
                                .Font(FAppStyle::GetFontStyle("SmallText"))
                                .ColorAndOpacity(FSlateColor::UseSubduedForeground())
                            ]
                            + SHorizontalBox::Slot().AutoWidth()
                            [
                                BatchSequenceStrip
                            ]
                        ]
                    ]
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 8)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("TimelineHeading", "采集时间轨道"))
                    .Font(FAppStyle::GetFontStyle("HeadingLarge"))
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 14)
                [
                    SAssignNew(TimelineBox, SVerticalBox)
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 8)
                [
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("ComparisonHeading", "结果轨道"))
						.Font(FAppStyle::GetFontStyle("HeadingLarge"))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SButton)
						.Text(LOCTEXT("ExportComparison", "保存对比报告"))
						.ToolTipText(LOCTEXT("ExportComparisonTip", "将当前批次的实验条件、样本范围和指标保存为中文 Markdown。"))
						.IsEnabled_Lambda([this]
						{
							return SelectedTimelineExperiment.IsValid() && SelectedTimelineGroup.IsValid();
						})
						.OnClicked_Lambda([this]
						{
							ExportSelectedComparison();
							return FReply::Handled();
						})
					]
                ]
                + SVerticalBox::Slot().AutoHeight()
                [
                    SAssignNew(ComparisonBox, SVerticalBox)
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 16)
                [
                    SNew(SSeparator)
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("LaunchHeading", "采集控制台"))
                    .Font(FAppStyle::GetFontStyle("HeadingLarge"))
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("LaunchGuide", "单次采集用于快速判断方向；正式 3+3 按 ABBAAB 顺序连续运行。"))
                    .ColorAndOpacity(FSlateColor::UseSubduedForeground())
                ]
				+ SVerticalBox::Slot().AutoHeight()
				[
					ExperimentCatalogBox
				]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 18, 0, 8)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("Refresh", "刷新结果与对比"))
                        .OnClicked_Lambda([this]
                        {
                            RefreshRecentRuns();
                            return FReply::Handled();
                        })
                    ]
                    + SHorizontalBox::Slot().AutoWidth()
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("OpenResults", "打开结果目录"))
                        .OnClicked_Lambda([this]
                        {
                            OpenResultsDirectory();
                            return FReply::Handled();
                        })
                    ]
                ]
                + SVerticalBox::Slot().AutoHeight()
                [
                    SAssignNew(RecentRunsBox, SVerticalBox)
                ]
            ]
        ]);

    RefreshRecentRuns();
    return Tab;
}

TSharedRef<SWidget> FPerformanceWorkbenchModule::BuildExperimentRow(int32 TrackNumber, const FText& Title, const FText& Detail, const FString& Dimension)
{
    const bool bOptimizedAvailable = Dimension == TEXT("Network") || Dimension == TEXT("Rendering")
        || Dimension == TEXT("AIDecision") || Dimension == TEXT("AnimationLOD") || Dimension == TEXT("HeroArenaAnimationLOD") || Dimension == TEXT("RenderingLOD")
        || Dimension == TEXT("Physics") || Dimension == TEXT("Navigation") || Dimension == TEXT("Niagara") || Dimension == TEXT("HeroArenaWeaponVFX")
		|| Dimension == TEXT("HeroArenaLiveWeaponVFX")
		|| Dimension == TEXT("HeroArenaGunPadVFX")
		|| Dimension == TEXT("HeroArenaHUD");
    const bool bHasOptimizedVariant = bOptimizedAvailable || Dimension == TEXT("HeroArenaGameplayTick");
    const bool bSupportsFormalBatch = bHasOptimizedVariant;
    const FLinearColor TrackColor = PerformanceWorkbench::DimensionColor(Dimension);
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SBorder)
            .Padding(FMargin(0.0f, 7.0f))
            .BorderImage(FAppStyle::GetBrush("NoBorder"))
            [
                SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Fill).Padding(0, 0, 12, 0)
            [
                SNew(SBox).WidthOverride(2.0f)
                [
                    SNew(SBorder)
                    .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
                    .BorderBackgroundColor(TrackColor)
                ]
            ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)
            [
                SNew(SBox).WidthOverride(34.0f).HeightOverride(24.0f)
                [
                    SNew(SBorder)
                    .Padding(0.0f)
                    .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
                    .BorderBackgroundColor(TrackColor.CopyWithNewOpacity(0.20f))
                    [
                        SNew(STextBlock)
                        .Text(FText::AsNumber(TrackNumber))
                        .Justification(ETextJustify::Center)
                        .ColorAndOpacity(TrackColor)
                        .Font(FAppStyle::GetFontStyle("SmallText"))
                    ]
                ]
            ]
            + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(Title).Font(FAppStyle::GetFontStyle("NormalFontBold"))]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)[SNew(STextBlock).Text(Detail).ColorAndOpacity(FSlateColor::UseSubduedForeground()).Font(FAppStyle::GetFontStyle("SmallText"))]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
				[
					SNew(STextBlock)
					.Text_Lambda([this, Dimension]
					{
						if (const FString* Summary = TrackResultSummaries.Find(Dimension))
						{
							return FText::FromString(*Summary);
						}
						return LOCTEXT("LoadingTrackResult", "正在读取已有采集");
					})
					.AutoWrapText(true)
					.ColorAndOpacity_Lambda([this, Dimension, TrackColor]
					{
						return TracksWithComparableResults.Contains(Dimension)
							? FSlateColor(TrackColor)
							: FSlateColor::UseSubduedForeground();
					})
					.Font(FAppStyle::GetFontStyle("SmallText"))
				]
            ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10, 0, 5, 0)
            [
				SNew(SButton)
				.Text(LOCTEXT("ViewLatestResult", "查看结果"))
				.ToolTipText(LOCTEXT("ViewLatestResultTip", "优先定位最近一组同批次完整 3+3；没有正式批次时显示最近采集。"))
				.OnClicked_Lambda([this, Dimension]
				{
					SelectLatestResult(Dimension);
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
            [
                SNew(SButton)
                .Text(LOCTEXT("RunBaseline", "改动前"))
                .ToolTipText(LOCTEXT("BaselineReady", "安排一次改动前采集；关闭当前编辑器后，NullRHI 控制器会自动开始。"))
                .IsEnabled_Lambda([this] { return !IsAnyExperimentRunning(); })
                .OnClicked_Lambda([this, Dimension]
                {
                    StartFormalBatch(Dimension, -1.0, -1.0, 1, false, TEXT("Baseline"));
                    return FReply::Handled();
                })
            ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [
                SNew(SButton)
                .ButtonStyle(FAppStyle::Get(), "PrimaryButton")
                .Text(LOCTEXT("RunOptimized", "调整后"))
                .ToolTipText(bHasOptimizedVariant
                    ? LOCTEXT("OptimizedReady", "安排一次调整后采集；关闭当前编辑器后，NullRHI 控制器会自动开始。")
                    : LOCTEXT("OptimizedPending", "尚未选择可验证的调整方案；先运行单次采集查看当前热点。"))
                .IsEnabled_Lambda([this, bHasOptimizedVariant] { return bHasOptimizedVariant && !IsAnyExperimentRunning(); })
                .OnClicked_Lambda([this, Dimension]
                {
                    StartFormalBatch(Dimension, -1.0, -1.0, 1, false, TEXT("Optimized"));
                    return FReply::Handled();
                })
            ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)
            [
                SNew(SButton)
                .Text_Lambda([this, Dimension]
                {
                    if (BatchControllerState && BatchControllerState->Dimension == Dimension)
                    {
                        return LOCTEXT("StopBackgroundFormalBatch", "停止 · 后台");
                    }
                    if (FormalBatchState && FormalBatchState->Dimension == Dimension)
                    {
                        return FText::FromString(FString::Printf(TEXT("停止 · %d/6"),
                            FMath::Min(FormalBatchState->NextRunIndex + 1, 6)));
                    }
                    return LOCTEXT("RunFormalBatch", "安排正式 3+3");
                })
                .ToolTipText(bSupportsFormalBatch
                    ? LOCTEXT("FormalBatchReady", "安排 ABBAAB 六次采集；关闭当前编辑器后，NullRHI 控制器会自动开始。")
                    : LOCTEXT("FormalBatchPending", "该方向尚无可比较的调整后配置。"))
                .IsEnabled_Lambda([this, Dimension, bSupportsFormalBatch]
                {
                    const bool bThisBatch = (FormalBatchState && FormalBatchState->Dimension == Dimension)
                        || (BatchControllerState && BatchControllerState->Dimension == Dimension);
                    return bThisBatch || (bSupportsFormalBatch && !IsAnyExperimentRunning());
                })
                .OnClicked_Lambda([this, Dimension]
                {
                    if ((FormalBatchState && FormalBatchState->Dimension == Dimension)
                        || (BatchControllerState && BatchControllerState->Dimension == Dimension))
                    {
                        CancelFormalBatch();
                    }
                    else
                    {
                        StartFormalBatch(Dimension);
                    }
                    return FReply::Handled();
                })
            ]
            ]
        ]
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SBox).HeightOverride(1.0f)
            [
                SNew(SBorder)
                .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
                .BorderBackgroundColor(FLinearColor(0.32f, 0.34f, 0.38f, 0.28f))
            ]
        ];
}

void FPerformanceWorkbenchModule::SelectLatestResult(const FString& Dimension)
{
	const FPerformanceExperimentLaunchRequest Request = BuildExperimentRequest(Dimension, TEXT("Baseline"));
	const FString Root = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PerformanceLab"), TEXT("Runs"));
	PerformanceWorkbenchTimeline::InvalidateIndex();
	FString GroupId = PerformanceWorkbenchTimeline::FindLatestCompleteGroup(
		Root, Request.Preset.PresetId, Request.Dimension);
	const bool bCompleteComparison = !GroupId.IsEmpty();
	if (GroupId.IsEmpty())
	{
		GroupId = PerformanceWorkbenchTimeline::FindLatestGroup(Root, Request.Preset.PresetId, Request.Dimension);
	}
	if (GroupId.IsEmpty())
	{
		LastLaunchStatus = FString::Printf(TEXT("%s：尚无有效采集"), *PerformanceWorkbench::ExperimentDimensionLabel(Dimension));
		return;
	}

	SelectedTimelineExperiment = MakeShared<FString>(Dimension);
	SelectedTimelineGroup = MakeShared<FString>(GroupId);
	SelectedTimelineRole.Reset();
	LastLaunchStatus = FString::Printf(TEXT("已定位：%s · %s"),
		*PerformanceWorkbench::ExperimentDimensionLabel(Dimension),
		bCompleteComparison ? TEXT("最近完整 3+3") : TEXT("最近采集"));
	RefreshTimeline();
	if (MainScrollBox.IsValid())
	{
		MainScrollBox->SetScrollOffset(0.0f);
	}
}

void FPerformanceWorkbenchModule::SelectResultGroup(
	const FString& Dimension, const FString& GroupId, const FString& ProcessRole)
{
	if (GroupId.IsEmpty())
	{
		return;
	}
	PerformanceWorkbenchTimeline::InvalidateIndex();
	SelectedTimelineExperiment = MakeShared<FString>(Dimension);
	SelectedTimelineGroup = MakeShared<FString>(GroupId);
	SelectedTimelineRole = ProcessRole.IsEmpty() ? TSharedPtr<FString>() : MakeShared<FString>(ProcessRole);
	LastLaunchStatus = FString::Printf(TEXT("已定位：%s · %s"),
		*PerformanceWorkbench::ExperimentDimensionLabel(Dimension),
		*PerformanceWorkbench::TimelineGroupLabel(GroupId));
	RefreshTimeline();
	if (MainScrollBox.IsValid())
	{
		MainScrollBox->SetScrollOffset(0.0f);
	}
}

FPerformanceExperimentLaunchRequest FPerformanceWorkbenchModule::BuildExperimentRequest(const FString& Dimension, const FString& Variant) const
{
    FPerformanceExperimentLaunchRequest Request;
	const FPerformanceExperimentDefinition* Definition = PerformanceExperimentCatalog::FindByLaunchDimension(Dimension);
    const bool bHeroArenaAnimation = Dimension == TEXT("HeroArenaAnimationLOD");
    const bool bHeroArenaGameplayTick = Dimension == TEXT("HeroArenaGameplayTick");
    const bool bHeroArenaWeaponVFX = Dimension == TEXT("HeroArenaWeaponVFX");
	const bool bHeroArenaLiveWeaponVFX = Dimension == TEXT("HeroArenaLiveWeaponVFX");
	const bool bHeroArenaGunPadVFX = Dimension == TEXT("HeroArenaGunPadVFX");
	const bool bHeroArenaHUD = Dimension == TEXT("HeroArenaHUD");
    if (Dimension == TEXT("Physics")) Request.Preset = FPerformanceExperimentPresetData::MakeControlledPhysicsLoad();
    else if (Dimension == TEXT("Niagara") || bHeroArenaWeaponVFX || bHeroArenaLiveWeaponVFX || bHeroArenaGunPadVFX) Request.Preset = FPerformanceExperimentPresetData::MakeControlledNiagaraLoad();
    else if (Dimension == TEXT("Rendering") || bHeroArenaAnimation || bHeroArenaGameplayTick || bHeroArenaHUD) Request.Preset = FPerformanceExperimentPresetData::MakeHeroArenaRendering();
    else Request.Preset = FPerformanceExperimentPresetData::MakeCrowd200();
	if (Definition)
	{
		Request.Preset.PresetId = Definition->PresetId;
		Request.Dimension = Definition->ResultDimension;
	}
	else
	{
		Request.Dimension = Dimension;
	}
    Request.Variant = Variant;
    Request.bNullRHI = Dimension == TEXT("AIDecision") || Dimension == TEXT("Navigation");
    Request.bCaptureGpuPasses = !Request.bNullRHI;
    if (bHeroArenaWeaponVFX)
    {
        Request.Preset.Load.AgentCount = 0;
    }
    if (Dimension == TEXT("AIDecision") && Variant == TEXT("Optimized"))
    {
        Request.Preset.ExtraCommandLine = TEXT("-PerfLabCrowdStateMachine -PerfLabSharedBattleFacts");
    }
    else if (Dimension == TEXT("AIDecision"))
    {
        Request.Preset.ExtraCommandLine = TEXT("-PerfLabCrowdStateMachine");
    }
    else if (Dimension == TEXT("Navigation") && Variant == TEXT("Optimized"))
    {
		Request.Preset.ExtraCommandLine = TEXT("-PerfLabNavigationLoad -PerfLabNavigationPolicy");
    }
    else if (Dimension == TEXT("Navigation"))
    {
		Request.Preset.ExtraCommandLine = TEXT("-PerfLabNavigationLoad");
    }
    else if (bHeroArenaAnimation && Variant == TEXT("Optimized"))
    {
        Request.Preset.ExtraCommandLine = TEXT("-PerfLabAnimationLOD -LyraCharacterAnimationLOD");
    }
    else if (bHeroArenaAnimation)
    {
        Request.Preset.ExtraCommandLine = TEXT("-HeroArenaDisableAnimationLOD");
    }
    else if (bHeroArenaGameplayTick && Variant == TEXT("Baseline"))
    {
        Request.Preset.ExtraCommandLine = TEXT("-LyraWeaponSpawnerFullRateTick");
    }
	else if (bHeroArenaHUD && Variant == TEXT("Baseline"))
	{
		Request.Preset.ExtraCommandLine = TEXT("-HeroArenaHUDFullRate");
	}
    else if (Dimension == TEXT("AnimationLOD") && Variant == TEXT("Optimized"))
    {
		Request.Preset.ExtraCommandLine = TEXT("-PerfLabCrowdStateMachine -PerfLabNavigationLoad -PerfLabAnimationLOD -LyraCharacterAnimationLOD");
	}
	else if (Dimension == TEXT("AnimationLOD"))
	{
		Request.Preset.ExtraCommandLine = TEXT("-PerfLabCrowdStateMachine -PerfLabNavigationLoad");
    }
    else if (Dimension == TEXT("RenderingLOD") && Variant == TEXT("Optimized"))
    {
		Request.Preset.ExtraCommandLine = TEXT("-PerfLabCrowdStateMachine -PerfLabNavigationLoad -LyraCharacterRayTracingLOD");
	}
    else if (Dimension == TEXT("RenderingLOD"))
	{
		Request.Preset.ExtraCommandLine = TEXT("-PerfLabCrowdStateMachine -PerfLabNavigationLoad");
    }
    else if (Dimension == TEXT("Rendering") && Variant == TEXT("Optimized"))
    {
        Request.Preset.ExtraCommandLine = TEXT("-LyraCharacterCustomDepthLOD");
    }
    else if (Dimension == TEXT("Physics") && Variant == TEXT("Optimized"))
    {
        Request.Preset.ExtraCommandLine = TEXT("-PerfLabPhysicsLoad -PerfLabPhysicsBodies=300 -PerfLabPhysicsLOD -PerfLabPhysicsFullDistance=2400");
    }
    else if (Dimension == TEXT("Physics"))
    {
        Request.Preset.ExtraCommandLine = TEXT("-PerfLabPhysicsLoad -PerfLabPhysicsBodies=300");
    }
    else if (Dimension == TEXT("Niagara") && Variant == TEXT("Optimized"))
    {
        Request.Preset.ExtraCommandLine = TEXT("-PerfLabNiagaraCombatLoad -PerfLabNiagaraStatusSlots=24 -PerfLabNiagaraBurstsPerSecond=36 -PerfLabNiagaraPersistentStatusReuse -PerfLabDetailedFXStats");
    }
    else if (Dimension == TEXT("Niagara"))
    {
        Request.Preset.ExtraCommandLine = TEXT("-PerfLabNiagaraCombatLoad -PerfLabNiagaraStatusSlots=24 -PerfLabNiagaraBurstsPerSecond=36 -PerfLabDetailedFXStats");
    }
    else if (bHeroArenaWeaponVFX && Variant == TEXT("Optimized"))
    {
        Request.Preset.ExtraCommandLine = TEXT("-HeroArenaDisableBots -PerfLabWeaponVFXLoad -PerfLabNiagaraBurstsPerSecond=48 -PerfLabNiagaraFullDistance=2400 -PerfLabNiagaraDistanceBudget -PerfLabDetailedFXStats");
    }
    else if (bHeroArenaWeaponVFX)
    {
        Request.Preset.ExtraCommandLine = TEXT("-HeroArenaDisableBots -PerfLabWeaponVFXLoad -PerfLabNiagaraBurstsPerSecond=48 -PerfLabNiagaraFullDistance=2400 -PerfLabDetailedFXStats");
    }
	else if (bHeroArenaLiveWeaponVFX && Variant == TEXT("Baseline"))
	{
		Request.Preset.ExtraCommandLine = TEXT("-HeroArenaDisableWeaponVFXBudget -PerfLabDetailedFXStats");
	}
	else if (bHeroArenaLiveWeaponVFX)
	{
		Request.Preset.ExtraCommandLine = TEXT("-PerfLabDetailedFXStats");
	}
	else if (bHeroArenaGunPadVFX && Variant == TEXT("Baseline"))
	{
		Request.Preset.ExtraCommandLine = TEXT("-HeroArenaDisableWeaponPadVFXBudget -PerfLabDetailedFXStats");
	}
	else if (bHeroArenaGunPadVFX)
	{
		Request.Preset.ExtraCommandLine = TEXT("-PerfLabDetailedFXStats");
	}
    return Request;
}

bool FPerformanceWorkbenchModule::IsAnyExperimentRunning() const
{
    return FormalBatchState.IsValid() || BatchControllerState.IsValid() || PendingControllerBatchState.IsValid()
        || (NetworkLaunchState && NetworkLaunchState->Future.IsSet());
}

void FPerformanceWorkbenchModule::LaunchFormalBatchCommand(const TArray<FString>& Arguments)
{
    if (Arguments.IsEmpty())
    {
        LastLaunchStatus = TEXT("缺少实验方向，例如 HeroArenaWeaponVFX");
        return;
    }
    const double WarmupOverride = Arguments.IsValidIndex(1) ? FMath::Max(0.0, FCString::Atod(*Arguments[1])) : -1.0;
    const double DurationOverride = Arguments.IsValidIndex(2) ? FMath::Max(1.0, FCString::Atod(*Arguments[2])) : -1.0;
    const int32 RunCount = Arguments.IsValidIndex(3) ? FMath::Clamp(FCString::Atoi(*Arguments[3]), 1, 6) : 6;
    const bool bExitWhenComplete = Arguments.IsValidIndex(4)
        && Arguments[4].Equals(TEXT("Exit"), ESearchCase::IgnoreCase);
    const FString VariantOverride = Arguments.IsValidIndex(5) ? Arguments[5] : FString();
    StartFormalBatch(Arguments[0], WarmupOverride, DurationOverride, RunCount, bExitWhenComplete, VariantOverride);
}

void FPerformanceWorkbenchModule::StartFormalBatch(const FString& Dimension, double WarmupOverride, double DurationOverride,
    int32 RunCount, bool bExitWhenComplete, const FString& VariantOverride)
{
    const bool bSupportedDimension = Dimension == TEXT("Network") || Dimension == TEXT("AIDecision")
        || Dimension == TEXT("Navigation") || Dimension == TEXT("AnimationLOD") || Dimension == TEXT("RenderingLOD") || Dimension == TEXT("Rendering")
        || Dimension == TEXT("HeroArenaAnimationLOD") || Dimension == TEXT("Physics") || Dimension == TEXT("Niagara")
        || Dimension == TEXT("HeroArenaGameplayTick") || Dimension == TEXT("HeroArenaWeaponVFX")
		|| Dimension == TEXT("HeroArenaLiveWeaponVFX") || Dimension == TEXT("HeroArenaGunPadVFX")
		|| Dimension == TEXT("HeroArenaHUD");
    if (IsAnyExperimentRunning() || !bSupportedDimension)
    {
        LastLaunchStatus = IsAnyExperimentRunning() ? TEXT("已有采集正在运行") : TEXT("该实验方向不支持连续采集");
        return;
    }

    if (!FParse::Param(FCommandLine::Get(), TEXT("PerfWorkbenchController")))
    {
        LaunchFormalBatchController(Dimension, WarmupOverride, DurationOverride, RunCount, bExitWhenComplete, VariantOverride);
        return;
    }

    int32 WaitForProcessId = 0;
    if (FParse::Value(FCommandLine::Get(), TEXT("PerfWorkbenchWaitForPid="), WaitForProcessId)
        && WaitForProcessId > 0)
    {
        FProcHandle WaitHandle = FPlatformProcess::OpenProcess(static_cast<uint32>(WaitForProcessId));
        const bool bHostRunning = WaitHandle.IsValid() && FPlatformProcess::IsProcRunning(WaitHandle);
        if (WaitHandle.IsValid())
        {
            FPlatformProcess::CloseProc(WaitHandle);
        }
        if (bHostRunning)
        {
            PendingControllerBatchState = MakeUnique<FPerformanceWorkbenchPendingControllerBatchState>();
            PendingControllerBatchState->Dimension = Dimension;
            PendingControllerBatchState->WarmupOverride = WarmupOverride;
            PendingControllerBatchState->DurationOverride = DurationOverride;
            PendingControllerBatchState->RunCount = RunCount;
            PendingControllerBatchState->bExitWhenComplete = bExitWhenComplete;
            PendingControllerBatchState->VariantOverride = VariantOverride;
            PendingControllerBatchState->WaitForProcessId = static_cast<uint32>(WaitForProcessId);
            PendingControllerBatchState->WaitDeadlineSeconds = FPlatformTime::Seconds() + 1800.0;
            LastLaunchStatus = FString::Printf(TEXT("等待编辑器 PID %d 退出后开始：%s"), WaitForProcessId,
                *PerformanceWorkbench::ExperimentDimensionLabel(Dimension));
            UE_LOG(LogTemp, Display, TEXT("[PerformanceWorkbench] %s"), *LastLaunchStatus);
            return;
        }
    }

    FormalBatchState = MakeUnique<FPerformanceWorkbenchFormalBatchState>();
    FormalBatchState->Dimension = Dimension;
    FormalBatchState->SourceRevision = FPerformanceExperimentLauncher::ReadSourceRevision();
    const int32 ClampedRunCount = FMath::Clamp(RunCount, 1, 6);
    const FString PresetId = Dimension == TEXT("Network") ? TEXT("hero-arena-network")
        : BuildExperimentRequest(Dimension, TEXT("Baseline")).Preset.PresetId;
    FormalBatchState->BatchId = FString::Printf(TEXT("%s-%s-%s"),
        ClampedRunCount == 6 ? TEXT("formal") : ClampedRunCount == 1 ? TEXT("single") : TEXT("smoke-batch"), *PresetId,
        *FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S")));
    FormalBatchState->Variants = {
        TEXT("Baseline"), TEXT("Optimized"), TEXT("Optimized"),
        TEXT("Baseline"), TEXT("Baseline"), TEXT("Optimized")
    };
    FormalBatchState->Variants.SetNum(ClampedRunCount);
    if (ClampedRunCount == 1 && (VariantOverride == TEXT("Baseline") || VariantOverride == TEXT("Optimized")))
    {
        FormalBatchState->Variants[0] = VariantOverride;
    }
    FormalBatchState->WarmupOverride = WarmupOverride;
    FormalBatchState->DurationOverride = DurationOverride;
    FormalBatchState->bExitWhenComplete = bExitWhenComplete;
    LastLaunchStatus = FString::Printf(TEXT("准备连续采集：%s · %d 次"),
        *PerformanceWorkbench::ExperimentDimensionLabel(Dimension), FormalBatchState->Variants.Num());
    LaunchNextFormalBatchRun();
}

void FPerformanceWorkbenchModule::LaunchFormalBatchController(const FString& Dimension, double WarmupOverride,
    double DurationOverride, int32 RunCount, bool bExitCallingProcessWhenComplete, const FString& VariantOverride)
{
    double ResolvedWarmup = WarmupOverride;
    double ResolvedDuration = DurationOverride;
    if (Dimension == TEXT("Network"))
    {
        const FPerformanceNetworkSessionLaunchRequest Request = BuildNetworkSessionRequest(TEXT("Baseline"), 2, 17770, 64);
        ResolvedWarmup = ResolvedWarmup >= 0.0 ? ResolvedWarmup : Request.Preset.Capture.WarmupSeconds;
        ResolvedDuration = ResolvedDuration >= 0.0 ? ResolvedDuration : Request.Preset.Capture.DurationSeconds;
    }
    else
    {
        const FPerformanceExperimentLaunchRequest Request = BuildExperimentRequest(Dimension, TEXT("Baseline"));
        ResolvedWarmup = ResolvedWarmup >= 0.0 ? ResolvedWarmup : Request.Preset.Capture.WarmupSeconds;
        ResolvedDuration = ResolvedDuration >= 0.0 ? ResolvedDuration : Request.Preset.Capture.DurationSeconds;
    }

    const FString Timestamp = FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"));
    const FString ControllerLogPath = FPaths::Combine(FPaths::ProjectLogDir(),
        FString::Printf(TEXT("PerformanceWorkbenchController-%s-%s.log"), *Dimension, *Timestamp));
    const FString VariantArgument = VariantOverride.IsEmpty()
        ? FString() : FString::Printf(TEXT(" %s"), *VariantOverride);
    const FString ExecCommand = FString::Printf(TEXT("PerfWorkbench.LaunchFormal %s %.3f %.3f %d Exit%s"),
        *Dimension, ResolvedWarmup, ResolvedDuration, FMath::Clamp(RunCount, 1, 6),
        *VariantArgument);
    const FString Arguments = FString::Printf(
        TEXT("\"%s\" -nullrhi -RenderOffscreen -unattended -nosplash -nocrashreports -NoCrashDialog -NoPause -NoSound -nop4 -PerfWorkbenchController -PerfWorkbenchWaitForPid=%u -ExecCmds=\"%s\" -abslog=\"%s\""),
        *FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()), FPlatformProcess::GetCurrentProcessId(),
        *ExecCommand, *ControllerLogPath);

    uint32 ProcessId = 0;
    const FString Executable = FPlatformProcess::ExecutablePath();
    FProcHandle Handle = FPlatformProcess::CreateProc(*Executable, *Arguments,
        true, true, true, &ProcessId, 0, *FPaths::ProjectDir(), nullptr);
    if (!Handle.IsValid())
    {
        LastLaunchStatus = TEXT("无法启动后台采集控制器；当前实验未开始");
        return;
    }

    BatchControllerState = MakeUnique<FPerformanceWorkbenchBatchControllerState>();
    BatchControllerState->Dimension = Dimension;
    BatchControllerState->LogPath = ControllerLogPath;
    BatchControllerState->ProcessHandle = Handle;
    BatchControllerState->ProcessId = ProcessId;
    BatchControllerState->DeadlineSeconds = FPlatformTime::Seconds() + 1800.0;
    const FString BatchLabel = RunCount == 1 ? TEXT("单次采集")
        : RunCount == 6 ? TEXT("正式 3+3") : FString::Printf(TEXT("%d 次短批次"), RunCount);
    LastLaunchStatus = FString::Printf(TEXT("已安排后台%s：%s · 关闭当前编辑器后自动开始"), *BatchLabel,
        *PerformanceWorkbench::ExperimentDimensionLabel(Dimension));
    UE_LOG(LogTemp, Display, TEXT("[PerformanceWorkbench] %s · Controller PID %u · %s"),
        *LastLaunchStatus, ProcessId, *ControllerLogPath);
    if (bExitCallingProcessWhenComplete)
    {
        FPlatformMisc::RequestExit(false);
    }
}

void FPerformanceWorkbenchModule::LaunchNextFormalBatchRun()
{
    if (!FormalBatchState)
    {
        return;
    }
    if (FormalBatchState->NextRunIndex >= FormalBatchState->Variants.Num())
    {
        const FString Dimension = FormalBatchState->Dimension;
        const int32 InvalidCount = FormalBatchState->InvalidRunCount;
        const int32 RunCount = FormalBatchState->Variants.Num();
        const bool bExitWhenComplete = FormalBatchState->bExitWhenComplete;
        const FString ResultGroupId = FormalBatchState->LastCompletedGroupId.IsEmpty()
            ? FormalBatchState->BatchId : FormalBatchState->LastCompletedGroupId;
        FormalBatchState.Reset();
        LastLaunchStatus = InvalidCount == 0
            ? FString::Printf(TEXT("连续采集完成：%s · %d 次结果已载入"),
                *PerformanceWorkbench::ExperimentDimensionLabel(Dimension), RunCount)
            : FString::Printf(TEXT("连续采集完成：%s · %d/%d 次结果需要检查"),
                *PerformanceWorkbench::ExperimentDimensionLabel(Dimension), InvalidCount, RunCount);
        SelectedTimelineGroup = MakeShared<FString>(ResultGroupId);
        RefreshRecentRuns();
        if (MainScrollBox.IsValid())
        {
            MainScrollBox->SetScrollOffset(0.0f);
        }
        if (bExitWhenComplete)
        {
            FPlatformMisc::RequestExit(false);
        }
        return;
    }

    const int32 RunNumber = FormalBatchState->NextRunIndex + 1;
    const FString Variant = FormalBatchState->Variants[FormalBatchState->NextRunIndex];
    if (FormalBatchState->Dimension == TEXT("Network"))
    {
        FPerformanceNetworkSessionLaunchRequest Request = BuildNetworkSessionRequest(
            Variant, FormalBatchState->NetworkClientCount, 17770 + FormalBatchState->NextRunIndex,
            64, FormalBatchState->WarmupOverride, FormalBatchState->DurationOverride);
        Request.SourceRevision = FormalBatchState->SourceRevision;
        Request.SessionId = FString::Printf(TEXT("%s-run-%02d-%s"), *FormalBatchState->BatchId, RunNumber,
            Variant == TEXT("Baseline") ? TEXT("baseline") : TEXT("optimized"));
        NetworkLaunchState->CancelRequested = MakeShared<TAtomic<bool>, ESPMode::ThreadSafe>(false);
        NetworkLaunchState->bFormalBatch = true;
        Request.CancelRequested = NetworkLaunchState->CancelRequested;
        FormalBatchState->CurrentNetworkSessionId = Request.SessionId;
        FormalBatchState->bCancelRequested = false;
        LastLaunchStatus = FString::Printf(TEXT("连续采集 · 多人网络 · 第 %d/%d 次 · %s · 启动服务器"),
            RunNumber, FormalBatchState->Variants.Num(), *PerformanceWorkbench::ExperimentVariantLabel(Variant));
        UE_LOG(LogTemp, Display, TEXT("[PerformanceWorkbench] %s · 端口 %d · %s"),
            *LastLaunchStatus, Request.ServerPort, *Request.SessionId);
        NetworkLaunchState->Future.Emplace(FPerformanceNetworkSessionLauncher::LaunchAsync(MoveTemp(Request)));
        return;
    }

    FPerformanceExperimentLaunchRequest Request = BuildExperimentRequest(FormalBatchState->Dimension, Variant);
    if (FormalBatchState->WarmupOverride >= 0.0)
    {
        Request.Preset.Capture.WarmupSeconds = FormalBatchState->WarmupOverride;
    }
    if (FormalBatchState->DurationOverride >= 0.0)
    {
        Request.Preset.Capture.DurationSeconds = FormalBatchState->DurationOverride;
    }
    Request.SourceRevision = FormalBatchState->SourceRevision;
    Request.SessionId = FormalBatchState->BatchId;
    Request.RunId = FString::Printf(TEXT("%s-run-%02d-%s"), *FormalBatchState->BatchId, RunNumber,
        Variant == TEXT("Baseline") ? TEXT("baseline") : TEXT("optimized"));
    const double MaximumRunSeconds = Request.Preset.Capture.ReadyTimeoutSeconds
        + Request.Preset.Capture.WarmupSeconds + Request.Preset.Capture.DurationSeconds + 30.0;
    const FPerformanceExperimentLaunchResult Result = FPerformanceExperimentLauncher::Launch(MoveTemp(Request));
    if (!Result.bStarted)
    {
        ++FormalBatchState->InvalidRunCount;
        ++FormalBatchState->NextRunIndex;
        LastLaunchStatus = FString::Printf(TEXT("第 %d/%d 次启动失败，继续下一次：%s"), RunNumber,
            FormalBatchState->Variants.Num(), *Result.Error);
        LaunchNextFormalBatchRun();
        return;
    }

    FormalBatchState->ProcessId = Result.ProcessId;
    FormalBatchState->CurrentRunId = Result.RunId;
    FormalBatchState->ProcessDeadlineSeconds = FPlatformTime::Seconds() + MaximumRunSeconds;
    FormalBatchState->ResultDeadlineSeconds = 0.0;
    LastLaunchStatus = FString::Printf(TEXT("连续采集 · %s · 第 %d/%d 次 · %s"),
        *PerformanceWorkbench::ExperimentDimensionLabel(FormalBatchState->Dimension), RunNumber,
        FormalBatchState->Variants.Num(),
        *PerformanceWorkbench::ExperimentVariantLabel(Variant));
    UE_LOG(LogTemp, Display, TEXT("[PerformanceWorkbench] %s · PID %u · %s"),
        *LastLaunchStatus, Result.ProcessId, *Result.RunId);
}

void FPerformanceWorkbenchModule::AdvanceFormalBatch(bool bValid)
{
    if (!FormalBatchState)
    {
        return;
    }
    if (!bValid)
    {
        ++FormalBatchState->InvalidRunCount;
    }
    ++FormalBatchState->NextRunIndex;
    FormalBatchState->ProcessId = 0;
    FormalBatchState->CurrentRunId.Reset();
    FormalBatchState->CurrentNetworkSessionId.Reset();
    FormalBatchState->ResultDeadlineSeconds = 0.0;
    LaunchNextFormalBatchRun();
}

void FPerformanceWorkbenchModule::CancelFormalBatch()
{
    if (BatchControllerState)
    {
        if (BatchControllerState->ProcessHandle.IsValid())
        {
            if (FPlatformProcess::IsProcRunning(BatchControllerState->ProcessHandle))
            {
                FPlatformProcess::TerminateProc(BatchControllerState->ProcessHandle, true);
            }
            FPlatformProcess::CloseProc(BatchControllerState->ProcessHandle);
        }
        const FString Dimension = BatchControllerState->Dimension;
        BatchControllerState.Reset();
        LastLaunchStatus = FString::Printf(TEXT("已停止后台采集：%s"),
            *PerformanceWorkbench::ExperimentDimensionLabel(Dimension));
        return;
    }
    if (!FormalBatchState)
    {
        return;
    }
    if (FormalBatchState->Dimension == TEXT("Network") && NetworkLaunchState && NetworkLaunchState->Future.IsSet())
    {
        FormalBatchState->bCancelRequested = true;
        if (NetworkLaunchState->CancelRequested.IsValid())
        {
            NetworkLaunchState->CancelRequested->Store(true);
        }
        LastLaunchStatus = TEXT("正在停止多人网络采集；等待本轮进程退出");
        return;
    }

    const uint32 ProcessId = FormalBatchState->ProcessId;
    if (ProcessId != 0)
    {
        FProcHandle Handle = FPlatformProcess::OpenProcess(ProcessId);
        if (Handle.IsValid())
        {
            if (FPlatformProcess::IsProcRunning(Handle))
            {
                FPlatformProcess::TerminateProc(Handle, true);
            }
            FPlatformProcess::CloseProc(Handle);
        }
    }
    const FString Dimension = FormalBatchState->Dimension;
    FormalBatchState.Reset();
    LastLaunchStatus = FString::Printf(TEXT("已停止正式 3+3：%s"),
        *PerformanceWorkbench::ExperimentDimensionLabel(Dimension));
}

bool FPerformanceWorkbenchModule::PollFormalBatch(float DeltaSeconds)
{
    if (PendingControllerBatchState)
    {
        FProcHandle WaitHandle = FPlatformProcess::OpenProcess(PendingControllerBatchState->WaitForProcessId);
        const bool bHostRunning = WaitHandle.IsValid() && FPlatformProcess::IsProcRunning(WaitHandle);
        if (WaitHandle.IsValid())
        {
            FPlatformProcess::CloseProc(WaitHandle);
        }
        if (bHostRunning && FPlatformTime::Seconds() < PendingControllerBatchState->WaitDeadlineSeconds)
        {
            return true;
        }
        if (bHostRunning)
        {
            UE_LOG(LogTemp, Error, TEXT("[PerformanceWorkbench] 等待原编辑器退出超时；后台正式采集未开始"));
            PendingControllerBatchState.Reset();
            FPlatformMisc::RequestExit(false);
            return true;
        }

        const FString Dimension = PendingControllerBatchState->Dimension;
        const double WarmupOverride = PendingControllerBatchState->WarmupOverride;
        const double DurationOverride = PendingControllerBatchState->DurationOverride;
        const int32 RunCount = PendingControllerBatchState->RunCount;
        const bool bExitWhenComplete = PendingControllerBatchState->bExitWhenComplete;
        const FString VariantOverride = PendingControllerBatchState->VariantOverride;
        PendingControllerBatchState.Reset();
        StartFormalBatch(Dimension, WarmupOverride, DurationOverride, RunCount, bExitWhenComplete, VariantOverride);
        return true;
    }

    if (BatchControllerState)
    {
        const bool bRunning = BatchControllerState->ProcessHandle.IsValid()
            && FPlatformProcess::IsProcRunning(BatchControllerState->ProcessHandle);
        if (bRunning && FPlatformTime::Seconds() < BatchControllerState->DeadlineSeconds)
        {
            return true;
        }

        int32 ReturnCode = INDEX_NONE;
        if (bRunning)
        {
            FPlatformProcess::TerminateProc(BatchControllerState->ProcessHandle, true);
        }
        else if (BatchControllerState->ProcessHandle.IsValid())
        {
            FPlatformProcess::GetProcReturnCode(BatchControllerState->ProcessHandle, &ReturnCode);
        }
        if (BatchControllerState->ProcessHandle.IsValid())
        {
            FPlatformProcess::CloseProc(BatchControllerState->ProcessHandle);
        }

        const FString Dimension = BatchControllerState->Dimension;
        const FString ControllerLogPath = BatchControllerState->LogPath;
        const bool bCompleted = !bRunning && ReturnCode == 0;
        BatchControllerState.Reset();
        PerformanceWorkbenchTimeline::InvalidateIndex();
        RefreshRecentRuns();
        LastLaunchStatus = bCompleted
            ? FString::Printf(TEXT("后台采集完成：%s · 结果已载入"),
                *PerformanceWorkbench::ExperimentDimensionLabel(Dimension))
            : FString::Printf(TEXT("后台采集未正常完成：%s · 查看 %s"),
                *PerformanceWorkbench::ExperimentDimensionLabel(Dimension), *ControllerLogPath);
        return true;
    }

    if (FormalBatchState && FormalBatchState->Dimension == TEXT("Network"))
    {
        return true;
    }
    if (!FormalBatchState || FormalBatchState->ProcessId == 0)
    {
        return true;
    }

    const double Now = FPlatformTime::Seconds();
    FProcHandle Handle = FPlatformProcess::OpenProcess(FormalBatchState->ProcessId);
    const bool bRunning = Handle.IsValid() && FPlatformProcess::IsProcRunning(Handle);
    if (Handle.IsValid())
    {
        FPlatformProcess::CloseProc(Handle);
    }
    if (bRunning && Now < FormalBatchState->ProcessDeadlineSeconds)
    {
        return true;
    }
    if (bRunning)
    {
        Handle = FPlatformProcess::OpenProcess(FormalBatchState->ProcessId);
        if (Handle.IsValid())
        {
            FPlatformProcess::TerminateProc(Handle, true);
            FPlatformProcess::CloseProc(Handle);
        }
    }

    const FString SummaryPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PerformanceLab"), TEXT("Runs"),
        FormalBatchState->CurrentRunId, TEXT("summary.json"));
    if (!bRunning && IFileManager::Get().FileSize(*SummaryPath) <= 0)
    {
        if (FormalBatchState->ResultDeadlineSeconds <= 0.0)
        {
            FormalBatchState->ResultDeadlineSeconds = Now + 3.0;
            return true;
        }
        if (Now < FormalBatchState->ResultDeadlineSeconds)
        {
            return true;
        }
    }

    bool bSummaryValid = false;
    FString SummaryJson;
    TSharedPtr<FJsonObject> SummaryObject;
    if (FFileHelper::LoadFileToString(SummaryJson, *SummaryPath)
        && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(SummaryJson), SummaryObject)
        && SummaryObject.IsValid())
    {
        SummaryObject->TryGetBoolField(TEXT("valid"), bSummaryValid);
    }
    AdvanceFormalBatch(!bRunning && bSummaryValid);
    return true;
}

void FPerformanceWorkbenchModule::UpdateNetworkBatchStatus()
{
    if (!FormalBatchState || FormalBatchState->Dimension != TEXT("Network")
        || FormalBatchState->CurrentNetworkSessionId.IsEmpty() || FormalBatchState->bCancelRequested
        || !NetworkLaunchState || !NetworkLaunchState->Future.IsSet() || NetworkLaunchState->Future->IsReady())
    {
        return;
    }

    const FString RunsRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PerformanceLab"), TEXT("Runs"));
    const FString ServerRunId = FormalBatchState->CurrentNetworkSessionId + TEXT("-server");
    const bool bServerListening = IFileManager::Get().FileSize(*FPaths::Combine(
        RunsRoot, ServerRunId, TEXT("network-ready.txt"))) > 0;
    int32 ClientManifestCount = 0;
    for (int32 ClientIndex = 1; ClientIndex <= FormalBatchState->NetworkClientCount; ++ClientIndex)
    {
        const FString ClientRunId = FString::Printf(TEXT("%s-client%d"),
            *FormalBatchState->CurrentNetworkSessionId, ClientIndex);
        ClientManifestCount += IFileManager::Get().FileSize(*FPaths::Combine(
            RunsRoot, ClientRunId, TEXT("manifest.json"))) > 0 ? 1 : 0;
    }

    const FString Variant = FormalBatchState->Variants[FormalBatchState->NextRunIndex];
    const FString Stage = !bServerListening ? TEXT("等待服务器监听")
        : ClientManifestCount < FormalBatchState->NetworkClientCount
            ? FString::Printf(TEXT("服务器已监听 · 客户端 %d/%d"), ClientManifestCount, FormalBatchState->NetworkClientCount)
            : FString::Printf(TEXT("Server + %d Clients 采集中"), FormalBatchState->NetworkClientCount);
    LastLaunchStatus = FString::Printf(TEXT("连续采集 · 多人网络 · 第 %d/%d 次 · %s · %s"),
        FormalBatchState->NextRunIndex + 1, FormalBatchState->Variants.Num(),
        *PerformanceWorkbench::ExperimentVariantLabel(Variant), *Stage);
}

void FPerformanceWorkbenchModule::RefreshComparisons()
{
    if (!ComparisonBox.IsValid())
    {
        return;
    }
    ComparisonBox->ClearChildren();
    const FString Root = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PerformanceLab"), TEXT("Runs"));
	if (!ComparisonCache)
	{
		ComparisonCache = MakeUnique<FPerformanceWorkbenchComparisonCache>();
	}
	if (ComparisonCache->Comparisons.IsEmpty())
	{
		const TArray<FPerformanceComparisonQuery> Queries = PerformanceExperimentCatalog::BuildComparisonQueries();
		ComparisonCache->Comparisons = FPerformanceResultAggregator::LoadLatestComparisons(Root, Queries);
	}
	int32 SelectedIndex = INDEX_NONE;
	if (SelectedTimelineExperiment.IsValid())
	{
		for (int32 Index = 0; Index < PerformanceExperimentCatalog::All().Num(); ++Index)
		{
			if (PerformanceExperimentCatalog::All()[Index].LaunchDimension == *SelectedTimelineExperiment)
			{
				SelectedIndex = Index;
				break;
			}
		}
	}
	const TArray<FPerformanceBatchComparisonView>& Comparisons = ComparisonCache->Comparisons;
	TrackResultSummaries.Reset();
	TracksWithComparableResults.Reset();
	const TConstArrayView<FPerformanceExperimentDefinition> Definitions = PerformanceExperimentCatalog::All();
	for (int32 Index = 0; Index < Definitions.Num(); ++Index)
	{
		const FPerformanceExperimentDefinition& Definition = Definitions[Index];
		if (!Comparisons.IsValidIndex(Index))
		{
			TrackResultSummaries.Add(Definition.LaunchDimension, TEXT("尚无有效采集"));
			continue;
		}

		const FPerformanceBatchComparisonView& Comparison = Comparisons[Index];
		if (Comparison.bComparable)
		{
			TrackResultSummaries.Add(Definition.LaunchDimension,
				PerformanceWorkbench::BuildTrackResultSummary(Comparison));
			TracksWithComparableResults.Add(Definition.LaunchDimension);
		}
		else if (Comparison.BaselineRunCount > 0 || Comparison.CandidateRunCount > 0)
		{
			TrackResultSummaries.Add(Definition.LaunchDimension,
				FString::Printf(TEXT("已有短测 · 改动前 %d / 调整后 %d"),
					Comparison.BaselineRunCount, Comparison.CandidateRunCount));
		}
		else
		{
			TrackResultSummaries.Add(Definition.LaunchDimension, TEXT("尚无有效采集"));
		}
	}
	UE_LOG(LogTemp, Display, TEXT("[PerformanceWorkbench] 实验目录结果状态 · 可比较 %d/%d · 动画=%s"),
		TracksWithComparableResults.Num(), Definitions.Num(),
		*TrackResultSummaries.FindRef(TEXT("AnimationLOD")));
	if (!Comparisons.IsValidIndex(SelectedIndex))
	{
		SelectedIndex = Comparisons.IndexOfByPredicate([](const FPerformanceBatchComparisonView& Comparison)
		{
			return Comparison.bComparable;
		});
	}
	if (!Comparisons.IsValidIndex(SelectedIndex) && !Comparisons.IsEmpty())
	{
		SelectedIndex = 0;
	}

	bool bAnyComparable = false;
	for (const FPerformanceBatchComparisonView& Comparison : Comparisons)
	{
		bAnyComparable |= Comparison.bComparable;
	}
	TOptional<FPerformanceBatchComparisonView> SelectedGroupComparison;
	if (SelectedTimelineGroup.IsValid() && SelectedTimelineExperiment.IsValid())
	{
		if (const FPerformanceExperimentDefinition* Definition =
			PerformanceExperimentCatalog::FindByLaunchDimension(*SelectedTimelineExperiment))
		{
			SelectedGroupComparison = FPerformanceResultAggregator::LoadComparisonForGroup(
				Root, Definition->PresetId, Definition->ResultDimension, *SelectedTimelineGroup);
		}
	}
	const FPerformanceBatchComparisonView* DisplayedComparison = SelectedGroupComparison.IsSet()
		? &SelectedGroupComparison.GetValue()
		: Comparisons.IsValidIndex(SelectedIndex) ? &Comparisons[SelectedIndex] : nullptr;
	if (DisplayedComparison)
	{
		ComparisonBox->AddSlot().AutoHeight().Padding(0, 0, 0, 6)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ComparisonFollowsTimeline", "当前结果按上方选中的采集批次汇总"))
			.Font(FAppStyle::GetFontStyle("SmallText"))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
		ComparisonBox->AddSlot().AutoHeight().Padding(0, 0, 0, 10)
		[
			PerformanceWorkbench::BuildComparisonPanel(*DisplayedComparison)
		];
		if (!DisplayedComparison->Metrics.IsEmpty())
		{
			const FPerformanceMetricComparisonView& Metric = DisplayedComparison->Metrics[0];
			UE_LOG(LogTemp, Display,
				TEXT("[PerformanceWorkbench] 对比读数 · %s · %s · 前 %.3f [%.3f, %.3f] · 后 %.3f [%.3f, %.3f] · 范围重叠=%s"),
				*DisplayedComparison->Title, *Metric.Label,
				Metric.BaselineValue, Metric.BaselineMinValue, Metric.BaselineMaxValue,
				Metric.CandidateValue, Metric.CandidateMinValue, Metric.CandidateMaxValue,
				Metric.bSampleRangesOverlap ? TEXT("是") : TEXT("否"));
		}
	}
	if (LastLaunchStatus == TEXT("尚未启动实验") && bAnyComparable)
    {
        LastLaunchStatus = TEXT("已载入最近的实战、受控负载与多角色 A/B 结果");
    }
}

void FPerformanceWorkbenchModule::RefreshTimeline()
{
    if (!TimelineBox.IsValid())
    {
        return;
    }
    TimelineBox->ClearChildren();
    const FString Root = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PerformanceLab"), TEXT("Runs"));
	FString PreviousExperiment = SelectedTimelineExperiment.IsValid() ? *SelectedTimelineExperiment : FString();
    const FString PreviousGroup = SelectedTimelineGroup.IsValid() ? *SelectedTimelineGroup : FString();
    const FString PreviousRole = SelectedTimelineRole.IsValid() ? *SelectedTimelineRole : FString();
	const TArray<FPerformanceComparisonQuery> RegisteredQueries = PerformanceExperimentCatalog::BuildComparisonQueries();
	const FPerformanceTimelineView PreviousView = PreviousGroup.IsEmpty()
		? FPerformanceTimelineView() : PerformanceWorkbenchTimeline::LoadGroup(Root, PreviousGroup);
	if (PreviousExperiment.IsEmpty() && PreviousView.IsValid())
	{
		if (const FPerformanceExperimentDefinition* Definition =
			PerformanceExperimentCatalog::FindByResult(PreviousView.PresetId, PreviousView.Dimension))
		{
			PreviousExperiment = Definition->LaunchDimension;
		}
	}
	if (PreviousExperiment.IsEmpty())
	{
		const TArray<FString> LatestRegisteredGroups =
			PerformanceWorkbenchTimeline::ListRecentGroups(Root, RegisteredQueries, 1);
		if (!LatestRegisteredGroups.IsEmpty())
		{
			const FPerformanceTimelineView LatestView =
				PerformanceWorkbenchTimeline::LoadGroup(Root, LatestRegisteredGroups[0]);
			if (const FPerformanceExperimentDefinition* Definition =
				PerformanceExperimentCatalog::FindByResult(LatestView.PresetId, LatestView.Dimension))
			{
				PreviousExperiment = Definition->LaunchDimension;
			}
		}
	}
	if (PreviousExperiment.IsEmpty() && !PerformanceExperimentCatalog::All().IsEmpty())
	{
		PreviousExperiment = PerformanceExperimentCatalog::All()[0].LaunchDimension;
	}

	TimelineExperimentOptions.Reset();
	SelectedTimelineExperiment.Reset();
	for (const FPerformanceExperimentDefinition& Definition : PerformanceExperimentCatalog::All())
	{
		const TSharedPtr<FString> Option = MakeShared<FString>(Definition.LaunchDimension);
		TimelineExperimentOptions.Add(Option);
		if (Definition.LaunchDimension == PreviousExperiment)
		{
			SelectedTimelineExperiment = Option;
		}
	}
	if (!SelectedTimelineExperiment.IsValid() && !TimelineExperimentOptions.IsEmpty())
	{
		SelectedTimelineExperiment = TimelineExperimentOptions[0];
	}

	TArray<FPerformanceComparisonQuery> SelectedQueries;
	const FPerformanceExperimentDefinition* SelectedDefinition = SelectedTimelineExperiment.IsValid()
		? PerformanceExperimentCatalog::FindByLaunchDimension(*SelectedTimelineExperiment) : nullptr;
	if (SelectedDefinition)
	{
		SelectedQueries.Add({ SelectedDefinition->PresetId, SelectedDefinition->ResultDimension });
	}
    TimelineGroupOptions.Reset();
    SelectedTimelineGroup.Reset();
    for (const FString& GroupId : PerformanceWorkbenchTimeline::ListRecentGroups(Root, SelectedQueries))
    {
        const TSharedPtr<FString> Option = MakeShared<FString>(GroupId);
        TimelineGroupOptions.Add(Option);
        if (GroupId == PreviousGroup)
        {
            SelectedTimelineGroup = Option;
        }
    }
	if (!PreviousGroup.IsEmpty() && !SelectedTimelineGroup.IsValid())
	{
		if (PreviousView.IsValid()
			&& SelectedDefinition
			&& PreviousView.PresetId == SelectedDefinition->PresetId
			&& PreviousView.Dimension == SelectedDefinition->ResultDimension)
		{
			const TSharedPtr<FString> Option = MakeShared<FString>(PreviousGroup);
			TimelineGroupOptions.Insert(Option, 0);
			SelectedTimelineGroup = Option;
		}
	}
    if (!SelectedTimelineGroup.IsValid() && !TimelineGroupOptions.IsEmpty())
    {
        SelectedTimelineGroup = TimelineGroupOptions[0];
    }

    TimelineRoleOptions.Reset();
    SelectedTimelineRole.Reset();
    if (SelectedTimelineGroup.IsValid())
    {
        for (const FString& Role : PerformanceWorkbenchTimeline::ListRoles(Root, *SelectedTimelineGroup))
        {
            const TSharedPtr<FString> Option = MakeShared<FString>(Role);
            TimelineRoleOptions.Add(Option);
            if (Role == PreviousRole)
            {
                SelectedTimelineRole = Option;
            }
        }
        if (!SelectedTimelineRole.IsValid())
        {
            if (const TSharedPtr<FString>* ServerRole = TimelineRoleOptions.FindByPredicate([](const TSharedPtr<FString>& Role)
            {
                return Role.IsValid() && *Role == TEXT("Server");
            }))
            {
                SelectedTimelineRole = *ServerRole;
            }
            else if (!TimelineRoleOptions.IsEmpty())
            {
                SelectedTimelineRole = TimelineRoleOptions[0];
            }
        }
    }

    if (SelectedTimelineExperiment.IsValid())
    {
        TimelineBox->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)
            [
                SNew(STextBlock)
				.Text(LOCTEXT("TimelineExperimentLabel", "实验方向"))
                .ColorAndOpacity(FSlateColor::UseSubduedForeground())
            ]
            + SHorizontalBox::Slot().AutoWidth()
            [
				SNew(SBox).WidthOverride(230.0f)
				[
					SNew(SComboBox<TSharedPtr<FString>>)
					.OptionsSource(&TimelineExperimentOptions)
					.InitiallySelectedItem(SelectedTimelineExperiment)
					.OnGenerateWidget_Lambda([](const TSharedPtr<FString>& Item)
					{
						return SNew(STextBlock).Text(FText::FromString(Item.IsValid()
							? PerformanceWorkbench::ExperimentCatalogLabel(*Item) : FString()));
					})
					.OnSelectionChanged_Lambda([this](const TSharedPtr<FString>& Item, ESelectInfo::Type)
					{
						if (Item.IsValid() && (!SelectedTimelineExperiment.IsValid() || *Item != *SelectedTimelineExperiment))
						{
							SelectedTimelineExperiment = Item;
							SelectedTimelineGroup.Reset();
							SelectedTimelineRole.Reset();
							RefreshTimeline();
						}
					})
					[
						SNew(STextBlock)
						.Text_Lambda([this]
						{
							return FText::FromString(SelectedTimelineExperiment.IsValid()
								? PerformanceWorkbench::ExperimentCatalogLabel(*SelectedTimelineExperiment) : FString());
						})
					]
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(18, 0, 10, 0)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("TimelineGroupLabel", "采集批次"))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).WidthOverride(330.0f)
                [
                    SNew(SComboBox<TSharedPtr<FString>>)
                    .OptionsSource(&TimelineGroupOptions)
                    .InitiallySelectedItem(SelectedTimelineGroup)
                    .OnGenerateWidget_Lambda([](const TSharedPtr<FString>& Item)
                    {
                        return SNew(STextBlock).Text(FText::FromString(Item.IsValid()
                            ? PerformanceWorkbench::TimelineGroupLabel(*Item) : FString()));
                    })
                    .OnSelectionChanged_Lambda([this](const TSharedPtr<FString>& Item, ESelectInfo::Type)
                    {
                        if (Item.IsValid() && (!SelectedTimelineGroup.IsValid() || *Item != *SelectedTimelineGroup))
                        {
                            SelectedTimelineGroup = Item;
                            RefreshTimeline();
                        }
                    })
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this]
                        {
                            return FText::FromString(SelectedTimelineGroup.IsValid()
								? PerformanceWorkbench::TimelineGroupLabel(*SelectedTimelineGroup) : TEXT("暂无有效采集"));
                        })
                    ]
                ]
            ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(18, 0, 10, 0)
            [
                SNew(STextBlock)
                .Visibility(TimelineRoleOptions.Num() > 1 ? EVisibility::Visible : EVisibility::Collapsed)
                .Text(LOCTEXT("TimelineRoleLabel", "进程"))
                .ColorAndOpacity(FSlateColor::UseSubduedForeground())
            ]
            + SHorizontalBox::Slot().AutoWidth()
            [
                SNew(SBox).WidthOverride(150.0f)
                .Visibility(TimelineRoleOptions.Num() > 1 ? EVisibility::Visible : EVisibility::Collapsed)
                [
                    SNew(SComboBox<TSharedPtr<FString>>)
                    .OptionsSource(&TimelineRoleOptions)
                    .InitiallySelectedItem(SelectedTimelineRole)
                    .OnGenerateWidget_Lambda([](const TSharedPtr<FString>& Item)
                    {
                        return SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? PerformanceWorkbench::ProcessRoleLabel(*Item) : FString()));
                    })
                    .OnSelectionChanged_Lambda([this](const TSharedPtr<FString>& Item, ESelectInfo::Type)
                    {
                        if (Item.IsValid() && (!SelectedTimelineRole.IsValid() || *Item != *SelectedTimelineRole))
                        {
                            SelectedTimelineRole = Item;
                            RefreshTimeline();
                        }
                    })
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this]
                        {
                            return FText::FromString(SelectedTimelineRole.IsValid()
                                ? PerformanceWorkbench::ProcessRoleLabel(*SelectedTimelineRole) : FString());
                        })
                    ]
                ]
            ]
            + SHorizontalBox::Slot().FillWidth(1.0f)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(FText::FromString(FString::Printf(TEXT("可选 %d 组"), TimelineGroupOptions.Num())))
                .Font(FAppStyle::GetFontStyle("SmallText"))
                .ColorAndOpacity(FSlateColor::UseSubduedForeground())
            ]
        ];
    }

    const FPerformanceTimelineView View = SelectedTimelineGroup.IsValid()
        ? PerformanceWorkbenchTimeline::LoadGroup(Root, *SelectedTimelineGroup,
            SelectedTimelineRole.IsValid() ? *SelectedTimelineRole : FString())
        : FPerformanceTimelineView();
    if (View.IsValid())
    {
        TimelineBox->AddSlot().AutoHeight()[PerformanceWorkbenchTimeline::BuildPanel(View)];
    }
    else
    {
        TimelineBox->AddSlot().AutoHeight()[SNew(STextBlock).Text(LOCTEXT("EmptyTimeline", "暂无可显示的有效采集。"))];
    }

	RefreshComparisons();
}

void FPerformanceWorkbenchModule::OpenResultsDirectory() const
{
    const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PerformanceLab"), TEXT("Runs")));
    IFileManager::Get().MakeDirectory(*Directory, true);
    FPlatformProcess::ExploreFolder(*Directory);
}

void FPerformanceWorkbenchModule::ExportComparisonCommand(const TArray<FString>& Arguments)
{
	if (Arguments.Num() >= 2)
	{
		SelectResultGroup(Arguments[0], Arguments[1],
			Arguments.IsValidIndex(2) ? Arguments[2] : FString());
	}
	else if (Arguments.Num() == 1)
	{
		SelectLatestResult(Arguments[0]);
	}
	ExportSelectedComparison();
}

void FPerformanceWorkbenchModule::ExportSelectedComparison()
{
	if (!SelectedTimelineExperiment.IsValid() || !SelectedTimelineGroup.IsValid())
	{
		LastLaunchStatus = TEXT("先在采集时间轨道中选择一个实验批次");
		return;
	}
	const FPerformanceExperimentDefinition* Definition =
		PerformanceExperimentCatalog::FindByLaunchDimension(*SelectedTimelineExperiment);
	if (!Definition)
	{
		LastLaunchStatus = TEXT("当前实验方向不在性能工作台目录中");
		return;
	}

	const FString RunsRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PerformanceLab"), TEXT("Runs"));
	const FPerformanceBatchComparisonView Comparison = FPerformanceResultAggregator::LoadComparisonForGroup(
		RunsRoot, Definition->PresetId, Definition->ResultDimension, *SelectedTimelineGroup);
	if (Comparison.Title.IsEmpty())
	{
		LastLaunchStatus = TEXT("当前批次没有可导出的汇总结果");
		return;
	}

	const FString ReportsDirectory = FPaths::ConvertRelativePathToFull(FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("PerformanceLab"), TEXT("Reports")));
	IFileManager::Get().MakeDirectory(*ReportsDirectory, true);
	const FString FileName = FPaths::MakeValidFileName(FString::Printf(TEXT("%s-%s-%s.md"),
		*Definition->PresetId, *Definition->ResultDimension,
		*FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"))));
	const FString ReportPath = FPaths::Combine(ReportsDirectory, FileName);
	const FString Markdown = PerformanceWorkbench::BuildComparisonMarkdown(
		Comparison, *SelectedTimelineGroup);
	if (!FFileHelper::SaveStringToFile(Markdown, *ReportPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		LastLaunchStatus = TEXT("保存对比报告失败；原始采集结果没有改变");
		UE_LOG(LogTemp, Error, TEXT("[PerformanceWorkbench] 无法保存对比报告：%s"), *ReportPath);
		return;
	}

	LastLaunchStatus = FString::Printf(TEXT("已保存对比报告：%s"), *FileName);
	UE_LOG(LogTemp, Display, TEXT("[PerformanceWorkbench] 对比报告已保存：%s"), *ReportPath);
}

void FPerformanceWorkbenchModule::RefreshRecentRuns()
{
    if (!RecentRunsBox.IsValid())
    {
        return;
    }

    const double RefreshStartedSeconds = FPlatformTime::Seconds();
    RecentRunsBox->ClearChildren();
    PerformanceWorkbenchTimeline::InvalidateIndex();
	FPerformanceResultAggregator::InvalidateRunCache();
	if (ComparisonCache)
	{
		ComparisonCache->Comparisons.Reset();
	}
    RefreshTimeline();
    const FString Root = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PerformanceLab"), TEXT("Runs"));
    TArray<FString> Directories;
    IFileManager::Get().FindFiles(Directories, *FPaths::Combine(Root, TEXT("*")), false, true);

    struct FRecentRunEntry
    {
        FString Directory;
        FDateTime SortTime = FDateTime::MinValue();
        TSharedPtr<FJsonObject> Summary;
    };

    TArray<FRecentRunEntry> Candidates;
    Candidates.Reserve(Directories.Num());
    for (const FString& Directory : Directories)
    {
		FRecentRunEntry Entry;
        Entry.Directory = Directory;
        const FString SummaryPath = FPaths::Combine(Root, Directory, TEXT("summary.json"));
		Entry.SortTime = IFileManager::Get().GetTimeStamp(*SummaryPath);
		if (Entry.SortTime != FDateTime::MinValue())
		{
			Candidates.Add(MoveTemp(Entry));
		}
    }

    Candidates.Sort([](const FRecentRunEntry& A, const FRecentRunEntry& B)
    {
        return A.SortTime == B.SortTime ? A.Directory > B.Directory : A.SortTime > B.SortTime;
    });

	TArray<FRecentRunEntry> RecentRuns;
	RecentRuns.Reserve(8);
	int32 ParsedSummaryCount = 0;
	for (FRecentRunEntry& Entry : Candidates)
	{
		FString Json;
		const FString SummaryPath = FPaths::Combine(Root, Entry.Directory, TEXT("summary.json"));
		++ParsedSummaryCount;
		if (!FFileHelper::LoadFileToString(Json, *SummaryPath)
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Entry.Summary)
			|| !Entry.Summary.IsValid())
		{
			continue;
		}

		FString PresetId;
		FString Dimension;
		if (!Entry.Summary->TryGetStringField(TEXT("presetId"), PresetId)
			|| !Entry.Summary->TryGetStringField(TEXT("dimension"), Dimension)
			|| !PerformanceExperimentCatalog::IsRegisteredResult(PresetId, Dimension))
		{
			continue;
		}
		RecentRuns.Add(MoveTemp(Entry));
		if (RecentRuns.Num() >= 8)
		{
			break;
		}
	}

    const int32 VisibleCount = FMath::Min(RecentRuns.Num(), 8);
    if (VisibleCount == 0)
    {
        RecentRunsBox->AddSlot().AutoHeight()[SNew(STextBlock).Text(LOCTEXT("EmptyRuns", "暂无结果。先运行一个基线实验。"))];
        return;
    }

    for (int32 Index = 0; Index < VisibleCount; ++Index)
    {
        const FRecentRunEntry& Entry = RecentRuns[Index];
        const FString& Directory = Entry.Directory;
        const TSharedPtr<FJsonObject>& RootObject = Entry.Summary;
        const bool bLoaded = RootObject.IsValid();

        FString Line = Directory + TEXT(" · 运行中或结果尚未生成");
		FString LaunchDimension;
		FString GroupId;
		FString ProcessRole = TEXT("Standalone");
        if (bLoaded)
        {
            bool bValid = false;
			double AgentsValue = 0.0;
			double TargetAgentsValue = 0.0;
			double AverageMs = 0.0;
			RootObject->TryGetBoolField(TEXT("valid"), bValid);
			RootObject->TryGetNumberField(TEXT("possessedAIPawnCount"), AgentsValue);
			RootObject->TryGetNumberField(TEXT("targetAgentCount"), TargetAgentsValue);
			RootObject->TryGetNumberField(TEXT("averageFrameTimeMs"), AverageMs);
			const int32 Agents = static_cast<int32>(AgentsValue);
			const int32 TargetAgents = static_cast<int32>(TargetAgentsValue);
            FString PresetId;
            FString Dimension;
            FString Variant;
            RootObject->TryGetStringField(TEXT("presetId"), PresetId);
            RootObject->TryGetStringField(TEXT("dimension"), Dimension);
            RootObject->TryGetStringField(TEXT("variant"), Variant);
            RootObject->TryGetStringField(TEXT("processRole"), ProcessRole);
			if (ProcessRole.IsEmpty())
			{
				ProcessRole = TEXT("Standalone");
			}
			RootObject->TryGetStringField(TEXT("sessionId"), GroupId);
			if (GroupId.IsEmpty())
			{
				RootObject->TryGetStringField(TEXT("runId"), GroupId);
			}
			if (GroupId.IsEmpty())
			{
				GroupId = Directory;
			}
			if (const FPerformanceExperimentDefinition* Definition =
				PerformanceExperimentCatalog::FindByResult(PresetId, Dimension))
			{
				LaunchDimension = Definition->LaunchDimension;
			}
            Line = FString::Printf(
                TEXT("%s · %s · %s · %s · %s · %s · AI %d/%d · %.2f ms"),
                *Entry.SortTime.ToString(TEXT("%m-%d %H:%M")),
                *PerformanceWorkbench::ExperimentSubjectLabel(PresetId),
                *PerformanceWorkbench::ExperimentDimensionLabel(Dimension),
                *PerformanceWorkbench::ExperimentVariantLabel(Variant),
                *PerformanceWorkbench::ProcessRoleLabel(ProcessRole),
                bValid ? TEXT("有效") : TEXT("无效"),
                Agents,
                TargetAgents,
                AverageMs);
        }

        RecentRunsBox->AddSlot().AutoHeight().Padding(0, 2)
        [
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Line))
				.AutoWrapText(true)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12, 0, 0, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("OpenRecentRun", "查看"))
				.ToolTipText(LOCTEXT("OpenRecentRunTip", "在上方时间轨道中打开这次采集及其同批次对比。"))
				.IsEnabled(!LaunchDimension.IsEmpty() && !GroupId.IsEmpty())
				.OnClicked_Lambda([this, LaunchDimension, GroupId, ProcessRole]
				{
					SelectResultGroup(LaunchDimension, GroupId, ProcessRole);
					return FReply::Handled();
				})
			]
        ];
    }

	UE_LOG(LogTemp, Display,
		TEXT("[PerformanceWorkbench] 结果刷新完成 · 目录=%d · 最近记录解析=%d · 显示=%d · 用时=%.1f ms"),
		Directories.Num(), ParsedSummaryCount, VisibleCount,
		(FPlatformTime::Seconds() - RefreshStartedSeconds) * 1000.0);
}

IMPLEMENT_MODULE(FPerformanceWorkbenchModule, PerformanceWorkbench)

#undef LOCTEXT_NAMESPACE
