#include "PerformanceWorkbenchModule.h"
#include "PerformanceExperimentLauncher.h"
#include "PerformanceNetworkSessionLauncher.h"
#include "PerformanceResultAggregator.h"

#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Interfaces/IPluginManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
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

    FString FormatMetricValue(double Value, const FString& Unit)
    {
        if (Unit == TEXT("ms")) return FString::Printf(TEXT("%.3f %s"), Value, *Unit);
        if (Unit == TEXT("B/s") || Unit == TEXT("B/帧")) return FString::Printf(TEXT("%.0f %s"), Value, *Unit);
        return FString::Printf(TEXT("%.1f %s"), Value, *Unit);
    }

    FSlateColor DeltaColor(const FPerformanceMetricComparisonView& Metric)
    {
        const double SignedBenefit = Metric.bLowerIsBetter ? -Metric.DeltaPercent : Metric.DeltaPercent;
        if (SignedBenefit >= 2.0) return FLinearColor(0.20f, 0.82f, 0.46f);
        if (SignedBenefit <= -2.0) return FLinearColor(1.0f, 0.32f, 0.28f);
        return FLinearColor(0.95f, 0.72f, 0.25f);
    }

    FString DeltaAssessment(const FPerformanceMetricComparisonView& Metric)
    {
        const double SignedBenefit = Metric.bLowerIsBetter ? -Metric.DeltaPercent : Metric.DeltaPercent;
        if (SignedBenefit >= 2.0) return TEXT("改善");
        if (SignedBenefit <= -2.0) return TEXT("回退");
        return TEXT("基本持平");
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
                    .Text(FText::FromString(Comparison.bComparable ? TEXT("正式可比较") : TEXT("结果未就绪")))
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
            Grid->AddSlot(1, 0).Padding(12, 5)[SNew(STextBlock).Text(LOCTEXT("BaselineHeader", "Baseline 中位数")).Font(HeaderFont).ColorAndOpacity(FSlateColor::UseSubduedForeground())];
            Grid->AddSlot(2, 0).Padding(12, 5)[SNew(STextBlock).Text(LOCTEXT("CandidateHeader", "Optimized 中位数")).Font(HeaderFont).ColorAndOpacity(FSlateColor::UseSubduedForeground())];
            Grid->AddSlot(3, 0).Padding(12, 5)[SNew(STextBlock).Text(LOCTEXT("DeltaHeader", "变化")).Font(HeaderFont).ColorAndOpacity(FSlateColor::UseSubduedForeground())];
            Grid->AddSlot(4, 0).Padding(12, 5)[SNew(STextBlock).Text(LOCTEXT("AssessmentHeader", "判断")).Font(HeaderFont).ColorAndOpacity(FSlateColor::UseSubduedForeground())];

            int32 Row = 1;
            for (const FPerformanceMetricComparisonView& Metric : Comparison.Metrics)
            {
                const FSlateColor Color = DeltaColor(Metric);
                Grid->AddSlot(0, Row).Padding(4, 6)[SNew(STextBlock).Text(FText::FromString(Metric.Label))];
                Grid->AddSlot(1, Row).Padding(12, 6)[SNew(STextBlock).Text(FText::FromString(FormatMetricValue(Metric.BaselineValue, Metric.Unit)))];
                Grid->AddSlot(2, Row).Padding(12, 6)[SNew(STextBlock).Text(FText::FromString(FormatMetricValue(Metric.CandidateValue, Metric.Unit)))];
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
                ];
            }
        }

        return SNew(SBorder)
            .Padding(16)
            .BorderImage(FAppStyle::GetBrush(Comparison.bComparable ? "Brushes.Header" : "Brushes.Recessed"))
            [
                Content
            ];
    }
}

struct FPerformanceWorkbenchNetworkLaunchState
{
    TOptional<TFuture<FPerformanceNetworkSessionLaunchResult>> Future;
};

void FPerformanceWorkbenchModule::StartupModule()
{
    NetworkLaunchState = MakeUnique<FPerformanceWorkbenchNetworkLaunchState>();
    NetworkLaunchCommand = IConsoleManager::Get().RegisterConsoleCommand(
        TEXT("PerfWorkbench.LaunchNetwork"),
        TEXT("启动后台网络实验。参数：Variant ClientCount Port [FixtureCount Warmup Duration]。"),
        FConsoleCommandWithArgsDelegate::CreateRaw(this, &FPerformanceWorkbenchModule::LaunchNetworkSession),
        ECVF_Default);
    CaptureUICommand = IConsoleManager::Get().RegisterConsoleCommand(
        TEXT("PerfWorkbench.CaptureUI"),
        TEXT("打开真实性能工作台并将 Slate 面板截图保存到指定 PNG。"),
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
    NetworkLaunchState.Reset();
    UToolMenus::UnRegisterStartupCallback(this);
    UToolMenus::UnregisterOwner(this);
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(PerformanceWorkbench::TabName);
}

void FPerformanceWorkbenchModule::CaptureWorkbenchUI(const TArray<FString>& Arguments)
{
    WorkbenchTab = FGlobalTabmanager::Get()->TryInvokeTab(PerformanceWorkbench::TabName);
    const bool bNetworkView = Arguments.IsValidIndex(0) && Arguments[0].Equals(TEXT("network"), ESearchCase::IgnoreCase);
    PendingScreenshotPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(
        FPaths::ProjectDir(), TEXT("Documentation"), TEXT("Images"), TEXT("PerformanceWorkbench"),
        bNetworkView ? TEXT("性能工作台-网络AB对比.png") : TEXT("性能工作台-渲染AB对比.png")));
    if (MainScrollBox.IsValid())
    {
        MainScrollBox->SetScrollOffset(bNetworkView ? 400.0f : 0.0f);
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
    if (NetworkLaunchState->Future.IsSet() && !NetworkLaunchState->Future->IsReady())
    {
        LastLaunchStatus = TEXT("已有网络实验正在启动，请等待服务器与客户端完成编排");
        return;
    }

    FPerformanceNetworkSessionLaunchRequest Request;
    if (Arguments.IsValidIndex(0) && !Arguments[0].IsEmpty())
    {
        Request.Variant = Arguments[0];
    }
    if (Arguments.IsValidIndex(1))
    {
        Request.ClientCount = FMath::Clamp(FCString::Atoi(*Arguments[1]), 1, 8);
    }
    if (Arguments.IsValidIndex(2))
    {
        Request.ServerPort = FMath::Clamp(FCString::Atoi(*Arguments[2]), 1024, 65535);
    }
    int32 FixtureCount = 64;
    if (Arguments.IsValidIndex(3))
    {
        FixtureCount = FMath::Clamp(FCString::Atoi(*Arguments[3]), 1, 512);
    }
    if (Arguments.IsValidIndex(4))
    {
        Request.Preset.Capture.WarmupSeconds = FMath::Max(0.0, FCString::Atod(*Arguments[4]));
    }
    if (Arguments.IsValidIndex(5))
    {
        Request.Preset.Capture.DurationSeconds = FMath::Max(1.0, FCString::Atod(*Arguments[5]));
    }
    Request.Preset.ExtraCommandLine = FString::Printf(
        TEXT("-PerfLabFixtureClass=/Script/HeroArenaRuntime.HeroArenaRelayBeacon -PerfLabFixtureCount=%d -PerfLabFixtureSpacing=4000"),
        FixtureCount);
    const bool bReplicationGraphSmoke = Request.Variant.Equals(TEXT("RepGraphSmoke"), ESearchCase::IgnoreCase);
    if (Request.Variant.Equals(TEXT("Optimized"), ESearchCase::IgnoreCase) || bReplicationGraphSmoke)
    {
        Request.Preset.ExtraCommandLine += TEXT(" -HeroArenaRelaySpatialReplication");
    }
    if (bReplicationGraphSmoke)
    {
        // Lyra ships with its Replication Graph disabled in DefaultGame.ini.
        // Keep the project default untouched until an isolated run proves the
        // graph is correctly routing HeroArena actors and offers independent value.
        Request.Preset.ExtraCommandLine +=
            TEXT(" -ini:Game:[/Script/LyraGame.LyraReplicationGraphSettings]:bDisableReplicationGraph=False");
    }

    LastLaunchStatus = FString::Printf(TEXT("正在启动网络实验：%s · %d 客户端 · 端口 %d"), *Request.Variant, Request.ClientCount, Request.ServerPort);
    UE_LOG(LogTemp, Display, TEXT("[PerformanceWorkbench] %s"), *LastLaunchStatus);
    NetworkLaunchState->Future.Emplace(FPerformanceNetworkSessionLauncher::LaunchAsync(MoveTemp(Request)));
}

bool FPerformanceWorkbenchModule::PollNetworkLaunch(float DeltaSeconds)
{
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
    }

    if (!NetworkLaunchState || !NetworkLaunchState->Future.IsSet() || !NetworkLaunchState->Future->IsReady())
    {
        return true;
    }

    const FPerformanceNetworkSessionLaunchResult Result = NetworkLaunchState->Future->Get();
    NetworkLaunchState->Future.Reset();
    LastLaunchStatus = Result.bStarted
        ? FString::Printf(TEXT("网络实验已启动：%s · %d 个受控进程"), *Result.SessionId, Result.Processes.Num())
        : FString::Printf(TEXT("网络实验启动失败：%s"), *Result.Error);
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
                    SNew(STextBlock)
                    .Text(LOCTEXT("Heading", "Lyra 性能工作台"))
                    .Font(FAppStyle::GetFontStyle("HeadingExtraLarge"))
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 18)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("Contract", "运行可复现 Benchmark，自动核对实验合同，并把三次 A/B 聚合成可解释结论"))
                    .ColorAndOpacity(FSlateColor::UseSubduedForeground())
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
                [
                    SNew(SBorder)
                    .Padding(12)
                    .BorderImage(FAppStyle::GetBrush("Brushes.Header"))
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this] { return FText::FromString(LastLaunchStatus); })
                    ]
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 8)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("ComparisonHeading", "正式 A/B 对比"))
                    .Font(FAppStyle::GetFontStyle("HeadingLarge"))
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
                    .Text(LOCTEXT("LaunchHeading", "实验启动"))
                    .Font(FAppStyle::GetFontStyle("HeadingLarge"))
                ]
                + SVerticalBox::Slot().AutoHeight()[BuildExperimentRow(LOCTEXT("NetworkName", "Relay 空间相关性"), LOCTEXT("NetworkDetail", "1 Dedicated Server + 2 Client · 64 Relay · Baseline/Optimized 单次运行"), TEXT("Network"))]
                + SVerticalBox::Slot().AutoHeight()[BuildExperimentRow(LOCTEXT("AIName", "AI 决策与状态机"), LOCTEXT("AIDetail", "固定 200 个 Lyra Pawn；逐 NPC 全表扫描对比共享战场事实与空间分桶"), TEXT("AIDecision"))]
                + SVerticalBox::Slot().AutoHeight()[BuildExperimentRow(LOCTEXT("NavName", "寻路"), LOCTEXT("NavDetail", "比较路径请求频率、重规划和移动目标更新成本"), TEXT("Navigation"))]
                + SVerticalBox::Slot().AutoHeight()[BuildExperimentRow(LOCTEXT("AnimName", "IK 与动画 LOD"), LOCTEXT("AnimDetail", "比较远距离动画更新、骨骼与 IK 计算成本"), TEXT("AnimationLOD"))]
                + SVerticalBox::Slot().AutoHeight()[BuildExperimentRow(LOCTEXT("RenderName", "渲染"), LOCTEXT("RenderDetail", "比较角色可见性、阴影、材质与网格 LOD 成本"), TEXT("Rendering"))]
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

TSharedRef<SWidget> FPerformanceWorkbenchModule::BuildExperimentRow(const FText& Title, const FText& Detail, const FString& Dimension)
{
    const bool bOptimizedAvailable = Dimension == TEXT("Network") || Dimension == TEXT("Rendering") || Dimension == TEXT("AIDecision");
    return SNew(SBorder)
        .Padding(12)
        .BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(Title).Font(FAppStyle::GetFontStyle("HeadingSmall"))]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)[SNew(STextBlock).Text(Detail).ColorAndOpacity(FSlateColor::UseSubduedForeground())]
            ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12, 0, 6, 0)
            [
                SNew(SButton)
                .Text(LOCTEXT("RunBaseline", "运行基线"))
                .OnClicked_Lambda([this, Dimension]
                {
                    if (Dimension == TEXT("Network")) LaunchNetworkSession({ TEXT("Baseline"), TEXT("2"), TEXT("7779") });
                    else LaunchExperiment(Dimension, TEXT("Baseline"));
                    return FReply::Handled();
                })
            ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [
                SNew(SButton)
                .ButtonStyle(FAppStyle::Get(), "PrimaryButton")
                .Text(LOCTEXT("RunOptimized", "运行优化版"))
                .ToolTipText(bOptimizedAvailable
                    ? LOCTEXT("OptimizedReady", "使用该方向已实现的真实优化开关启动单次实验。")
                    : LOCTEXT("OptimizedPending", "该方向的优化开关实现后启用；当前不会用同一配置伪造优化结果。"))
                .IsEnabled(bOptimizedAvailable)
                .OnClicked_Lambda([this, Dimension]
                {
                    if (Dimension == TEXT("Network")) LaunchNetworkSession({ TEXT("Optimized"), TEXT("2"), TEXT("7780") });
                    else LaunchExperiment(Dimension, TEXT("Optimized"));
                    return FReply::Handled();
                })
            ]
        ];
}

void FPerformanceWorkbenchModule::LaunchExperiment(const FString& Dimension, const FString& Variant)
{
    FPerformanceExperimentLaunchRequest Request;
    Request.Preset = Dimension == TEXT("Rendering")
        ? FPerformanceExperimentPresetData::MakeHeroArenaRendering()
        : FPerformanceExperimentPresetData::MakeCrowd200();
    Request.Dimension = Dimension;
    Request.Variant = Variant;
    if (Dimension == TEXT("Rendering") && Variant == TEXT("Optimized"))
    {
        Request.Preset.ExtraCommandLine = TEXT("-LyraCharacterRenderSignificance");
    }
    else if (Dimension == TEXT("AIDecision") && Variant == TEXT("Optimized"))
    {
        Request.Preset.ExtraCommandLine = TEXT("-PerfLabCrowdStateMachine -PerfLabSharedBattleFacts");
    }
    else if (Dimension == TEXT("AIDecision"))
    {
        Request.Preset.ExtraCommandLine = TEXT("-PerfLabCrowdStateMachine");
    }
    else if (Dimension == TEXT("Navigation") && Variant == TEXT("Optimized"))
    {
        Request.Preset.ExtraCommandLine = TEXT("-PerfLabNavigationLoad -PerfLabNavigationBudget");
    }
    else if (Dimension == TEXT("Navigation"))
    {
        Request.Preset.ExtraCommandLine = TEXT("-PerfLabNavigationLoad");
    }
    const FPerformanceExperimentLaunchResult Result = FPerformanceExperimentLauncher::Launch(MoveTemp(Request));
    LastLaunchStatus = Result.bStarted
        ? FString::Printf(TEXT("实验已启动：%s / %s（运行编号 %s）"), *Dimension, *Variant, *Result.RunId)
        : FString::Printf(TEXT("启动失败：%s"), *Result.Error);
}

void FPerformanceWorkbenchModule::RefreshComparisons()
{
    if (!ComparisonBox.IsValid())
    {
        return;
    }
    ComparisonBox->ClearChildren();
    const FString Root = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PerformanceLab"), TEXT("Runs"));
    const FPerformanceBatchComparisonView Rendering = FPerformanceResultAggregator::LoadLatestComparison(Root, TEXT("hero-arena-character-rendering"));
    const FPerformanceBatchComparisonView Network = FPerformanceResultAggregator::LoadLatestComparison(Root, TEXT("hero-arena-network"));
    const FPerformanceBatchComparisonView AIDecision = FPerformanceResultAggregator::LoadLatestComparison(Root, TEXT("crowd-200"));
    ComparisonBox->AddSlot().AutoHeight().Padding(0, 0, 0, 10)[PerformanceWorkbench::BuildComparisonPanel(Rendering)];
    ComparisonBox->AddSlot().AutoHeight().Padding(0, 0, 0, 10)[PerformanceWorkbench::BuildComparisonPanel(Network)];
    ComparisonBox->AddSlot().AutoHeight()[PerformanceWorkbench::BuildComparisonPanel(AIDecision)];
    if (LastLaunchStatus == TEXT("尚未启动实验") && Rendering.bComparable && Network.bComparable && AIDecision.bComparable)
    {
        LastLaunchStatus = TEXT("已载入 3 组正式 A/B · 渲染、网络与 AI 决策实验合同全部通过");
    }
}

void FPerformanceWorkbenchModule::OpenResultsDirectory() const
{
    const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PerformanceLab"), TEXT("Runs")));
    IFileManager::Get().MakeDirectory(*Directory, true);
    FPlatformProcess::ExploreFolder(*Directory);
}

void FPerformanceWorkbenchModule::RefreshRecentRuns()
{
    if (!RecentRunsBox.IsValid())
    {
        return;
    }

    RecentRunsBox->ClearChildren();
    RefreshComparisons();
    const FString Root = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PerformanceLab"), TEXT("Runs"));
    TArray<FString> Directories;
    IFileManager::Get().FindFiles(Directories, *FPaths::Combine(Root, TEXT("*")), false, true);
    Directories.Sort([](const FString& A, const FString& B) { return A > B; });

    const int32 VisibleCount = FMath::Min(Directories.Num(), 8);
    if (VisibleCount == 0)
    {
        RecentRunsBox->AddSlot().AutoHeight()[SNew(STextBlock).Text(LOCTEXT("EmptyRuns", "暂无结果。先运行一个基线实验。"))];
        return;
    }

    for (int32 Index = 0; Index < VisibleCount; ++Index)
    {
        const FString& Directory = Directories[Index];
        FString Json;
        TSharedPtr<FJsonObject> RootObject;
        const FString SummaryPath = FPaths::Combine(Root, Directory, TEXT("summary.json"));
        const bool bLoaded = FFileHelper::LoadFileToString(Json, *SummaryPath)
            && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), RootObject)
            && RootObject.IsValid();

        FString Line = Directory + TEXT(" · 运行中或结果尚未生成");
        if (bLoaded)
        {
            const bool bValid = RootObject->GetBoolField(TEXT("valid"));
            const int32 Agents = static_cast<int32>(RootObject->GetNumberField(TEXT("possessedAIPawnCount")));
            const int32 TargetAgents = static_cast<int32>(RootObject->GetNumberField(TEXT("targetAgentCount")));
            const double AverageMs = RootObject->GetNumberField(TEXT("averageFrameTimeMs"));
            Line = FString::Printf(TEXT("%s · %s · AI %d/%d · 平均帧时 %.2f ms"), *Directory, bValid ? TEXT("有效") : TEXT("无效"), Agents, TargetAgents, AverageMs);
        }

        RecentRunsBox->AddSlot().AutoHeight().Padding(0, 2)
        [
            SNew(STextBlock).Text(FText::FromString(Line))
        ];
    }
}

IMPLEMENT_MODULE(FPerformanceWorkbenchModule, PerformanceWorkbench)

#undef LOCTEXT_NAMESPACE
