#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Modules/ModuleManager.h"

class FSpawnTabArgs;
class SDockTab;
class SScrollBox;
class SVerticalBox;
class IConsoleObject;
struct FPerformanceWorkbenchNetworkLaunchState;
struct FPerformanceWorkbenchFormalBatchState;
struct FPerformanceWorkbenchBatchControllerState;
struct FPerformanceWorkbenchPendingControllerBatchState;
struct FPerformanceWorkbenchComparisonCache;
struct FPerformanceExperimentLaunchRequest;
struct FPerformanceNetworkSessionLaunchRequest;

class FPerformanceWorkbenchModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    TSharedRef<SDockTab> SpawnWorkbenchTab(const FSpawnTabArgs& Args);
    void RegisterMenus();
    FPerformanceExperimentLaunchRequest BuildExperimentRequest(const FString& Dimension, const FString& Variant) const;
    FPerformanceNetworkSessionLaunchRequest BuildNetworkSessionRequest(const FString& Variant, int32 ClientCount,
        int32 ServerPort, int32 FixtureCount, double WarmupOverride = -1.0, double DurationOverride = -1.0) const;
    void StartFormalBatch(const FString& Dimension, double WarmupOverride = -1.0, double DurationOverride = -1.0,
        int32 RunCount = 6, bool bExitWhenComplete = false, const FString& VariantOverride = FString());
    void LaunchFormalBatchController(const FString& Dimension, double WarmupOverride, double DurationOverride,
        int32 RunCount, bool bExitCallingProcessWhenComplete, const FString& VariantOverride);
    void LaunchFormalBatchCommand(const TArray<FString>& Arguments);
    void CancelFormalBatch();
    void LaunchNextFormalBatchRun();
    void AdvanceFormalBatch(bool bValid);
    bool PollFormalBatch(float DeltaSeconds);
    void UpdateNetworkBatchStatus();
    bool IsAnyExperimentRunning() const;
    void LaunchNetworkSession(const TArray<FString>& Arguments);
    void CaptureWorkbenchUI(const TArray<FString>& Arguments);
    bool PollNetworkLaunch(float DeltaSeconds);
    void OpenResultsDirectory() const;
	void ExportSelectedComparison();
	void ExportComparisonCommand(const TArray<FString>& Arguments);
    void RefreshComparisons();
    void RefreshTimeline();
    void RefreshRecentRuns();
	void SelectLatestResult(const FString& Dimension);
	void SelectResultGroup(const FString& Dimension, const FString& GroupId, const FString& ProcessRole);
    TSharedRef<SWidget> BuildExperimentRow(int32 TrackNumber, const FText& Title, const FText& Detail, const FString& Dimension);

    TSharedPtr<SVerticalBox> RecentRunsBox;
    TSharedPtr<SVerticalBox> ComparisonBox;
    TSharedPtr<SVerticalBox> TimelineBox;
    TSharedPtr<SScrollBox> MainScrollBox;
	TArray<TSharedPtr<FString>> TimelineExperimentOptions;
	TSharedPtr<FString> SelectedTimelineExperiment;
    TArray<TSharedPtr<FString>> TimelineGroupOptions;
    TSharedPtr<FString> SelectedTimelineGroup;
    TArray<TSharedPtr<FString>> TimelineRoleOptions;
    TSharedPtr<FString> SelectedTimelineRole;
    TWeakPtr<SDockTab> WorkbenchTab;
    TUniquePtr<FPerformanceWorkbenchNetworkLaunchState> NetworkLaunchState;
    TUniquePtr<FPerformanceWorkbenchFormalBatchState> FormalBatchState;
    TUniquePtr<FPerformanceWorkbenchBatchControllerState> BatchControllerState;
    TUniquePtr<FPerformanceWorkbenchPendingControllerBatchState> PendingControllerBatchState;
    TUniquePtr<FPerformanceWorkbenchComparisonCache> ComparisonCache;
	TMap<FString, FString> TrackResultSummaries;
	TSet<FString> TracksWithComparableResults;
    IConsoleObject* NetworkLaunchCommand = nullptr;
    IConsoleObject* FormalBatchCommand = nullptr;
	IConsoleObject* OpenWorkbenchCommand = nullptr;
	IConsoleObject* ExportComparisonConsoleCommand = nullptr;
    IConsoleObject* CaptureUICommand = nullptr;
    FTSTicker::FDelegateHandle NetworkLaunchTickerHandle;
    FString PendingScreenshotPath;
    double ScreenshotReadyTime = 0.0;
    bool bExitAfterScreenshot = false;
    FString LastLaunchStatus = TEXT("尚未启动实验");
};
