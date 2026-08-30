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

class FPerformanceWorkbenchModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    TSharedRef<SDockTab> SpawnWorkbenchTab(const FSpawnTabArgs& Args);
    void RegisterMenus();
    void LaunchExperiment(const FString& Dimension, const FString& Variant);
    void LaunchNetworkSession(const TArray<FString>& Arguments);
    void CaptureWorkbenchUI(const TArray<FString>& Arguments);
    bool PollNetworkLaunch(float DeltaSeconds);
    void OpenResultsDirectory() const;
    void RefreshComparisons();
    void RefreshRecentRuns();
    TSharedRef<SWidget> BuildExperimentRow(const FText& Title, const FText& Detail, const FString& Dimension);

    TSharedPtr<SVerticalBox> RecentRunsBox;
    TSharedPtr<SVerticalBox> ComparisonBox;
    TSharedPtr<SScrollBox> MainScrollBox;
    TWeakPtr<SDockTab> WorkbenchTab;
    TUniquePtr<FPerformanceWorkbenchNetworkLaunchState> NetworkLaunchState;
    IConsoleObject* NetworkLaunchCommand = nullptr;
    IConsoleObject* CaptureUICommand = nullptr;
    FTSTicker::FDelegateHandle NetworkLaunchTickerHandle;
    FString PendingScreenshotPath;
    double ScreenshotReadyTime = 0.0;
    FString LastLaunchStatus = TEXT("尚未启动实验");
};
