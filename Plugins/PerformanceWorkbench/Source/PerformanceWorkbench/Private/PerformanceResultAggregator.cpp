#include "PerformanceResultAggregator.h"

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
        FString StartedUtc;
        FString Map;
        FString RHI;
        int32 Seed = 0;
        int32 TargetAgents = 0;
        int32 TargetFixtures = 0;
        int32 ConnectionCount = 0;
        int32 ResolutionX = 0;
        int32 ResolutionY = 0;
        double WarmupSeconds = 0.0;
        double MeasurementSeconds = 0.0;
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
        OutRun.StartedUtc = GetString(Root, TEXT("startedUtc"), OutRun.RunId);
        OutRun.Map = GetString(Root, TEXT("map"));
        OutRun.RHI = GetString(Root, TEXT("rhiName"));
        OutRun.Seed = FMath::RoundToInt(GetNumber(Root, TEXT("seed")));
        OutRun.TargetAgents = FMath::RoundToInt(GetNumber(Root, TEXT("targetAgentCount")));
        OutRun.TargetFixtures = FMath::RoundToInt(GetNumber(Root, TEXT("targetReplicatedFixtureCount")));
        OutRun.WarmupSeconds = GetNumber(Root, TEXT("warmupSeconds"));
        OutRun.MeasurementSeconds = GetNumber(Root, TEXT("measurementSeconds"));
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
                Metric.bPresent = true;
                OutRun.Metrics.Add(Pair.Key, Metric);
            }
        }
        return !OutRun.RunId.IsEmpty() && !OutRun.PresetId.IsEmpty();
    }

    FString MakeContractKey(const FRunSummary& Run)
    {
        return FString::Printf(TEXT("%s|%s|%s|%s|%d|%d|%d|%s|%dx%d|%s|%.3f|%.3f"),
            *Run.PresetId, *Run.Dimension, *Run.SourceRevision, *Run.Map, Run.Seed,
            Run.TargetAgents, Run.TargetFixtures, *Run.RHI, Run.ResolutionX, Run.ResolutionY,
            *Run.ProcessRole, Run.WarmupSeconds, Run.MeasurementSeconds);
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

    double MetricMedian(const TArray<const FRunSummary*>& Runs, const TCHAR* Key, const TCHAR* Aggregate, bool& bOutPresent)
    {
        TArray<double> Values;
        for (const FRunSummary* Run : Runs)
        {
            if (const FMetricValue* Metric = Run->Metrics.Find(Key); Metric && Metric->bPresent)
            {
                Values.Add(FCString::Stricmp(Aggregate, TEXT("p95")) == 0 ? Metric->P95 : Metric->Mean);
            }
        }
        bOutPresent = Values.Num() == Runs.Num() && !Values.IsEmpty();
        return Median(MoveTemp(Values));
    }

    TArray<FMetricDefinition> GetMetricDefinitions(const FString& Dimension)
    {
        if (Dimension == TEXT("Rendering"))
        {
            return {
                { TEXT("gpuTimeMs"), TEXT("mean"), TEXT("GPU 平均"), TEXT("ms"), true },
                { TEXT("gpuTimeMs"), TEXT("p95"), TEXT("GPU P95"), TEXT("ms"), true },
                { TEXT("renderThreadTimeMs"), TEXT("mean"), TEXT("Render Thread 平均"), TEXT("ms"), true },
                { TEXT("renderThreadTimeMs"), TEXT("p95"), TEXT("Render Thread P95"), TEXT("ms"), true },
                { TEXT("rhiThreadTimeMs"), TEXT("mean"), TEXT("RHI Thread 平均"), TEXT("ms"), true },
                { TEXT("drawCalls"), TEXT("mean"), TEXT("Draw Call 平均"), TEXT("次/帧"), true },
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
                { TEXT("csvTickActorsTimeMs"), TEXT("mean"), TEXT("TickActors 平均"), TEXT("ms"), true },
                { TEXT("csvTickActorsTimeMs"), TEXT("p95"), TEXT("TickActors P95"), TEXT("ms"), true },
                { TEXT("frameTimeMs"), TEXT("p95"), TEXT("帧时 P95"), TEXT("ms"), true }
            };
        }
        return {};
    }
}

FPerformanceBatchComparisonView FPerformanceResultAggregator::LoadLatestComparison(
    const FString& RunsRoot,
    const FString& PresetId)
{
    FPerformanceBatchComparisonView Result;
    Result.PresetId = PresetId;

    TArray<FString> Directories;
    IFileManager::Get().FindFiles(Directories, *FPaths::Combine(RunsRoot, TEXT("*")), false, true);
    TArray<FRunSummary> Runs;
    for (const FString& Directory : Directories)
    {
        FRunSummary Run;
        if (LoadRun(FPaths::Combine(RunsRoot, Directory, TEXT("summary.json")), Run)
            && Run.PresetId == PresetId
            && (Run.ProcessRole == TEXT("Standalone") || Run.ProcessRole == TEXT("Server")))
        {
            Runs.Add(MoveTemp(Run));
        }
    }

    if (Runs.IsEmpty())
    {
        Result.Title = PresetId;
        Result.StatusSummary = TEXT("尚无可读取的正式结果");
        Result.Problems.Add(TEXT("没有找到 summary.json"));
        return Result;
    }

    TMap<FString, TArray<int32>> Groups;
    for (int32 Index = 0; Index < Runs.Num(); ++Index)
    {
        Groups.FindOrAdd(MakeContractKey(Runs[Index])).Add(Index);
    }

    FString SelectedKey;
    FString SelectedLatest;
    bool bSelectedHasBothVariants = false;
    for (const TPair<FString, TArray<int32>>& Group : Groups)
    {
        bool bHasBaseline = false;
        bool bHasCandidate = false;
        FString Latest;
        for (int32 Index : Group.Value)
        {
            bHasBaseline |= Runs[Index].Variant == TEXT("Baseline");
            bHasCandidate |= Runs[Index].Variant == TEXT("Optimized");
            if (Runs[Index].StartedUtc > Latest)
            {
                Latest = Runs[Index].StartedUtc;
            }
        }
        const bool bHasBoth = bHasBaseline && bHasCandidate;
        if (SelectedKey.IsEmpty() || (bHasBoth && !bSelectedHasBothVariants)
            || (bHasBoth == bSelectedHasBothVariants && Latest > SelectedLatest))
        {
            SelectedKey = Group.Key;
            SelectedLatest = Latest;
            bSelectedHasBothVariants = bHasBoth;
        }
    }

    TArray<const FRunSummary*> Baselines;
    TArray<const FRunSummary*> Candidates;
    for (int32 Index : Groups.FindChecked(SelectedKey))
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
    Result.Title = Contract.Dimension == TEXT("Rendering")
        ? TEXT("角色阴影 LOD · 正常 RHI")
        : (Contract.Dimension == TEXT("AIDecision") ? TEXT("Crowd 200 · 共享战场事实") : TEXT("Relay 空间相关性 · 多实例网络"));
    Result.BaselineRunCount = Baselines.Num();
    Result.CandidateRunCount = Candidates.Num();
    Result.bAllGuardsPassing = Algo::AllOf(Baselines, [](const FRunSummary* Run) { return Run->bValid && Run->bRequiredGuardsPassing; })
        && Algo::AllOf(Candidates, [](const FRunSummary* Run) { return Run->bValid && Run->bRequiredGuardsPassing; });

    if (Contract.Dimension == TEXT("Rendering"))
    {
        Result.ContractSummary = FString::Printf(TEXT("%s · %d AI · %dx%d · %s · 固定镜头 · Seed %d"),
            *Contract.Map, Contract.TargetAgents, Contract.ResolutionX, Contract.ResolutionY, *Contract.RHI, Contract.Seed);
        Result.QualitySummary = Contract.bNormalRHI && Contract.bInitialViewLocked
            ? TEXT("质量条件通过：正常 RHI、固定镜头、两组 Required Guard 全部通过")
            : TEXT("质量条件异常：渲染对比必须使用正常 RHI 与固定镜头");
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
            ? FString::Printf(TEXT("行为质量：目标获取 %.1f → %.1f，Chase 状态 %.1f → %.1f；两组 Required Guard 全部通过"),
                BaselineTargeted, CandidateTargeted, BaselineChasing, CandidateChasing)
            : TEXT("行为质量指标缺失");
    }
    else
    {
        Result.ContractSummary = FString::Printf(TEXT("%s · %d AI · %d Relay · %d 客户端 · Dedicated Server · Seed %d"),
            *Contract.Map, Contract.TargetAgents, Contract.TargetFixtures, Contract.ConnectionCount, Contract.Seed);
        Result.QualitySummary = FString::Printf(TEXT("服务器始终保留 %d 个权威 Relay；空间裁剪只减少远端连接可见与复制集合"), Contract.TargetFixtures);
    }

    if (Baselines.Num() < 3) Result.Problems.Add(FString::Printf(TEXT("Baseline 只有 %d/3 次有效候选"), Baselines.Num()));
    if (Candidates.Num() < 3) Result.Problems.Add(FString::Printf(TEXT("Optimized 只有 %d/3 次有效候选"), Candidates.Num()));
    if (!Result.bAllGuardsPassing) Result.Problems.Add(TEXT("至少一次运行无效或 Required Guard 未通过"));
    if (Contract.Dimension == TEXT("Rendering") && (!Contract.bNormalRHI || !Contract.bInitialViewLocked))
    {
        Result.Problems.Add(TEXT("渲染合同缺少正常 RHI 或固定镜头"));
    }

    Result.bComparable = Result.Problems.IsEmpty();
    Result.StatusSummary = Result.bComparable
        ? FString::Printf(TEXT("可比较 · Baseline %d/3 · Optimized %d/3 · 源码 %s"), Baselines.Num(), Candidates.Num(), *Result.SourceRevision)
        : FString::Printf(TEXT("不可形成正式结论 · %s"), Result.Problems.IsEmpty() ? TEXT("合同不完整") : *Result.Problems[0]);

    if (Result.bComparable)
    {
        for (const FMetricDefinition& Definition : GetMetricDefinitions(Result.Dimension))
        {
            bool bBaselinePresent = false;
            bool bCandidatePresent = false;
            const double BaselineValue = MetricMedian(Baselines, Definition.Key, Definition.Aggregate, bBaselinePresent);
            const double CandidateValue = MetricMedian(Candidates, Definition.Key, Definition.Aggregate, bCandidatePresent);
            if (!bBaselinePresent || !bCandidatePresent || FMath::IsNearlyZero(BaselineValue))
            {
                continue;
            }
            FPerformanceMetricComparisonView& Metric = Result.Metrics.AddDefaulted_GetRef();
            Metric.Label = Definition.Label;
            Metric.Unit = Definition.Unit;
            Metric.BaselineValue = BaselineValue;
            Metric.CandidateValue = CandidateValue;
            Metric.DeltaPercent = ((CandidateValue - BaselineValue) / BaselineValue) * 100.0;
            Metric.bLowerIsBetter = Definition.bLowerIsBetter;
        }
    }
    return Result;
}
