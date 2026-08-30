#pragma once

#include "Async/Future.h"
#include "CoreMinimal.h"
#include "PerformanceExperimentTypes.h"

struct FPerformanceNetworkSessionLaunchRequest
{
    FPerformanceExperimentPresetData Preset = FPerformanceExperimentPresetData::MakeHeroArenaNetwork();
    FString Dimension = TEXT("Network");
    FString Variant = TEXT("Baseline");
    FString SessionId;
    FString SourceRevision;
    int32 ClientCount = 2;
    int32 ServerPort = 7777;
    double ServerReadyTimeoutSeconds = 45.0;
    double ClientJoinGraceSeconds = 15.0;
};

struct FPerformanceNetworkProcessLaunch
{
    FString Role;
    FString RunId;
    FString LogPath;
    uint32 ProcessId = 0;
};

struct FPerformanceNetworkSessionLaunchResult
{
    bool bStarted = false;
    FString SessionId;
    FString Error;
    TArray<FPerformanceNetworkProcessLaunch> Processes;
};

/**
 * Starts a dedicated server first, waits for its actual listening marker, then
 * starts the requested clients. Only processes created by this operation are
 * terminated when launch fails.
 */
class FPerformanceNetworkSessionLauncher
{
public:
    static TFuture<FPerformanceNetworkSessionLaunchResult> LaunchAsync(FPerformanceNetworkSessionLaunchRequest Request);
};
