#include "PerformanceResultAggregator.h"

#include "PerformanceComparisonContract.h"

#include "Algo/AllOf.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
    struct FMetricValue
    {
        double Mean = 0.0;
        double P95 = 0.0;
        double Min = 0.0;
        double Max = 0.0;
        bool bPresent = false;
    };

    struct FRunSummary
    {
        FString RunId;
        FString SessionId;
        FString ProcessRole;
        FString PresetId;
        FString Dimension;
        FString Variant;
        FString SourceRevision;
        FString EngineVersion;
        FString StartedUtc;
        FString Map;
        FString RHI;
        FString ContractKey;
        TSharedPtr<FJsonObject> RawSummary;
        int32 Seed = 0;
        int32 TargetAgents = 0;
        int32 TargetFixtures = 0;
        int32 TargetPhysicsBodies = 0;
        int32 TargetNiagaraStatusSlots = 0;
        int32 ConnectionCount = 0;
        int32 ResolutionX = 0;
        int32 ResolutionY = 0;
        double WarmupSeconds = 0.0;
        double MeasurementSeconds = 0.0;
        double NiagaraBurstsPerSecond = 0.0;
        bool bWindowed = false;
        bool bNormalRHI = false;
        bool bInitialViewLocked = false;
        bool bValid = false;
        bool bRequiredGuardsPassing = true;
        TMap<FString, FMetricValue> Metrics;
    };

    struct FMetricDefinition
    {
        const TCHAR* Key;
        const TCHAR* Aggregate;
        const TCHAR* Label;
        const TCHAR* Unit;
        bool bLowerIsBetter;
    };

    FString GetString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, const FString& Default = FString())
    {
        FString Value;
        return Object.IsValid() && Object->TryGetStringField(Field, Value) ? Value : Default;
    }

    double GetNumber(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, double Default = 0.0)
    {
        double Value = Default;
        return Object.IsValid() && Object->TryGetNumberField(Field, Value) ? Value : Default;
    }

    bool GetBool(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, bool Default = false)
    {
        bool Value = Default;
        return Object.IsValid() && Object->TryGetBoolField(Field, Value) ? Value : Default;
    }

    bool LoadRun(const FString& SummaryPath, FRunSummary& OutRun)
    {
        FString Json;
        TSharedPtr<FJsonObject> Root;
        if (!FFileHelper::LoadFileToString(Json, *SummaryPath)
            || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root)
            || !Root.IsValid())
        {
            return false;
        }

        OutRun.RunId = GetString(Root, TEXT("runId"));
        OutRun.SessionId = GetString(Root, TEXT("sessionId"));
        OutRun.ProcessRole = GetString(Root, TEXT("processRole"), TEXT("Standalone"));
        if (OutRun.ProcessRole.IsEmpty())
        {
            OutRun.ProcessRole = TEXT("Standalone");
        }
        OutRun.PresetId = GetString(Root, TEXT("presetId"));
        OutRun.Dimension = GetString(Root, TEXT("dimension"));
        OutRun.Variant = GetString(Root, TEXT("variant"));
        OutRun.SourceRevision = GetString(Root, TEXT("sourceRevision"));
        OutRun.EngineVersion = GetString(Root, TEXT("engineVersion"));
        OutRun.StartedUtc = GetString(Root, TEXT("startedUtc"), OutRun.RunId);
        OutRun.Map = GetString(Root, TEXT("map"));
        OutRun.RHI = GetString(Root, TEXT("rhiName"));
        OutRun.ContractKey = PerformanceComparisonContract::BuildKey(Root);
        OutRun.RawSummary = Root;
        OutRun.Seed = FMath::RoundToInt(GetNumber(Root, TEXT("seed")));
        OutRun.TargetAgents = FMath::RoundToInt(GetNumber(Root, TEXT("targetAgentCount")));
        OutRun.TargetFixtures = FMath::RoundToInt(GetNumber(Root, TEXT("targetReplicatedFixtureCount")));
        OutRun.TargetPhysicsBodies = FMath::RoundToInt(GetNumber(Root, TEXT("targetPhysicsBodyCount")));
        OutRun.TargetNiagaraStatusSlots = FMath::RoundToInt(GetNumber(Root, TEXT("targetNiagaraStatusSlotCount")));
        OutRun.WarmupSeconds = GetNumber(Root, TEXT("warmupSeconds"));
        OutRun.MeasurementSeconds = GetNumber(Root, TEXT("measurementSeconds"));
        OutRun.NiagaraBurstsPerSecond = GetNumber(Root, TEXT("niagaraBurstsPerSecond"));
        OutRun.bNormalRHI = GetBool(Root, TEXT("normalRHI"));
        OutRun.bInitialViewLocked = GetBool(Root, TEXT("initialViewLocked"));
        OutRun.bValid = GetBool(Root, TEXT("valid"));

        const TSharedPtr<FJsonObject>* Environment = nullptr;
        if (Root->TryGetObjectField(TEXT("environment"), Environment) && Environment && Environment->IsValid())
        {
            OutRun.ResolutionX = FMath::RoundToInt(GetNumber(*Environment, TEXT("resolutionX")));
            OutRun.ResolutionY = FMath::RoundToInt(GetNumber(*Environment, TEXT("resolutionY")));
            OutRun.bWindowed = GetBool(*Environment, TEXT("windowed"));
        }

        const TArray<TSharedPtr<FJsonValue>>* Guards = nullptr;
        if (Root->TryGetArrayField(TEXT("guards"), Guards) && Guards)
        {
            for (const TSharedPtr<FJsonValue>& GuardValue : *Guards)
            {
                const TSharedPtr<FJsonObject> Guard = GuardValue.IsValid() ? GuardValue->AsObject() : nullptr;
                if (!Guard.IsValid())
                {
                    continue;
                }
                const bool bRequired = GetBool(Guard, TEXT("required"), true);
                const bool bPassed = GetBool(Guard, TEXT("passed"));
                OutRun.bRequiredGuardsPassing &= !bRequired || bPassed;
                if (GetString(Guard, TEXT("id")) == TEXT("network-connections"))
                {
                    OutRun.ConnectionCount = FMath::RoundToInt(GetNumber(Guard, TEXT("actual")));
                }
            }
        }

        const TSharedPtr<FJsonObject>* Metrics = nullptr;
        if (Root->TryGetObjectField(TEXT("metrics"), Metrics) && Metrics && Metrics->IsValid())
        {
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Metrics)->Values)
            {
                const TSharedPtr<FJsonObject> MetricObject = Pair.Value.IsValid() ? Pair.Value->AsObject() : nullptr;
                if (!MetricObject.IsValid())
                {
                    continue;
                }
                FMetricValue Metric;
                Metric.Mean = GetNumber(MetricObject, TEXT("mean"));
                Metric.P95 = GetNumber(MetricObject, TEXT("p95"));
                Metric.Min = GetNumber(MetricObject, TEXT("min"));
                Metric.Max = GetNumber(MetricObject, TEXT("max"));
                Metric.bPresent = true;
                OutRun.Metrics.Add(Pair.Key, Metric);
            }
        }
        return !OutRun.RunId.IsEmpty() && !OutRun.PresetId.IsEmpty();
    }

    FString MakeContractKey(const FRunSummary& Run)
    {
        return Run.ContractKey;
    }

    FString NormalizeComparisonBatchId(FString BatchId)
    {
        const int32 RunMarker = BatchId.Find(TEXT("-run-"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
        if (RunMarker != INDEX_NONE)
        {
            BatchId.LeftInline(RunMarker);
        }
        return BatchId;
    }

    FString MakeComparisonBatchId(const FRunSummary& Run)
    {
        return NormalizeComparisonBatchId(Run.SessionId.IsEmpty() ? Run.RunId : Run.SessionId);
    }

    FString MakeComparisonBatchKey(const FRunSummary& Run)
    {
        const FString BatchId = MakeComparisonBatchId(Run);
        return MakeContractKey(Run) + TEXT("\n") + BatchId;
    }

    double Median(TArray<double> Values)
    {
        if (Values.IsEmpty())
        {
            return 0.0;
        }
        Values.Sort();
        const int32 Middle = Values.Num() / 2;
        return Values.Num() % 2 == 1 ? Values[Middle] : (Values[Middle - 1] + Values[Middle]) * 0.5;
    }

    TArray<double> MetricSamples(const TArray<const FRunSummary*>& Runs, const TCHAR* Key,
        const TCHAR* Aggregate, bool& bOutPresent)
    {
        TArray<double> Values;
        for (const FRunSummary* Run : Runs)
        {
            if (const FMetricValue* Metric = Run->Metrics.Find(Key); Metric && Metric->bPresent)
            {
                if (FCString::Stricmp(Aggregate, TEXT("p95")) == 0) Values.Add(Metric->P95);
                else if (FCString::Stricmp(Aggregate, TEXT("min")) == 0) Values.Add(Metric->Min);
                else if (FCString::Stricmp(Aggregate, TEXT("max")) == 0) Values.Add(Metric->Max);
                else Values.Add(Metric->Mean);
            }
        }
        bOutPresent = Values.Num() == Runs.Num() && !Values.IsEmpty();
        return Values;
    }

    double MetricMedian(const TArray<const FRunSummary*>& Runs, const TCHAR* Key, const TCHAR* Aggregate, bool& bOutPresent)
    {
        return Median(MetricSamples(Runs, Key, Aggregate, bOutPresent));
    }

    TArray<FMetricDefinition> GetMetricDefinitions(const FString& Dimension)
    {
        if (Dimension == TEXT("Rendering"))
        {
            return {
                { TEXT("gpuTimeMs"), TEXT("mean"), TEXT("GPU 平均"), TEXT("ms"), true },
                { TEXT("gpuTimeMs"), TEXT("p95"), TEXT("GPU P95"), TEXT("ms"), true },
                { TEXT("csvGpuShadowDepthsTimeMs"), TEXT("mean"), TEXT("GPU 阴影深度平均"), TEXT("ms"), true },
                { TEXT("csvGpuTemporalSuperResolutionTimeMs"), TEXT("mean"), TEXT("GPU TSR 平均"), TEXT("ms"), true },
                { TEXT("csvGpuLumenReflectionsTimeMs"), TEXT("mean"), TEXT("GPU Lumen 反射平均"), TEXT("ms"), true },
                { TEXT("csvGpuNaniteVisBufferTimeMs"), TEXT("mean"), TEXT("GPU Nanite 可见性平均"), TEXT("ms"), true },
                { TEXT("csvGpuCustomDepthTimeMs"), TEXT("mean"), TEXT("GPU Custom Depth 平均"), TEXT("ms"), true },
				{ TEXT("csvGpuRayTracingDynamicGeometryTimeMs"), TEXT("mean"), TEXT("GPU 光追动态几何平均"), TEXT("ms"), true },
                { TEXT("renderThreadTimeMs"), TEXT("mean"), TEXT("Render Thread 平均"), TEXT("ms"), true },
                { TEXT("renderThreadTimeMs"), TEXT("p95"), TEXT("Render Thread P95"), TEXT("ms"), true },
                { TEXT("rhiThreadTimeMs"), TEXT("mean"), TEXT("RHI Thread 平均"), TEXT("ms"), true },
                { TEXT("drawCalls"), TEXT("mean"), TEXT("Draw Call 平均"), TEXT("次/帧"), true },
                { TEXT("customDepthCharacters"), TEXT("mean"), TEXT("Custom Depth 角色平均"), TEXT("个"), true },
                { TEXT("shadowCastingCharacters"), TEXT("mean"), TEXT("投影角色平均"), TEXT("个"), true }
            };
        }
        if (Dimension == TEXT("Network"))
        {
            return {
                { TEXT("netOutBytesPerSecond"), TEXT("mean"), TEXT("服务器出站平均"), TEXT("B/s"), true },
                { TEXT("netOutBytesPerSecond"), TEXT("p95"), TEXT("服务器出站 P95"), TEXT("B/s"), true },
                { TEXT("netReplicatedActors"), TEXT("mean"), TEXT("复制 Actor 平均"), TEXT("个/帧"), true },
                { TEXT("netReplicatedActorBytes"), TEXT("mean"), TEXT("Actor 复制字节平均"), TEXT("B/帧"), true },
                { TEXT("netPrioritizedActors"), TEXT("mean"), TEXT("优先级 Actor 平均"), TEXT("个/帧"), true },
                { TEXT("netServerReplicateActorTimeMs"), TEXT("mean"), TEXT("复制阶段平均"), TEXT("ms"), true },
                { TEXT("netServerReplicateActorTimeMs"), TEXT("p95"), TEXT("复制阶段 P95"), TEXT("ms"), true },
                { TEXT("netConsideredActors"), TEXT("mean"), TEXT("候选 Actor 平均"), TEXT("个/帧"), true }
            };
        }
        if (Dimension == TEXT("AIDecision"))
        {
            return {
                { TEXT("crowdDecisionWorkTimeMs"), TEXT("mean"), TEXT("目标发现决策平均"), TEXT("ms/帧"), true },
                { TEXT("crowdDecisionWorkTimeMs"), TEXT("p95"), TEXT("目标发现决策 P95"), TEXT("ms/帧"), true },
                { TEXT("crowdCandidateChecksPerFrame"), TEXT("mean"), TEXT("候选检查平均"), TEXT("次/帧"), true },
                { TEXT("crowdCandidateChecksPerFrame"), TEXT("p95"), TEXT("候选检查 P95"), TEXT("次/帧"), true },
                { TEXT("crowdDecisionCountPerFrame"), TEXT("mean"), TEXT("状态机决策平均"), TEXT("次/帧"), false },
                { TEXT("crowdTargetedAgentCount"), TEXT("mean"), TEXT("已获取目标角色"), TEXT("个"), false },
                { TEXT("crowdChasingAgentCount"), TEXT("mean"), TEXT("追击状态角色"), TEXT("个"), false },
                { TEXT("crowdEngagingAgentCount"), TEXT("mean"), TEXT("交战状态角色"), TEXT("个"), false },
                { TEXT("csvTickActorsTimeMs"), TEXT("mean"), TEXT("TickActors 平均"), TEXT("ms"), true },
                { TEXT("csvTickActorsTimeMs"), TEXT("p95"), TEXT("TickActors P95"), TEXT("ms"), true },
                { TEXT("frameTimeMs"), TEXT("p95"), TEXT("帧时 P95"), TEXT("ms"), true }
            };
        }
        if (Dimension == TEXT("Navigation"))
        {
            return {
                { TEXT("navDecisionWorkTimeMs"), TEXT("mean"), TEXT("寻路决策平均"), TEXT("ms/帧"), true },
                { TEXT("navPathRequestsPerFrame"), TEXT("mean"), TEXT("路径请求"), TEXT("次/帧"), true },
                { TEXT("navReplansPerFrame"), TEXT("mean"), TEXT("重新规划"), TEXT("次/帧"), true },
				{ TEXT("navMaxPendingWaitMs"), TEXT("p95"), TEXT("请求等待 P95"), TEXT("ms"), true },
                { TEXT("navMovingAgentCount"), TEXT("mean"), TEXT("移动角色平均"), TEXT("个"), false },
                { TEXT("navStuckAgentCount"), TEXT("mean"), TEXT("持续卡住平均"), TEXT("个"), true },
                { TEXT("frameTimeMs"), TEXT("p95"), TEXT("帧时 P95"), TEXT("ms"), true }
            };
        }
        if (Dimension == TEXT("AnimationLOD"))
        {
            return {
                { TEXT("csvAnimationTimeMs"), TEXT("mean"), TEXT("Animation 平均"), TEXT("ms"), true },
                { TEXT("csvAnimationTimeMs"), TEXT("p95"), TEXT("Animation P95"), TEXT("ms"), true },
                { TEXT("csvSignificanceUpdateTimeMs"), TEXT("mean"), TEXT("重要度更新平均"), TEXT("ms"), true },
                { TEXT("animationFullRateProtectedCount"), TEXT("mean"), TEXT("全帧率保护角色"), TEXT("个"), false },
				{ TEXT("animationUROEnabledCount"), TEXT("mean"), TEXT("URO 角色平均"), TEXT("个"), false },
                { TEXT("animationFarFieldCount"), TEXT("mean"), TEXT("远景骨骼 LOD 角色"), TEXT("个"), false },
				{ TEXT("animationPostProcessDisabledCount"), TEXT("mean"), TEXT("省略后处理动画角色"), TEXT("个"), false },
				{ TEXT("navMovingAgentCount"), TEXT("mean"), TEXT("持续移动角色"), TEXT("个"), false },
                { TEXT("frameTimeMs"), TEXT("p95"), TEXT("帧时 P95"), TEXT("ms"), true }
            };
        }
        if (Dimension == TEXT("RenderingLOD"))
        {
            return {
                { TEXT("gpuTimeMs"), TEXT("mean"), TEXT("GPU 平均"), TEXT("ms"), true },
                { TEXT("gpuTimeMs"), TEXT("p95"), TEXT("GPU P95"), TEXT("ms"), true },
                { TEXT("csvGpuShadowDepthsTimeMs"), TEXT("mean"), TEXT("GPU 阴影深度平均"), TEXT("ms"), true },
                { TEXT("csvGpuTemporalSuperResolutionTimeMs"), TEXT("mean"), TEXT("GPU TSR 平均"), TEXT("ms"), true },
                { TEXT("csvGpuLumenReflectionsTimeMs"), TEXT("mean"), TEXT("GPU Lumen 反射平均"), TEXT("ms"), true },
                { TEXT("csvGpuNaniteVisBufferTimeMs"), TEXT("mean"), TEXT("GPU Nanite 可见性平均"), TEXT("ms"), true },
                { TEXT("csvGpuCustomDepthTimeMs"), TEXT("mean"), TEXT("GPU Custom Depth 平均"), TEXT("ms"), true },
				{ TEXT("csvGpuRayTracingDynamicGeometryTimeMs"), TEXT("mean"), TEXT("GPU 光追动态几何平均"), TEXT("ms"), true },
                { TEXT("renderThreadTimeMs"), TEXT("mean"), TEXT("Render Thread 平均"), TEXT("ms"), true },
                { TEXT("drawCalls"), TEXT("mean"), TEXT("Draw Call 平均"), TEXT("次/帧"), true },
                { TEXT("shadowCastingCharacters"), TEXT("mean"), TEXT("投影角色平均"), TEXT("个"), true },
				{ TEXT("reducedAttachedShadowPrimitives"), TEXT("mean"), TEXT("省略阴影的角色附件"), TEXT("个"), false },
				{ TEXT("rayTracingExcludedCharacters"), TEXT("mean"), TEXT("退出光追场景的角色"), TEXT("个"), false },
				{ TEXT("rayTracingExcludedAttachedPrimitives"), TEXT("mean"), TEXT("退出光追场景的外观组件"), TEXT("个"), false },
				{ TEXT("navMovingAgentCount"), TEXT("mean"), TEXT("持续移动角色"), TEXT("个"), false },
                { TEXT("frameTimeMs"), TEXT("p95"), TEXT("帧时 P95"), TEXT("ms"), true }
            };
        }
		if (Dimension == TEXT("Physics"))
		{
			return {
				{ TEXT("csvPhysicsGameThreadTimeMs"), TEXT("mean"), TEXT("物理主线程平均"), TEXT("ms"), true },
				{ TEXT("csvPhysicsGameThreadTimeMs"), TEXT("p95"), TEXT("物理主线程 P95"), TEXT("ms"), true },
				{ TEXT("csvPhysicsAllWorkersTimeMs"), TEXT("mean"), TEXT("物理工作线程平均"), TEXT("ms"), true },
				{ TEXT("csvEndPhysicsWaitTimeMs"), TEXT("p95"), TEXT("等待物理结束 P95"), TEXT("ms"), true },
				{ TEXT("frameTimeMs"), TEXT("p95"), TEXT("帧时 P95"), TEXT("ms"), true }
			};
		}
		if (Dimension == TEXT("Niagara"))
		{
			return {
				{ TEXT("csvNiagaraRenderThreadTimeMs"), TEXT("mean"), TEXT("Niagara 渲染线程平均"), TEXT("ms"), true },
				{ TEXT("csvNiagaraRenderThreadTimeMs"), TEXT("p95"), TEXT("Niagara 渲染线程 P95"), TEXT("ms"), true },
				{ TEXT("csvNiagaraAllWorkersTimeMs"), TEXT("mean"), TEXT("Niagara 工作线程平均"), TEXT("ms"), true },
				{ TEXT("csvFXSystemRenderThreadTimeMs"), TEXT("mean"), TEXT("FXSystem 渲染线程平均"), TEXT("ms"), true },
				{ TEXT("csvFXSystemAllWorkersTimeMs"), TEXT("mean"), TEXT("FXSystem 工作线程平均"), TEXT("ms"), true },
				{ TEXT("gpuTimeMs"), TEXT("p95"), TEXT("GPU P95"), TEXT("ms"), true },
				{ TEXT("frameTimeMs"), TEXT("p95"), TEXT("帧时 P95"), TEXT("ms"), true }
			};
		}
		if (Dimension == TEXT("GameplayTick"))
		{
			return {
				{ TEXT("csvWeaponSpawnerTickCount"), TEXT("mean"), TEXT("拾取点 Tick 平均"), TEXT("次/帧"), true },
				{ TEXT("csvTickActorsTimeMs"), TEXT("mean"), TEXT("TickActors 平均"), TEXT("ms"), true },
				{ TEXT("csvTickActorsTimeMs"), TEXT("p95"), TEXT("TickActors P95"), TEXT("ms"), true },
				{ TEXT("gameThreadTimeMs"), TEXT("mean"), TEXT("Game Thread 平均"), TEXT("ms"), true },
				{ TEXT("frameTimeMs"), TEXT("p95"), TEXT("帧时 P95"), TEXT("ms"), true }
			};
		}
		if (Dimension == TEXT("UI"))
		{
			return {
				{ TEXT("csvHeroArenaHUDRefreshTimeMs"), TEXT("mean"), TEXT("战术 HUD 更新平均"), TEXT("ms/帧"), true },
				{ TEXT("csvHeroArenaHUDRefreshTimeMsPerSecond"), TEXT("mean"), TEXT("战术 HUD 更新累计"), TEXT("ms/s"), true },
				{ TEXT("csvHeroArenaHUDRefreshCountPerSecond"), TEXT("mean"), TEXT("战术 HUD 刷新频率"), TEXT("次/秒"), true },
				{ TEXT("csvUITimeMs"), TEXT("mean"), TEXT("UI 总时间平均"), TEXT("ms"), true },
				{ TEXT("gameThreadTimeMs"), TEXT("mean"), TEXT("Game Thread 平均"), TEXT("ms"), true },
				{ TEXT("frameTimeMs"), TEXT("p95"), TEXT("帧时 P95"), TEXT("ms"), true }
			};
		}
		if (Dimension == TEXT("WeaponVFX"))
		{
			return {
				{ TEXT("niagaraBurstSpawnCount"), TEXT("max"), TEXT("生成组件事件"), TEXT("次"), true },
				{ TEXT("csvNiagaraRenderThreadTimeMs"), TEXT("mean"), TEXT("Niagara 渲染线程平均"), TEXT("ms"), true },
				{ TEXT("csvNiagaraAllWorkersTimeMs"), TEXT("mean"), TEXT("Niagara 工作线程平均"), TEXT("ms"), true },
				{ TEXT("gameThreadTimeMs"), TEXT("mean"), TEXT("Game Thread 平均"), TEXT("ms"), true },
				{ TEXT("frameTimeMs"), TEXT("p95"), TEXT("帧时 P95"), TEXT("ms"), true }
			};
        }
		if (Dimension == TEXT("LiveWeaponVFX"))
		{
			return {
				{ TEXT("csvWeaponVFXPresentationEventsPerSecond"), TEXT("mean"), TEXT("武器表现事件率"), TEXT("次/秒"), false },
				{ TEXT("csvWeaponVFXFullDetailEventsPerSecond"), TEXT("mean"), TEXT("完整表现事件率"), TEXT("次/秒"), false },
				{ TEXT("csvWeaponVFXReducedDetailEventsPerSecond"), TEXT("mean"), TEXT("远距降级事件率"), TEXT("次/秒"), false },
				{ TEXT("csvNiagaraAllWorkersTimeMs"), TEXT("mean"), TEXT("Niagara 工作线程平均"), TEXT("ms"), true },
				{ TEXT("frameTimeMs"), TEXT("p95"), TEXT("帧时 P95"), TEXT("ms"), true }
			};
		}
		if (Dimension == TEXT("GunPadVFX"))
		{
			return {
				{ TEXT("csvGunPadCpuTimeMs"), TEXT("mean"), TEXT("GunPad CPU 累计平均"), TEXT("ms"), true },
				{ TEXT("csvGunPadCpuTimeMs"), TEXT("p95"), TEXT("GunPad CPU 累计 P95"), TEXT("ms"), true },
				{ TEXT("csvGunPadInstanceCount"), TEXT("mean"), TEXT("GunPad 活跃实例"), TEXT("个"), true },
				{ TEXT("csvNiagaraRenderThreadTimeMs"), TEXT("mean"), TEXT("Niagara 渲染线程平均"), TEXT("ms"), true },
				{ TEXT("csvNiagaraAllWorkersTimeMs"), TEXT("mean"), TEXT("Niagara 工作线程平均"), TEXT("ms"), true },
				{ TEXT("frameTimeMs"), TEXT("p95"), TEXT("帧时 P95"), TEXT("ms"), true }
			};
		}
        return {};
    }
}

namespace
{
    struct FRunSummaryCache
    {
        FString RunsRoot;
        TArray<FRunSummary> Runs;
        bool bValid = false;
    };

    FRunSummaryCache GRunSummaryCache;
}

static const TArray<FRunSummary>& LoadRuns(const FString& RunsRoot)
{
    if (GRunSummaryCache.bValid && GRunSummaryCache.RunsRoot == RunsRoot)
    {
        return GRunSummaryCache.Runs;
    }

    TArray<FString> Directories;
    IFileManager::Get().FindFiles(Directories, *FPaths::Combine(RunsRoot, TEXT("*")), false, true);

    GRunSummaryCache.RunsRoot = RunsRoot;
    GRunSummaryCache.Runs.Reset();
    GRunSummaryCache.Runs.Reserve(Directories.Num());
    for (const FString& Directory : Directories)
    {
        FRunSummary Run;
        if (LoadRun(FPaths::Combine(RunsRoot, Directory, TEXT("summary.json")), Run))
        {
            GRunSummaryCache.Runs.Add(MoveTemp(Run));
        }
    }
    GRunSummaryCache.bValid = true;
    return GRunSummaryCache.Runs;
}

static FPerformanceBatchComparisonView BuildLatestComparison(
    const TArray<FRunSummary>& AllRuns,
    const FString& PresetId,
    const FString& Dimension,
    const FString& SelectedGroupId = FString())
{
    FPerformanceBatchComparisonView Result;
    Result.PresetId = PresetId;

    TArray<FRunSummary> Runs;
    const FString SelectedBatchId = NormalizeComparisonBatchId(SelectedGroupId);
    for (const FRunSummary& Run : AllRuns)
    {
        if (Run.PresetId == PresetId
            && (Dimension.IsEmpty() || Run.Dimension == Dimension)
            && (Run.ProcessRole == TEXT("Standalone") || Run.ProcessRole == TEXT("Server"))
            && (SelectedBatchId.IsEmpty() || MakeComparisonBatchId(Run) == SelectedBatchId))
        {
            Runs.Add(Run);
        }
    }

    if (Runs.IsEmpty())
    {
        Result.Title = PresetId;
        Result.StatusSummary = SelectedBatchId.IsEmpty()
            ? TEXT("尚无可读取的正式结果") : TEXT("当前采集批次没有可读取的结果");
        Result.Problems.Add(SelectedBatchId.IsEmpty() ? TEXT("没有找到 summary.json") : TEXT("当前批次没有匹配的 summary.json"));
        return Result;
    }

    TMap<FString, TArray<int32>> ContractGroups;
    TMap<FString, TArray<int32>> BatchGroups;
    for (int32 Index = 0; Index < Runs.Num(); ++Index)
    {
        ContractGroups.FindOrAdd(MakeContractKey(Runs[Index])).Add(Index);
        BatchGroups.FindOrAdd(MakeComparisonBatchKey(Runs[Index])).Add(Index);
    }

    auto SelectBestGroup = [&Runs](const TMap<FString, TArray<int32>>& CandidateGroups, bool bRequireCompleteBatch)
    {
        TArray<int32> SelectedIndices;
        FString SelectedLatest;
        int32 SelectedPairCount = -1;
        for (const TPair<FString, TArray<int32>>& Group : CandidateGroups)
        {
            int32 BaselineCount = 0;
            int32 CandidateCount = 0;
            FString Latest;
            for (int32 Index : Group.Value)
            {
                BaselineCount += Runs[Index].Variant == TEXT("Baseline") ? 1 : 0;
                CandidateCount += Runs[Index].Variant == TEXT("Optimized") ? 1 : 0;
                if (Runs[Index].StartedUtc > Latest)
                {
                    Latest = Runs[Index].StartedUtc;
                }
            }
            const int32 PairCount = FMath::Min(3, FMath::Min(BaselineCount, CandidateCount));
            if (bRequireCompleteBatch && PairCount < 3)
            {
                continue;
            }
            if (SelectedIndices.IsEmpty() || PairCount > SelectedPairCount
                || (PairCount == SelectedPairCount && Latest > SelectedLatest))
            {
                SelectedIndices = Group.Value;
                SelectedLatest = Latest;
                SelectedPairCount = PairCount;
            }
        }
        return SelectedIndices;
    };

    TArray<int32> SelectedIndices;
    bool bSelectedSingleBatch = false;
    if (!SelectedBatchId.IsEmpty())
    {
        for (int32 Index = 0; Index < Runs.Num(); ++Index)
        {
            SelectedIndices.Add(Index);
        }
        bSelectedSingleBatch = true;
    }
    else
    {
        SelectedIndices = SelectBestGroup(BatchGroups, true);
        bSelectedSingleBatch = !SelectedIndices.IsEmpty();
        if (SelectedIndices.IsEmpty())
        {
            SelectedIndices = SelectBestGroup(ContractGroups, false);
        }
    }

    TArray<const FRunSummary*> Baselines;
    TArray<const FRunSummary*> Candidates;
    for (int32 Index : SelectedIndices)
    {
        const FRunSummary& Run = Runs[Index];
        if (Run.Variant == TEXT("Baseline")) Baselines.Add(&Run);
        else if (Run.Variant == TEXT("Optimized")) Candidates.Add(&Run);
    }
    auto SortNewest = [](const FRunSummary& A, const FRunSummary& B) { return A.StartedUtc > B.StartedUtc; };
    Baselines.Sort([&](const FRunSummary& A, const FRunSummary& B) { return SortNewest(A, B); });
    Candidates.Sort([&](const FRunSummary& A, const FRunSummary& B) { return SortNewest(A, B); });
    Baselines.SetNum(FMath::Min(3, Baselines.Num()));
    Candidates.SetNum(FMath::Min(3, Candidates.Num()));

    const FRunSummary& Contract = Baselines.IsEmpty() ? *Candidates[0] : *Baselines[0];
    Result.Dimension = Contract.Dimension;
    Result.SourceRevision = Contract.SourceRevision;
    Result.EngineVersion = Contract.EngineVersion;
    if (Contract.Dimension == TEXT("Rendering")) Result.Title = TEXT("英雄场景 · 角色附加通道 LOD");
    else if (Contract.Dimension == TEXT("Network")) Result.Title = TEXT("多人会话 · Relay 空间相关性");
    else if (Contract.Dimension == TEXT("AIDecision")) Result.Title = TEXT("多角色负载 · AI 状态机与目标发现");
    else if (Contract.Dimension == TEXT("Navigation")) Result.Title = TEXT("多角色负载 · 寻路重规划节流");
    else if (Contract.Dimension == TEXT("AnimationLOD") && PresetId == TEXT("hero-arena-character-rendering")) Result.Title = TEXT("英雄场景 · 动画更新 LOD");
    else if (Contract.Dimension == TEXT("AnimationLOD")) Result.Title = TEXT("多角色负载 · 动画与 IK LOD");
	else if (Contract.Dimension == TEXT("RenderingLOD")) Result.Title = TEXT("多角色负载 · 角色光追场景 LOD");
    else if (Contract.Dimension == TEXT("Physics")) Result.Title = TEXT("受控负载 · 物理距离 LOD");
    else if (Contract.Dimension == TEXT("Niagara")) Result.Title = TEXT("受控负载 · Niagara 持续特效生命周期");
    else if (Contract.Dimension == TEXT("WeaponVFX")) Result.Title = TEXT("受控负载 · 武器表现距离预算");
    else if (Contract.Dimension == TEXT("LiveWeaponVFX")) Result.Title = TEXT("英雄场景 · 真实对局武器表现");
	else if (Contract.Dimension == TEXT("GunPadVFX")) Result.Title = TEXT("英雄场景 · 拾取点常驻特效距离管理");
    else if (Contract.Dimension == TEXT("GameplayTick")) Result.Title = TEXT("英雄场景 · 拾取点更新频率");
	else if (Contract.Dimension == TEXT("UI")) Result.Title = TEXT("英雄场景 · 战术 HUD 更新");
    else Result.Title = Contract.Dimension;
    Result.BaselineRunCount = Baselines.Num();
    Result.CandidateRunCount = Candidates.Num();
    Result.bSingleBatch = bSelectedSingleBatch;
    Result.bAllGuardsPassing = Algo::AllOf(Baselines, [](const FRunSummary* Run) { return Run->bValid && Run->bRequiredGuardsPassing; })
        && Algo::AllOf(Candidates, [](const FRunSummary* Run) { return Run->bValid && Run->bRequiredGuardsPassing; });

    if (Contract.Dimension == TEXT("Rendering"))
    {
        Result.ContractSummary = FString::Printf(TEXT("%s · %d AI · %dx%d · %s · 固定镜头 · Seed %d"),
            *Contract.Map, Contract.TargetAgents, Contract.ResolutionX, Contract.ResolutionY, *Contract.RHI, Contract.Seed);
        Result.QualitySummary = Contract.bNormalRHI && Contract.bInitialViewLocked
            ? TEXT("近场角色保留 Custom Depth；只省略远距离非本地角色的附加通道")
            : TEXT("实验条件不一致：渲染对比需要正常 RHI 与固定镜头");
    }
    else if (Contract.Dimension == TEXT("AIDecision"))
    {
        Result.ContractSummary = FString::Printf(TEXT("%s · %d NPC · 200ms 决策周期 · NullRHI 逻辑实验 · Seed %d"),
            *Contract.Map, Contract.TargetAgents, Contract.Seed);
        bool bBaselineTargetedPresent = false;
        bool bCandidateTargetedPresent = false;
        bool bBaselineChasingPresent = false;
        bool bCandidateChasingPresent = false;
        const double BaselineTargeted = MetricMedian(Baselines, TEXT("crowdTargetedAgentCount"), TEXT("mean"), bBaselineTargetedPresent);
        const double CandidateTargeted = MetricMedian(Candidates, TEXT("crowdTargetedAgentCount"), TEXT("mean"), bCandidateTargetedPresent);
        const double BaselineChasing = MetricMedian(Baselines, TEXT("crowdChasingAgentCount"), TEXT("mean"), bBaselineChasingPresent);
        const double CandidateChasing = MetricMedian(Candidates, TEXT("crowdChasingAgentCount"), TEXT("mean"), bCandidateChasingPresent);
        Result.QualitySummary = (bBaselineTargetedPresent && bCandidateTargetedPresent && bBaselineChasingPresent && bCandidateChasingPresent)
            ? FString::Printf(TEXT("场景状态：目标获取 %.1f → %.1f，追击状态 %.1f → %.1f"),
                BaselineTargeted, CandidateTargeted, BaselineChasing, CandidateChasing)
            : TEXT("场景状态指标尚未写入");
    }
    else if (Contract.Dimension == TEXT("Navigation"))
    {
        Result.ContractSummary = FString::Printf(TEXT("%s · %d 个角色 · 固定路线与 Seed %d"),
            *Contract.Map, Contract.TargetAgents, Contract.Seed);
        Result.QualitySummary = TEXT("同时显示移动、到达、失败与持续卡住状态；这些指标不代替实验条件检查");
    }
    else if (Contract.Dimension == TEXT("AnimationLOD"))
    {
        Result.ContractSummary = FString::Printf(TEXT("%s · %d 个角色 · %dx%d · %s · 固定镜头"),
            *Contract.Map, Contract.TargetAgents, Contract.ResolutionX, Contract.ResolutionY, *Contract.RHI);
        Result.QualitySummary = PresetId == TEXT("hero-arena-character-rendering")
			? TEXT("真实 5v5 对局；本地与近场角色保持全帧率，远场角色进入 URO、骨骼 LOD，并省略后处理动画")
			: TEXT("本地与近场角色保留完整动画；远场角色进入 URO、骨骼 LOD，并省略包含 FootPlant IK 的后处理动画");
    }
    else if (Contract.Dimension == TEXT("RenderingLOD"))
    {
        Result.ContractSummary = FString::Printf(TEXT("%s · %d 个角色 · %dx%d · %s · 固定镜头"),
            *Contract.Map, Contract.TargetAgents, Contract.ResolutionX, Contract.ResolutionY, *Contract.RHI);
		Result.QualitySummary = TEXT("近景角色保留光追可见性；远场角色退出光追场景，普通主通道、动画与光栅阴影保持不变");
    }
    else if (Contract.Dimension == TEXT("Physics"))
    {
        Result.ContractSummary = FString::Printf(TEXT("%s · %d 个受控 Chaos 刚体 · %dx%d · %s"),
            *Contract.Map, Contract.TargetPhysicsBodies, Contract.ResolutionX, Contract.ResolutionY, *Contract.RHI);
        bool bBaselineBodiesPresent = false;
        bool bCandidateBodiesPresent = false;
        bool bBaselineSimulatedPresent = false;
        bool bCandidateSimulatedPresent = false;
        bool bBaselineReducedPresent = false;
        bool bCandidateReducedPresent = false;
        const double BaselineBodies = MetricMedian(Baselines, TEXT("physicsBodyCount"), TEXT("mean"), bBaselineBodiesPresent);
        const double CandidateBodies = MetricMedian(Candidates, TEXT("physicsBodyCount"), TEXT("mean"), bCandidateBodiesPresent);
        const double BaselineSimulated = MetricMedian(Baselines, TEXT("physicsSimulatedBodyCount"), TEXT("mean"), bBaselineSimulatedPresent);
        const double CandidateSimulated = MetricMedian(Candidates, TEXT("physicsSimulatedBodyCount"), TEXT("mean"), bCandidateSimulatedPresent);
        const double BaselineReduced = MetricMedian(Baselines, TEXT("physicsLODDisabledBodyCount"), TEXT("mean"), bBaselineReducedPresent);
        const double CandidateReduced = MetricMedian(Candidates, TEXT("physicsLODDisabledBodyCount"), TEXT("mean"), bCandidateReducedPresent);
        Result.QualitySummary = bBaselineBodiesPresent && bCandidateBodiesPresent
            && bBaselineSimulatedPresent && bCandidateSimulatedPresent
            && bBaselineReducedPresent && bCandidateReducedPresent
            ? FString::Printf(TEXT("场景状态：总刚体 %.1f → %.1f，参与模拟 %.1f → %.1f，距离降级 %.1f → %.1f"),
                BaselineBodies, CandidateBodies, BaselineSimulated, CandidateSimulated, BaselineReduced, CandidateReduced)
            : TEXT("固定排列与激励节奏；刚体总数、参与模拟和距离降级数量随结果保存");
    }
    else if (Contract.Dimension == TEXT("Niagara"))
    {
        Result.ContractSummary = FString::Printf(TEXT("%s · %d 个状态槽位 · %.0f 次爆发/秒 · %dx%d · %s"),
            *Contract.Map, Contract.TargetNiagaraStatusSlots, Contract.NiagaraBurstsPerSecond,
            Contract.ResolutionX, Contract.ResolutionY, *Contract.RHI);
        bool bBaselineRequestedPresent = false;
        bool bCandidateRequestedPresent = false;
        bool bBaselineSpawnedPresent = false;
        bool bCandidateSpawnedPresent = false;
        bool bBaselineActivePresent = false;
        bool bCandidateActivePresent = false;
        bool bBaselineStatusPresent = false;
        bool bCandidateStatusPresent = false;
        const double BaselineRequested = MetricMedian(Baselines, TEXT("niagaraRequestedBurstCount"), TEXT("max"), bBaselineRequestedPresent);
        const double CandidateRequested = MetricMedian(Candidates, TEXT("niagaraRequestedBurstCount"), TEXT("max"), bCandidateRequestedPresent);
        const double BaselineSpawned = MetricMedian(Baselines, TEXT("niagaraBurstSpawnCount"), TEXT("max"), bBaselineSpawnedPresent);
        const double CandidateSpawned = MetricMedian(Candidates, TEXT("niagaraBurstSpawnCount"), TEXT("max"), bCandidateSpawnedPresent);
        const double BaselineActive = MetricMedian(Baselines, TEXT("niagaraActiveComponentCount"), TEXT("mean"), bBaselineActivePresent);
        const double CandidateActive = MetricMedian(Candidates, TEXT("niagaraActiveComponentCount"), TEXT("mean"), bCandidateActivePresent);
        const double BaselineStatus = MetricMedian(Baselines, TEXT("niagaraStatusActivationCount"), TEXT("max"), bBaselineStatusPresent);
        const double CandidateStatus = MetricMedian(Candidates, TEXT("niagaraStatusActivationCount"), TEXT("max"), bCandidateStatusPresent);
        if (bBaselineRequestedPresent && bCandidateRequestedPresent && bBaselineSpawnedPresent && bCandidateSpawnedPresent
            && bBaselineActivePresent && bCandidateActivePresent && bBaselineStatusPresent && bCandidateStatusPresent)
        {
            Result.QualitySummary = FString::Printf(TEXT("场景状态：爆发请求/生成 %.0f/%.0f → %.0f/%.0f，状态激活 %.0f → %.0f，活动组件 %.1f → %.1f"),
                BaselineRequested, BaselineSpawned, CandidateRequested, CandidateSpawned,
                BaselineStatus, CandidateStatus, BaselineActive, CandidateActive);
        }
        else if (bBaselineRequestedPresent && bBaselineSpawnedPresent && bBaselineActivePresent)
        {
            Result.QualitySummary = FString::Printf(TEXT("场景状态：请求 %.0f，实际生成 %.0f，活动组件平均 %.1f"),
                BaselineRequested, BaselineSpawned, BaselineActive);
        }
        else
        {
            Result.QualitySummary = TEXT("状态激活、爆发请求、实际生成和活动组件随结果保存");
        }
    }
    else if (Contract.Dimension == TEXT("GameplayTick"))
    {
        Result.ContractSummary = FString::Printf(TEXT("%s · %d AI · %dx%d · %s · 13 个武器拾取点"),
            *Contract.Map, Contract.TargetAgents, Contract.ResolutionX, Contract.ResolutionY, *Contract.RHI);
        Result.QualitySummary = TEXT("客户端保留旋转与重生进度；专用服务器不执行展示 Tick");
    }
	else if (Contract.Dimension == TEXT("UI"))
	{
		Result.ContractSummary = FString::Printf(TEXT("%s · %d AI · %dx%d · %s · 真实 5v5 HUD"),
			*Contract.Map, Contract.TargetAgents, Contract.ResolutionX, Contract.ResolutionY, *Contract.RHI);
		Result.QualitySummary = TEXT("技能冷却显示精度为 0.1 秒；目标距离与状态文本按 10 Hz 更新，消息提示仍即时触发");
	}
    else if (Contract.Dimension == TEXT("WeaponVFX"))
    {
        Result.ContractSummary = FString::Printf(TEXT("%s · %.0f 个武器表现组件事件/秒 · %dx%d · %s · 固定镜头"),
            *Contract.Map, Contract.NiagaraBurstsPerSecond, Contract.ResolutionX, Contract.ResolutionY, *Contract.RHI);
        bool bBaselineRequestedPresent = false;
        bool bCandidateRequestedPresent = false;
        bool bBaselineSpawnedPresent = false;
        bool bCandidateSpawnedPresent = false;
        bool bBaselineCulledPresent = false;
        bool bCandidateCulledPresent = false;
        bool bBaselineCriticalPresent = false;
        bool bCandidateCriticalPresent = false;
        const double BaselineRequested = MetricMedian(Baselines, TEXT("niagaraRequestedBurstCount"), TEXT("max"), bBaselineRequestedPresent);
        const double CandidateRequested = MetricMedian(Candidates, TEXT("niagaraRequestedBurstCount"), TEXT("max"), bCandidateRequestedPresent);
        const double BaselineSpawned = MetricMedian(Baselines, TEXT("niagaraBurstSpawnCount"), TEXT("max"), bBaselineSpawnedPresent);
        const double CandidateSpawned = MetricMedian(Candidates, TEXT("niagaraBurstSpawnCount"), TEXT("max"), bCandidateSpawnedPresent);
        const double BaselineCulled = MetricMedian(Baselines, TEXT("niagaraCulledBurstCount"), TEXT("max"), bBaselineCulledPresent);
        const double CandidateCulled = MetricMedian(Candidates, TEXT("niagaraCulledBurstCount"), TEXT("max"), bCandidateCulledPresent);
        const double BaselineCritical = MetricMedian(Baselines, TEXT("niagaraCriticalPreservationRatio"), TEXT("min"), bBaselineCriticalPresent);
        const double CandidateCritical = MetricMedian(Candidates, TEXT("niagaraCriticalPreservationRatio"), TEXT("min"), bCandidateCriticalPresent);
        Result.QualitySummary = bBaselineRequestedPresent && bCandidateRequestedPresent && bBaselineSpawnedPresent && bCandidateSpawnedPresent
            && bBaselineCulledPresent && bCandidateCulledPresent && bBaselineCriticalPresent && bCandidateCriticalPresent
            ? FString::Printf(TEXT("事件请求/生成 %.0f/%.0f → %.0f/%.0f；远场省略 %.0f → %.0f；曳光与命中保留率 %.0f%% → %.0f%%"),
                BaselineRequested, BaselineSpawned, CandidateRequested, CandidateSpawned,
                BaselineCulled, CandidateCulled, BaselineCritical * 100.0, CandidateCritical * 100.0)
            : TEXT("远场枪口与弹壳按距离省略；曳光和命中事件保持");
    }
	else if (Contract.Dimension == TEXT("LiveWeaponVFX"))
	{
		Result.ContractSummary = FString::Printf(TEXT("%s · 真实 5v5 对局 · %dx%d · %s · 2400 uu 完整表现距离"),
			*Contract.Map, Contract.ResolutionX, Contract.ResolutionY, *Contract.RHI);
		bool bBaselineEventsPresent = false;
		bool bCandidateEventsPresent = false;
		bool bBaselineReducedPresent = false;
		bool bCandidateReducedPresent = false;
		bool bCandidateShellPresent = false;
		bool bCandidateMuzzlePresent = false;
		bool bCandidateMissingPresent = false;
		const double BaselineEvents = MetricMedian(Baselines, TEXT("csvWeaponVFXPresentationEventsPerSecond"), TEXT("mean"), bBaselineEventsPresent);
		const double CandidateEvents = MetricMedian(Candidates, TEXT("csvWeaponVFXPresentationEventsPerSecond"), TEXT("mean"), bCandidateEventsPresent);
		const double BaselineReduced = MetricMedian(Baselines, TEXT("csvWeaponVFXReducedDetailEventsPerSecond"), TEXT("mean"), bBaselineReducedPresent);
		const double CandidateReduced = MetricMedian(Candidates, TEXT("csvWeaponVFXReducedDetailEventsPerSecond"), TEXT("mean"), bCandidateReducedPresent);
		const double CandidateShell = MetricMedian(Candidates, TEXT("csvWeaponVFXShellSuppressedPerSecond"), TEXT("mean"), bCandidateShellPresent);
		const double CandidateMuzzle = MetricMedian(Candidates, TEXT("csvWeaponVFXMuzzleSuppressedPerSecond"), TEXT("mean"), bCandidateMuzzlePresent);
		const double CandidateMissing = MetricMedian(Candidates, TEXT("csvWeaponVFXMissingPropertiesPerSecond"), TEXT("mean"), bCandidateMissingPresent);
		Result.QualitySummary = bBaselineEventsPresent && bCandidateEventsPresent && bBaselineReducedPresent && bCandidateReducedPresent
			&& bCandidateShellPresent && bCandidateMuzzlePresent
			? FString::Printf(TEXT("武器表现事件 %.1f → %.1f 次/秒；调整后降级/弹壳/枪口焰 %.1f/%.1f/%.1f 次/秒；属性缺失 %.1f 次/秒"),
				BaselineEvents, CandidateEvents, CandidateReduced, CandidateShell, CandidateMuzzle,
				bCandidateMissingPresent ? CandidateMissing : 0.0)
			: TEXT("开火率、远距降级与实际抑制项随结果保存；不同交火节奏不直接用于归因");
	}
	else if (Contract.Dimension == TEXT("GunPadVFX"))
	{
		Result.ContractSummary = FString::Printf(TEXT("%s · 真实 5v5 对局 · 13 个拾取点 · %dx%d · %s · 3000 uu 特效距离"),
			*Contract.Map, Contract.ResolutionX, Contract.ResolutionY, *Contract.RHI);
		bool bBaselineCountPresent = false;
		bool bCandidateCountPresent = false;
		bool bBaselineCpuPresent = false;
		bool bCandidateCpuPresent = false;
		const double BaselineCount = MetricMedian(Baselines, TEXT("csvGunPadInstanceCount"), TEXT("mean"), bBaselineCountPresent);
		const double CandidateCount = MetricMedian(Candidates, TEXT("csvGunPadInstanceCount"), TEXT("mean"), bCandidateCountPresent);
		const double BaselineCpu = MetricMedian(Baselines, TEXT("csvGunPadCpuTimeMs"), TEXT("mean"), bBaselineCpuPresent);
		const double CandidateCpu = MetricMedian(Candidates, TEXT("csvGunPadCpuTimeMs"), TEXT("mean"), bCandidateCpuPresent);
		Result.QualitySummary = bBaselineCountPresent && bCandidateCountPresent && bBaselineCpuPresent && bCandidateCpuPresent
			? FString::Printf(TEXT("GunPad 活跃实例 %.1f → %.1f；逐 System CPU 累计 %.3f → %.3f ms"),
				BaselineCount, CandidateCount, BaselineCpu, CandidateCpu)
			: TEXT("武器模型、底座、拾取碰撞、冷却与重生表现保持；只管理远场常驻粒子");
	}
    else
    {
        Result.ContractSummary = FString::Printf(TEXT("%s · %d AI · %d Relay · %d 客户端 · Dedicated Server · Seed %d"),
            *Contract.Map, Contract.TargetAgents, Contract.TargetFixtures, Contract.ConnectionCount, Contract.Seed);
        Result.QualitySummary = FString::Printf(TEXT("服务器始终保留 %d 个权威 Relay；空间裁剪只减少远端连接可见与复制集合"), Contract.TargetFixtures);
    }

    auto AddUnmatchedVariantHint = [&](const TCHAR* Variant, const int32 SelectedCount, const TCHAR* Label)
    {
        if (SelectedCount >= 3)
        {
            return;
        }
        const FRunSummary* Alternative = nullptr;
        for (const FRunSummary& Run : Runs)
        {
            if (Run.Variant == Variant && Run.ContractKey != Contract.ContractKey
                && (!Alternative || Run.StartedUtc > Alternative->StartedUtc))
            {
                Alternative = &Run;
            }
        }
        if (!Alternative)
        {
            return;
        }
        const TArray<FString> Differences = PerformanceComparisonContract::DescribeDifferences(
            Contract.RawSummary, Alternative->RawSummary);
        if (!Differences.IsEmpty())
        {
            Result.Problems.Add(FString::Printf(TEXT("另有%s样本未参与：%s不同"), Label,
                *FString::Join(Differences, TEXT("、"))));
        }
    };
    AddUnmatchedVariantHint(TEXT("Baseline"), Baselines.Num(), TEXT("改动前"));
    AddUnmatchedVariantHint(TEXT("Optimized"), Candidates.Num(), TEXT("调整后"));
    if (Baselines.Num() < 3) Result.Problems.Add(FString::Printf(TEXT("改动前只有 %d/3 次同条件样本"), Baselines.Num()));
    if (Candidates.Num() < 3) Result.Problems.Add(FString::Printf(TEXT("调整后只有 %d/3 次同条件样本"), Candidates.Num()));
    if (!Result.bAllGuardsPassing) Result.Problems.Add(TEXT("至少一次运行缺少必要负载、环境或采样结果"));
    if (Contract.Dimension == TEXT("Rendering") && (!Contract.bNormalRHI || !Contract.bInitialViewLocked))
    {
        Result.Problems.Add(TEXT("渲染实验缺少正常 RHI 或固定镜头"));
    }
	if (Contract.Dimension == TEXT("LiveWeaponVFX") && !Baselines.IsEmpty() && !Candidates.IsEmpty())
	{
		bool bBaselineEventsPresent = false;
		bool bCandidateEventsPresent = false;
		const double BaselineEvents = MetricMedian(Baselines, TEXT("csvWeaponVFXPresentationEventsPerSecond"), TEXT("mean"), bBaselineEventsPresent);
		const double CandidateEvents = MetricMedian(Candidates, TEXT("csvWeaponVFXPresentationEventsPerSecond"), TEXT("mean"), bCandidateEventsPresent);
		if (bBaselineEventsPresent && bCandidateEventsPresent && BaselineEvents > 0.0
			&& FMath::Abs(CandidateEvents - BaselineEvents) / BaselineEvents > 0.20)
		{
			Result.Problems.Add(FString::Printf(TEXT("两组武器表现事件率相差 %.0f%%，帧时间只作观测"),
				FMath::Abs(CandidateEvents - BaselineEvents) / BaselineEvents * 100.0));
		}
		bool bReducedPresent = false;
		bool bShellPresent = false;
		bool bMuzzlePresent = false;
		bool bMissingPresent = false;
		const double Reduced = MetricMedian(Candidates, TEXT("csvWeaponVFXReducedDetailEventsPerSecond"), TEXT("mean"), bReducedPresent);
		const double Shell = MetricMedian(Candidates, TEXT("csvWeaponVFXShellSuppressedPerSecond"), TEXT("mean"), bShellPresent);
		const double Muzzle = MetricMedian(Candidates, TEXT("csvWeaponVFXMuzzleSuppressedPerSecond"), TEXT("mean"), bMuzzlePresent);
		const double Missing = MetricMedian(Candidates, TEXT("csvWeaponVFXMissingPropertiesPerSecond"), TEXT("mean"), bMissingPresent);
		if (!bReducedPresent || !bShellPresent || !bMuzzlePresent || !bMissingPresent
			|| Missing > 0.0 || !FMath::IsNearlyEqual(Reduced, Shell, 0.01) || !FMath::IsNearlyEqual(Reduced, Muzzle, 0.01))
		{
			Result.Problems.Add(TEXT("武器表现距离预算没有完整作用于弹壳与枪口焰"));
		}
	}

    Result.bComparable = Result.Problems.IsEmpty();
    Result.StatusSummary = Result.bComparable
        ? FString::Printf(TEXT("%s · 改动前 %d/3 · 调整后 %d/3 · 源码 %s"),
            Result.bSingleBatch ? TEXT("同批次") : TEXT("同条件跨批次"),
            Baselines.Num(), Candidates.Num(), *Result.SourceRevision)
        : FString::Printf(TEXT("当前不能比较 · %s"), Result.Problems.IsEmpty() ? TEXT("实验条件不完整") : *Result.Problems[0]);

    if (Result.bComparable)
    {
        for (const FMetricDefinition& Definition : GetMetricDefinitions(Result.Dimension))
        {
            bool bBaselinePresent = false;
            bool bCandidatePresent = false;
            TArray<double> BaselineSamples = MetricSamples(
                Baselines, Definition.Key, Definition.Aggregate, bBaselinePresent);
            TArray<double> CandidateSamples = MetricSamples(
                Candidates, Definition.Key, Definition.Aggregate, bCandidatePresent);
            if (!bBaselinePresent || !bCandidatePresent)
            {
                continue;
            }
            BaselineSamples.Sort();
            CandidateSamples.Sort();
            const double BaselineValue = Median(BaselineSamples);
            const double CandidateValue = Median(CandidateSamples);
            if (FMath::IsNearlyZero(BaselineValue))
            {
                continue;
            }
            FPerformanceMetricComparisonView& Metric = Result.Metrics.AddDefaulted_GetRef();
            Metric.Label = Definition.Label;
            Metric.Unit = Definition.Unit;
            Metric.BaselineValue = BaselineValue;
            Metric.CandidateValue = CandidateValue;
            Metric.BaselineMinValue = BaselineSamples[0];
            Metric.BaselineMaxValue = BaselineSamples.Last();
            Metric.CandidateMinValue = CandidateSamples[0];
            Metric.CandidateMaxValue = CandidateSamples.Last();
            Metric.DeltaPercent = ((CandidateValue - BaselineValue) / BaselineValue) * 100.0;
            Metric.bLowerIsBetter = Definition.bLowerIsBetter;
            Metric.bSampleRangesOverlap = Metric.BaselineMinValue <= Metric.CandidateMaxValue
                && Metric.CandidateMinValue <= Metric.BaselineMaxValue;
        }
    }
    return Result;
}

FPerformanceBatchComparisonView FPerformanceResultAggregator::LoadLatestComparison(
    const FString& RunsRoot,
    const FString& PresetId,
    const FString& Dimension)
{
    return BuildLatestComparison(LoadRuns(RunsRoot), PresetId, Dimension);
}

void FPerformanceResultAggregator::InvalidateRunCache()
{
    GRunSummaryCache.bValid = false;
    GRunSummaryCache.RunsRoot.Reset();
    GRunSummaryCache.Runs.Reset();
}

FPerformanceBatchComparisonView FPerformanceResultAggregator::LoadComparisonForGroup(
    const FString& RunsRoot,
    const FString& PresetId,
    const FString& Dimension,
    const FString& GroupId)
{
    return BuildLatestComparison(LoadRuns(RunsRoot), PresetId, Dimension, GroupId);
}

TArray<FPerformanceBatchComparisonView> FPerformanceResultAggregator::LoadLatestComparisons(
    const FString& RunsRoot,
    TConstArrayView<FPerformanceComparisonQuery> Queries)
{
    const TArray<FRunSummary>& Runs = LoadRuns(RunsRoot);
    TArray<FPerformanceBatchComparisonView> Results;
    Results.Reserve(Queries.Num());
    for (const FPerformanceComparisonQuery& Query : Queries)
    {
        Results.Add(BuildLatestComparison(Runs, Query.PresetId, Query.Dimension));
    }
    return Results;
}
