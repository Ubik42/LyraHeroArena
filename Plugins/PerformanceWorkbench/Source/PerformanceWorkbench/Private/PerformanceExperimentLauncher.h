#pragma once

#include "CoreMinimal.h"
#include "PerformanceExperimentTypes.h"

struct FPerformanceExperimentLaunchRequest
{
    FPerformanceExperimentPresetData Preset;
    FString Dimension;
    FString Variant = TEXT("Baseline");
    FString RunId;
    FString SessionId;
    FString ProcessRole = TEXT("Standalone");
    FString SourceRevision;
    FString TargetOverride;
    FString AbsoluteLogPath;
    int32 TargetConnectionCount = 0;
    int32 ServerPort = 0;
    bool bAutoExit = true;
    bool bDedicatedServer = false;
    bool bNullRHI = false;
    bool bIncludeExperience = true;
    bool bCaptureGpuPasses = true;
};

struct FPerformanceExperimentLaunchResult
{
    bool bStarted = false;
    uint32 ProcessId = 0;
    FString RunId;
    FString Arguments;
    FString Error;
};

class FPerformanceExperimentLauncher
{
public:
    static FPerformanceExperimentLaunchResult Launch(FPerformanceExperimentLaunchRequest Request);
    static FString BuildArguments(const FPerformanceExperimentLaunchRequest& Request);
    static FString ReadSourceRevision();
};
