#include "PerformanceNetworkSessionLauncher.h"

#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PerformanceExperimentLauncher.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
    bool IsCancelled(const FPerformanceNetworkSessionLaunchRequest& Request)
    {
        return Request.CancelRequested.IsValid() && Request.CancelRequested->Load();
    }

    void TerminateOwnedProcesses(const TArray<FPerformanceNetworkProcessLaunch>& Processes)
    {
        for (const FPerformanceNetworkProcessLaunch& Process : Processes)
        {
            if (Process.ProcessId == 0)
            {
                continue;
            }
            FProcHandle Handle = FPlatformProcess::OpenProcess(Process.ProcessId);
            if (Handle.IsValid())
            {
                if (FPlatformProcess::IsProcRunning(Handle))
                {
                    FPlatformProcess::TerminateProc(Handle, true);
                }
                FPlatformProcess::CloseProc(Handle);
            }
        }
    }

    bool IsProcessRunning(uint32 ProcessId)
    {
        FProcHandle Handle = FPlatformProcess::OpenProcess(ProcessId);
        if (!Handle.IsValid())
        {
            return false;
        }
        const bool bRunning = FPlatformProcess::IsProcRunning(Handle);
        FPlatformProcess::CloseProc(Handle);
        return bRunning;
    }

    bool WaitForServerListening(const FPerformanceNetworkProcessLaunch& Server, int32 Port, double TimeoutSeconds,
        const FPerformanceNetworkSessionLaunchRequest& Request)
    {
        const FString Marker = FString::Printf(TEXT("listening on port %d"), Port);
        const FString ReadyPath = FPaths::Combine(
            FPaths::ProjectSavedDir(), TEXT("PerformanceLab"), TEXT("Runs"), Server.RunId, TEXT("network-ready.txt"));
        const double Deadline = FPlatformTime::Seconds() + FMath::Max(1.0, TimeoutSeconds);
        while (FPlatformTime::Seconds() < Deadline)
        {
            if (IsCancelled(Request))
            {
                return false;
            }
            if (IFileManager::Get().FileSize(*ReadyPath) > 0)
            {
                return true;
            }

            // Keep the marker as a secondary diagnostic path when the ready
            // artifact cannot be written, for example due to filesystem policy.
            FString LogText;
            if (FFileHelper::LoadFileToString(LogText, *Server.LogPath) && LogText.Contains(Marker, ESearchCase::IgnoreCase))
            {
                return true;
            }
            if (!IsProcessRunning(Server.ProcessId))
            {
                return false;
            }
            FPlatformProcess::Sleep(0.25f);
        }
        return false;
    }

    bool ReadFinalSummary(const FString& RunId, bool& bOutValid)
    {
        bOutValid = false;
        FString Json;
        TSharedPtr<FJsonObject> Root;
        const FString SummaryPath = FPaths::Combine(
            FPaths::ProjectSavedDir(), TEXT("PerformanceLab"), TEXT("Runs"), RunId, TEXT("summary.json"));
        if (!FFileHelper::LoadFileToString(Json, *SummaryPath)
            || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root)
            || !Root.IsValid())
        {
            return false;
        }
        bool bFinal = false;
        bool bArtifactsFinalized = false;
        Root->TryGetBoolField(TEXT("final"), bFinal);
        Root->TryGetBoolField(TEXT("artifactsFinalized"), bArtifactsFinalized);
        Root->TryGetBoolField(TEXT("valid"), bOutValid);
        return bFinal && bArtifactsFinalized;
    }

    void WaitForSessionCompletion(const FPerformanceNetworkSessionLaunchRequest& Request,
        FPerformanceNetworkSessionLaunchResult& Result)
    {
        const double CompletionTimeoutSeconds = Request.ServerReadyTimeoutSeconds
            + Request.Preset.Capture.WarmupSeconds + Request.Preset.Capture.DurationSeconds
            + Request.ClientJoinGraceSeconds + 45.0;
        const double Deadline = FPlatformTime::Seconds() + CompletionTimeoutSeconds;
        TMap<uint32, double> StoppedWithoutSummarySince;

        while (FPlatformTime::Seconds() < Deadline)
        {
            if (IsCancelled(Request))
            {
                Result.bCancelled = true;
                Result.Error = TEXT("网络采集已停止");
                return;
            }

            bool bAllFinal = true;
            bool bAnyStoppedWithoutSummary = false;
            Result.InvalidProcessRoles.Reset();
            for (const FPerformanceNetworkProcessLaunch& Process : Result.Processes)
            {
                bool bValid = false;
                if (ReadFinalSummary(Process.RunId, bValid))
                {
                    if (!bValid)
                    {
                        Result.InvalidProcessRoles.Add(Process.Role);
                    }
                    continue;
                }

                bAllFinal = false;
                if (!IsProcessRunning(Process.ProcessId))
                {
                    double& StoppedAt = StoppedWithoutSummarySince.FindOrAdd(Process.ProcessId);
                    if (StoppedAt <= 0.0)
                    {
                        StoppedAt = FPlatformTime::Seconds();
                    }
                    bAnyStoppedWithoutSummary |= FPlatformTime::Seconds() - StoppedAt >= 3.0;
                }
            }

            if (bAllFinal)
            {
                Result.bCompleted = true;
                Result.bAllSummariesValid = Result.InvalidProcessRoles.IsEmpty();
                return;
            }
            if (bAnyStoppedWithoutSummary)
            {
                Result.Error = TEXT("网络进程在写出 summary.json 前退出");
                return;
            }
            FPlatformProcess::Sleep(0.25f);
        }
        Result.Error = TEXT("网络采集在时限内未完成");
    }

    FPerformanceNetworkSessionLaunchResult LaunchBlocking(FPerformanceNetworkSessionLaunchRequest Request)
    {
        FPerformanceNetworkSessionLaunchResult Result;
        Request.Preset.Normalize();
        Request.ClientCount = FMath::Clamp(Request.ClientCount, 1, 8);
        Request.ServerPort = FMath::Clamp(Request.ServerPort, 1024, 65535);
        if (Request.SessionId.IsEmpty())
        {
            Request.SessionId = FString::Printf(TEXT("%s-network-%s"), *FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S")), *Request.Variant);
        }
        if (Request.SourceRevision.IsEmpty())
        {
            Request.SourceRevision = FPerformanceExperimentLauncher::ReadSourceRevision();
        }
        Result.SessionId = Request.SessionId;

        const FString LogDirectory = FPaths::Combine(FPaths::ProjectLogDir(), TEXT("PerformanceWorkbench"));
        IFileManager::Get().MakeDirectory(*LogDirectory, true);

        FPerformanceExperimentLaunchRequest ServerRequest;
        ServerRequest.Preset = Request.Preset;
        ServerRequest.Preset.Capture.DurationSeconds = FMath::Max(
            ServerRequest.Preset.Capture.DurationSeconds,
            Request.Preset.Capture.DurationSeconds + FMath::Max(5.0, Request.ClientJoinGraceSeconds));
        ServerRequest.Dimension = Request.Dimension;
        ServerRequest.Variant = Request.Variant;
        ServerRequest.RunId = Request.SessionId + TEXT("-server");
        ServerRequest.SessionId = Request.SessionId;
        ServerRequest.ProcessRole = TEXT("Server");
        ServerRequest.SourceRevision = Request.SourceRevision;
        ServerRequest.TargetConnectionCount = Request.ClientCount;
        ServerRequest.ServerPort = Request.ServerPort;
        ServerRequest.bDedicatedServer = true;
        ServerRequest.bNullRHI = true;
        ServerRequest.bAutoExit = false;
        ServerRequest.AbsoluteLogPath = FPaths::Combine(LogDirectory, ServerRequest.RunId + TEXT(".log"));

        const FPerformanceExperimentLaunchResult ServerLaunch = FPerformanceExperimentLauncher::Launch(ServerRequest);
        if (!ServerLaunch.bStarted)
        {
            Result.Error = ServerLaunch.Error;
            return Result;
        }
        Result.Processes.Add({ TEXT("Server"), ServerLaunch.RunId, ServerRequest.AbsoluteLogPath, ServerLaunch.ProcessId });

        if (!WaitForServerListening(Result.Processes[0], Request.ServerPort, Request.ServerReadyTimeoutSeconds, Request))
        {
            Result.bCancelled = IsCancelled(Request);
            Result.Error = Result.bCancelled ? TEXT("网络采集已停止") : TEXT("服务器未在时限内进入监听状态");
            TerminateOwnedProcesses(Result.Processes);
            return Result;
        }

        for (int32 ClientIndex = 1; ClientIndex <= Request.ClientCount; ++ClientIndex)
        {
            if (IsCancelled(Request))
            {
                Result.bCancelled = true;
                Result.Error = TEXT("网络采集已停止");
                TerminateOwnedProcesses(Result.Processes);
                return Result;
            }
            FPerformanceExperimentLaunchRequest ClientRequest;
            ClientRequest.Preset = Request.Preset;
            ClientRequest.Dimension = Request.Dimension;
            ClientRequest.Variant = Request.Variant;
            ClientRequest.RunId = FString::Printf(TEXT("%s-client%d"), *Request.SessionId, ClientIndex);
            ClientRequest.SessionId = Request.SessionId;
            ClientRequest.ProcessRole = FString::Printf(TEXT("Client%d"), ClientIndex);
            ClientRequest.SourceRevision = Request.SourceRevision;
            ClientRequest.TargetOverride = FString::Printf(TEXT("127.0.0.1:%d"), Request.ServerPort);
            ClientRequest.TargetConnectionCount = 1;
            ClientRequest.bNullRHI = true;
            ClientRequest.bIncludeExperience = false;
            ClientRequest.AbsoluteLogPath = FPaths::Combine(LogDirectory, ClientRequest.RunId + TEXT(".log"));

            const FPerformanceExperimentLaunchResult ClientLaunch = FPerformanceExperimentLauncher::Launch(ClientRequest);
            if (!ClientLaunch.bStarted)
            {
                Result.Error = FString::Printf(TEXT("客户端 %d 启动失败，已停止本次创建的进程"), ClientIndex);
                TerminateOwnedProcesses(Result.Processes);
                return Result;
            }
            Result.Processes.Add({ ClientRequest.ProcessRole, ClientLaunch.RunId, ClientRequest.AbsoluteLogPath, ClientLaunch.ProcessId });
        }

        Result.bStarted = true;
        WaitForSessionCompletion(Request, Result);
        TerminateOwnedProcesses(Result.Processes);
        return Result;
    }
}

TFuture<FPerformanceNetworkSessionLaunchResult> FPerformanceNetworkSessionLauncher::LaunchAsync(FPerformanceNetworkSessionLaunchRequest Request)
{
    return Async(EAsyncExecution::Thread, [Request = MoveTemp(Request)]() mutable
    {
        return LaunchBlocking(MoveTemp(Request));
    });
}
