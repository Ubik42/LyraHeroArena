#include "PerformanceExperimentLauncher.h"

#include "HAL/PlatformProcess.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"

namespace
{
    FString Quote(const FString& Value)
    {
        return FString::Printf(TEXT("\"%s\""), *Value.ReplaceCharWithEscapedChar());
    }
}

FPerformanceExperimentLaunchResult FPerformanceExperimentLauncher::Launch(FPerformanceExperimentLaunchRequest Request)
{
    Request.Preset.Normalize();
    if (Request.RunId.IsEmpty())
    {
        Request.RunId = FString::Printf(TEXT("%s-%s-%s"), *FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S")), *Request.Dimension, *Request.Variant);
    }
    if (Request.SourceRevision.IsEmpty())
    {
        Request.SourceRevision = ReadSourceRevision();
    }

    FPerformanceExperimentLaunchResult Result;
    Result.RunId = Request.RunId;
    Result.Arguments = BuildArguments(Request);
    const FString Executable = FPlatformProcess::ExecutablePath();
	const FProcHandle Handle = FPlatformProcess::CreateProc(*Executable, *Result.Arguments, true, true, true, &Result.ProcessId, 0, *FPaths::ProjectDir(), nullptr);
    Result.bStarted = Handle.IsValid();
    if (!Result.bStarted)
    {
        Result.Error = TEXT("无法创建独立 Unreal 运行进程");
    }
    return Result;
}

FString FPerformanceExperimentLauncher::BuildArguments(const FPerformanceExperimentLaunchRequest& Request)
{
    FString MapTarget = Request.TargetOverride.IsEmpty() ? Request.Preset.Target.MapPath : Request.TargetOverride;
    FString MapOptions = Request.Preset.Load.MapOptions;
    if (Request.TargetOverride.IsEmpty() && Request.Preset.Load.AgentCount > 0 && MapOptions.IsEmpty())
    {
        MapOptions = FString::Printf(TEXT("NumBots=%d"), Request.Preset.Load.AgentCount);
    }
    if (!MapOptions.IsEmpty())
    {
        MapTarget += TEXT("?") + MapOptions;
    }

    FString Arguments = FString::Printf(
        TEXT("%s %s %s -unattended -nosplash -nocrashreports -NoCrashDialog -NoPause -nop4 -PerfLab -PerfLabAgents=%d -PerfLabSeed=%d -PerfLabWarmup=%.3f -PerfLabDuration=%.3f -PerfLabReadyTimeout=%.3f -PerfLabRunId=%s -PerfLabSessionId=%s -PerfLabProcessRole=%s -PerfLabConnections=%d -PerfLabPresetId=%s -PerfLabDimension=%s -PerfLabVariant=%s -PerfLabSourceRevision=%s"),
        *Quote(FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath())),
        *MapTarget,
        Request.bDedicatedServer ? TEXT("-server") : TEXT("-game"),
        Request.Preset.Load.AgentCount,
        Request.Preset.Load.Seed,
        Request.Preset.Capture.WarmupSeconds,
        Request.Preset.Capture.DurationSeconds,
        Request.Preset.Capture.ReadyTimeoutSeconds,
        *Quote(Request.RunId),
        *Quote(Request.SessionId.IsEmpty() ? Request.RunId : Request.SessionId),
        *Quote(Request.ProcessRole),
        FMath::Max(0, Request.TargetConnectionCount),
        *Quote(Request.Preset.PresetId),
        *Quote(Request.Dimension),
        *Quote(Request.Variant),
        *Quote(Request.SourceRevision));

    if (Request.bIncludeExperience && !Request.Preset.Target.ExperienceId.IsEmpty())
    {
        Arguments += TEXT(" -Experience=") + Quote(Request.Preset.Target.ExperienceId);
    }
    if (Request.Preset.Capture.bWindowed)
    {
        Arguments += FString::Printf(TEXT(" -windowed -ResX=%d -ResY=%d"), Request.Preset.Capture.ResolutionX, Request.Preset.Capture.ResolutionY);
    }
    if (Request.bAutoExit)
    {
        Arguments += TEXT(" -PerfLabAutoExit");
    }
    if (Request.bNullRHI)
    {
        Arguments += TEXT(" -nullrhi -NoSound");
    }
	else if (Request.bCaptureGpuPasses)
	{
		Arguments += TEXT(" -csvGpuStats -RenderOffscreen -NoSound");
	}
    if (Request.ServerPort > 0)
    {
        Arguments += FString::Printf(TEXT(" -port=%d"), Request.ServerPort);
    }
    if (!Request.AbsoluteLogPath.IsEmpty())
    {
        Arguments += TEXT(" -abslog=") + Quote(Request.AbsoluteLogPath);
    }
    if (!Request.Preset.ExtraCommandLine.IsEmpty())
    {
        Arguments += TEXT(" ") + Request.Preset.ExtraCommandLine;
    }
    return Arguments;
}

FString FPerformanceExperimentLauncher::ReadSourceRevision()
{
    FString Output;
    FString Error;
    int32 ReturnCode = INDEX_NONE;
    const FString Parameters = FString::Printf(TEXT("-C %s rev-parse --short HEAD"), *Quote(FPaths::ProjectDir()));
    if (FPlatformProcess::ExecProcess(TEXT("git"), *Parameters, &ReturnCode, &Output, &Error) && ReturnCode == 0)
    {
        Output.TrimStartAndEndInline();
        return Output;
    }
    return TEXT("unknown");
}
