#include "PerformanceLabWorldSubsystem.h"

#include "PerformanceLabCrowdAISubsystem.h"

#include "AIController.h"
#include "Algo/Sort.h"
#include "BrainComponent.h"
#include "Camera/CameraActor.h"
#include "Character/LyraCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "DynamicRHI.h"
#include "Engine/NetDriver.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Net/NetworkMetricsDatabase.h"
#include "Net/NetworkMetricsDefs.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "RenderTimer.h"
#include "RHIGlobals.h"
#include "RHIStats.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/Csv/CsvParser.h"
#include "System/LyraSignificanceManager.h"
#include "SkeletalMeshComponentBudgeted.h"

CSV_DEFINE_CATEGORY(PerformanceLab, true);

namespace PerformanceLabCommandLine
{
    bool ReadInt(const TCHAR* Key, int32& Value)
    {
        return FParse::Value(FCommandLine::Get(), Key, Value);
    }

    bool ReadDouble(const TCHAR* Key, double& Value)
    {
        FString Text;
        if (FParse::Value(FCommandLine::Get(), Key, Text))
        {
            Value = FCString::Atod(*Text);
            return true;
        }
        return false;
    }

    bool ReadString(const TCHAR* Key, FString& Value)
    {
        return FParse::Value(FCommandLine::Get(), Key, Value);
    }

    FString DomainToString(EPerformanceExperimentDomain Domain)
    {
        return StaticEnum<EPerformanceExperimentDomain>()->GetNameStringByValue(static_cast<int64>(Domain));
    }
}

bool UPerformanceLabWorldSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const UWorld* World = Cast<UWorld>(Outer);
    const bool bRuntimeWorld = World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
    return bRuntimeWorld && FParse::Param(FCommandLine::Get(), TEXT("PerfLab"));
}

void UPerformanceLabWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    ReadCommandLine();
    RHIName = GDynamicRHI ? GDynamicRHI->GetName() : TEXT("Unavailable");
    switch (GetWorld()->GetNetMode())
    {
    case NM_DedicatedServer: NetModeName = TEXT("DedicatedServer"); break;
    case NM_ListenServer: NetModeName = TEXT("ListenServer"); break;
    case NM_Client: NetModeName = TEXT("Client"); break;
    default: NetModeName = TEXT("Standalone"); break;
    }
    RunStartWallSeconds = FPlatformTime::Seconds();
    FMath::RandInit(Preset.Load.Seed);
    IFileManager::Get().MakeDirectory(*GetRunDirectory(), true);
    WriteRunJson(TEXT("manifest.json"), false, false);
    UE_LOG(LogTemp, Display, TEXT("[PerformanceLab] Run %s using preset %s; waiting for %d real AI pawns."), *Session.RunId, *Session.PresetId, Preset.Load.AgentCount);
}

void UPerformanceLabWorldSubsystem::Deinitialize()
{
    RestoreAIDecisionPolicy();
    // UE may create and tear down transitional runtime worlds while opening the
    // requested map. A transitional world is not a completed experiment and
    // must not write a final result or trigger -PerfLabAutoExit.
    Super::Deinitialize();
}

void UPerformanceLabWorldSubsystem::ReadCommandLine()
{
    bEnabled = true;
    bAutoExit = FParse::Param(FCommandLine::Get(), TEXT("PerfLabAutoExit"));
    Preset = FPerformanceExperimentPresetData::MakeCrowd200();

    FString PresetPath;
    FString RequestedPresetId;
    PerformanceLabCommandLine::ReadString(TEXT("PerfLabPresetId="), RequestedPresetId);
    if (PerformanceLabCommandLine::ReadString(TEXT("PerfLabPreset="), PresetPath))
    {
        if (const UPerformanceExperimentPreset* Asset = LoadObject<UPerformanceExperimentPreset>(nullptr, *PresetPath))
        {
            Preset = Asset->Definition;
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[PerformanceLab] Could not load preset '%s'; using built-in Crowd 200 contract."), *PresetPath);
        }
    }
    else if (RequestedPresetId == TEXT("hero-arena-character-rendering"))
    {
        Preset = FPerformanceExperimentPresetData::MakeHeroArenaRendering();
    }
    else if (RequestedPresetId == TEXT("hero-arena-network"))
    {
        Preset = FPerformanceExperimentPresetData::MakeHeroArenaNetwork();
    }

    Session.RunId = FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"));
    Session.SessionId = Session.RunId;
    Session.PresetId = Preset.PresetId;
    Session.Dimension = TEXT("Baseline");
    Session.Variant = TEXT("Baseline");
    Session.StartedUtc = FDateTime::UtcNow().ToIso8601();

    PerformanceLabCommandLine::ReadInt(TEXT("PerfLabAgents="), Preset.Load.AgentCount);
    PerformanceLabCommandLine::ReadInt(TEXT("PerfLabSeed="), Preset.Load.Seed);
    PerformanceLabCommandLine::ReadInt(TEXT("PerfLabConnections="), TargetConnectionCount);
    PerformanceLabCommandLine::ReadInt(TEXT("PerfLabFixtureCount="), TargetReplicatedFixtureCount);
    PerformanceLabCommandLine::ReadDouble(TEXT("PerfLabFixtureSpacing="), ReplicatedFixtureSpacing);
    PerformanceLabCommandLine::ReadString(TEXT("PerfLabFixtureClass="), ReplicatedFixtureClassPath);
    PerformanceLabCommandLine::ReadDouble(TEXT("PerfLabReadyTimeout="), Preset.Capture.ReadyTimeoutSeconds);
    PerformanceLabCommandLine::ReadDouble(TEXT("PerfLabWarmup="), Preset.Capture.WarmupSeconds);
    PerformanceLabCommandLine::ReadDouble(TEXT("PerfLabDuration="), Preset.Capture.DurationSeconds);
    PerformanceLabCommandLine::ReadString(TEXT("PerfLabRunId="), Session.RunId);
    const bool bHasSessionId = PerformanceLabCommandLine::ReadString(TEXT("PerfLabSessionId="), Session.SessionId);
    PerformanceLabCommandLine::ReadString(TEXT("PerfLabProcessRole="), Session.ProcessRole);
    PerformanceLabCommandLine::ReadString(TEXT("PerfLabVariant="), Session.Variant);
    PerformanceLabCommandLine::ReadString(TEXT("PerfLabDimension="), Session.Dimension);
    PerformanceLabCommandLine::ReadString(TEXT("PerfLabSourceRevision="), Session.SourceRevision);
    bAIDecisionBudgetEnabled = FParse::Param(FCommandLine::Get(), TEXT("PerfLabAIDecisionBudget"));
    bActiveAIRequested = FParse::Param(FCommandLine::Get(), TEXT("PerfLabActiveAI"));
    bCrowdStateMachineRequested = FParse::Param(FCommandLine::Get(), TEXT("PerfLabCrowdStateMachine"));
    bSharedBattleFactsRequested = FParse::Param(FCommandLine::Get(), TEXT("PerfLabSharedBattleFacts"));
    bNavigationLoadRequested = FParse::Param(FCommandLine::Get(), TEXT("PerfLabNavigationLoad"));
    bNavigationBudgetRequested = FParse::Param(FCommandLine::Get(), TEXT("PerfLabNavigationBudget"));
    bAnimationBudgetRequested = FParse::Param(FCommandLine::Get(), TEXT("PerfLabAnimationBudget"))
        || FParse::Param(FCommandLine::Get(), TEXT("LyraCharacterAnimationBudget"));
    if (!RequestedPresetId.IsEmpty())
    {
        Session.PresetId = RequestedPresetId;
    }
    Preset.Normalize();
    TargetConnectionCount = FMath::Max(0, TargetConnectionCount);
    TargetReplicatedFixtureCount = FMath::Max(0, TargetReplicatedFixtureCount);
    ReplicatedFixtureSpacing = FMath::Max(100.0, ReplicatedFixtureSpacing);
    if (TargetConnectionCount > 0)
    {
        FPerformanceQualityGuard Connections;
        Connections.Id = TEXT("network-connections");
        Connections.MetricOrPath = TEXT("netConnectionCount");
        Connections.ExpectedValue = TargetConnectionCount;
        Preset.Guards.Add(Connections);
    }
    if (TargetReplicatedFixtureCount > 0 && Session.ProcessRole == TEXT("Server"))
    {
        FPerformanceQualityGuard Fixtures;
        Fixtures.Id = TEXT("replicated-fixture-load");
        Fixtures.MetricOrPath = TEXT("replicatedFixtureCount");
        Fixtures.ExpectedValue = TargetReplicatedFixtureCount;
        Preset.Guards.Add(Fixtures);
    }
    if (!bHasSessionId || Session.SessionId.IsEmpty())
    {
        Session.SessionId = Session.RunId;
    }
    if (Session.Dimension == TEXT("AIDecision"))
    {
        Preset.Guards.RemoveAll([](const FPerformanceQualityGuard& Guard)
        {
            return Guard.Id == TEXT("possessed-ai-pawn-count");
        });

        FPerformanceQualityGuard LivePawns;
        LivePawns.Id = TEXT("ai-live-pawn-average");
        LivePawns.MetricOrPath = TEXT("possessedAIPawnCount.mean");
        LivePawns.ExpectedValue = Preset.Load.AgentCount * 0.9;
        Preset.Guards.Add(LivePawns);

        if (bCrowdStateMachineRequested)
        {
            FPerformanceQualityGuard RegisteredAgents;
            RegisteredAgents.Id = TEXT("crowd-state-machine-agent-count");
            RegisteredAgents.MetricOrPath = TEXT("crowdRegisteredAgentCount.mean");
            RegisteredAgents.ExpectedValue = Preset.Load.AgentCount;
            Preset.Guards.Add(RegisteredAgents);

            FPerformanceQualityGuard TargetedAgents;
            TargetedAgents.Id = TEXT("crowd-target-acquisition");
            TargetedAgents.MetricOrPath = TEXT("crowdTargetedAgentCount.mean");
            TargetedAgents.ExpectedValue = Preset.Load.AgentCount * 0.9;
            Preset.Guards.Add(TargetedAgents);
        }
        else
        {
            FPerformanceQualityGuard BrainComponents;
            BrainComponents.Id = TEXT("ai-brain-component-count");
            BrainComponents.MetricOrPath = TEXT("aiBrainComponentCount.mean");
            BrainComponents.ExpectedValue = Preset.Load.AgentCount;
            Preset.Guards.Add(BrainComponents);

            FPerformanceQualityGuard RunningBrains;
            RunningBrains.Id = TEXT("ai-running-brain-count");
            RunningBrains.MetricOrPath = TEXT("aiRunningBrainCount.mean");
            RunningBrains.ExpectedValue = Preset.Load.AgentCount * 0.9;
            Preset.Guards.Add(RunningBrains);

            FPerformanceQualityGuard DecisionBudget;
            DecisionBudget.Id = TEXT("ai-added-decision-delay-budget");
            DecisionBudget.MetricOrPath = TEXT("aiMaxDecisionBudgetMs.max");
            DecisionBudget.Operator = EPerformanceGuardOperator::LessOrEqual;
            DecisionBudget.ExpectedValue = 200.0;
            Preset.Guards.Add(DecisionBudget);
        }
    }
    else if (Session.Dimension == TEXT("Navigation"))
    {
        Preset.Guards.RemoveAll([](const FPerformanceQualityGuard& Guard)
        {
            return Guard.Id == TEXT("possessed-ai-pawn-count");
        });

        FPerformanceQualityGuard LivePawns;
        LivePawns.Id = TEXT("navigation-live-pawn-average");
        LivePawns.MetricOrPath = TEXT("possessedAIPawnCount.mean");
        LivePawns.ExpectedValue = Preset.Load.AgentCount * 0.9;
        Preset.Guards.Add(LivePawns);

        FPerformanceQualityGuard RegisteredAgents;
        RegisteredAgents.Id = TEXT("navigation-agent-count");
        RegisteredAgents.MetricOrPath = TEXT("navRegisteredAgentCount.mean");
        RegisteredAgents.ExpectedValue = Preset.Load.AgentCount;
        Preset.Guards.Add(RegisteredAgents);

        FPerformanceQualityGuard ValidRoutes;
        ValidRoutes.Id = TEXT("navigation-valid-route-count");
        ValidRoutes.MetricOrPath = TEXT("navValidRouteCount.mean");
        ValidRoutes.ExpectedValue = Preset.Load.AgentCount * 0.9;
        Preset.Guards.Add(ValidRoutes);

        FPerformanceQualityGuard Failures;
        Failures.Id = TEXT("navigation-request-failure-ratio");
        Failures.MetricOrPath = TEXT("navPathRequestFailureRatio.max");
        Failures.Operator = EPerformanceGuardOperator::LessOrEqual;
        Failures.ExpectedValue = 0.1;
        Preset.Guards.Add(Failures);

        FPerformanceQualityGuard MovingAgents;
        MovingAgents.Id = TEXT("navigation-moving-agent-count");
        MovingAgents.MetricOrPath = TEXT("navMovingAgentCount.mean");
        MovingAgents.ExpectedValue = Preset.Load.AgentCount * 0.8;
        Preset.Guards.Add(MovingAgents);

        FPerformanceQualityGuard Arrivals;
        Arrivals.Id = TEXT("navigation-arrivals-during-measurement");
        Arrivals.MetricOrPath = TEXT("navArrivalsDuringMeasurement.max");
        Arrivals.ExpectedValue = 1.0;
        Preset.Guards.Add(Arrivals);

        FPerformanceQualityGuard StuckAgents;
        StuckAgents.Id = TEXT("navigation-stuck-agent-budget");
        StuckAgents.MetricOrPath = TEXT("navStuckAgentCount.mean");
        StuckAgents.Operator = EPerformanceGuardOperator::LessOrEqual;
        StuckAgents.ExpectedValue = Preset.Load.AgentCount * 0.25;
        Preset.Guards.Add(StuckAgents);
    }
    else if (Session.Dimension == TEXT("AnimationLOD"))
    {
        Preset.Capture.bLockInitialPlayerView = true;
        Preset.Guards.RemoveAll([](const FPerformanceQualityGuard& Guard)
        {
            return Guard.Id == TEXT("possessed-ai-pawn-count");
        });

        FPerformanceQualityGuard LivePawns;
        LivePawns.Id = TEXT("animation-live-pawn-average");
        LivePawns.MetricOrPath = TEXT("possessedAIPawnCount.mean");
        LivePawns.ExpectedValue = Preset.Load.AgentCount * 0.9;
        Preset.Guards.Add(LivePawns);

        FPerformanceQualityGuard BudgetedMeshes;
        BudgetedMeshes.Id = TEXT("animation-budgeted-mesh-count");
        BudgetedMeshes.MetricOrPath = TEXT("animationBudgetedMeshCount.mean");
        BudgetedMeshes.ExpectedValue = Preset.Load.AgentCount;
        Preset.Guards.Add(BudgetedMeshes);

        FPerformanceQualityGuard NormalRHI;
        NormalRHI.Id = TEXT("animation-normal-rhi");
        NormalRHI.MetricOrPath = TEXT("normalRHI");
        NormalRHI.Operator = EPerformanceGuardOperator::Equal;
        NormalRHI.ExpectedValue = 1.0;
        Preset.Guards.Add(NormalRHI);

        FPerformanceQualityGuard FixedView;
        FixedView.Id = TEXT("animation-fixed-player-view");
        FixedView.MetricOrPath = TEXT("initialViewLocked");
        FixedView.Operator = EPerformanceGuardOperator::Equal;
        FixedView.ExpectedValue = 1.0;
        Preset.Guards.Add(FixedView);

        if (bAnimationBudgetRequested)
        {
            FPerformanceQualityGuard ManagedCharacters;
            ManagedCharacters.Id = TEXT("animation-managed-character-count");
            ManagedCharacters.MetricOrPath = TEXT("managedSignificanceObjects.mean");
            ManagedCharacters.ExpectedValue = Preset.Load.AgentCount;
            Preset.Guards.Add(ManagedCharacters);

            FPerformanceQualityGuard ProtectedNearField;
            ProtectedNearField.Id = TEXT("animation-near-field-protected");
            ProtectedNearField.MetricOrPath = TEXT("animationFullRateProtectedCount.mean");
            ProtectedNearField.ExpectedValue = 1.0;
            Preset.Guards.Add(ProtectedNearField);

            FPerformanceQualityGuard BudgetEligible;
            BudgetEligible.Id = TEXT("animation-budget-eligible-load");
            BudgetEligible.MetricOrPath = TEXT("animationBudgetEligibleCount.mean");
            BudgetEligible.ExpectedValue = Preset.Load.AgentCount * 0.25;
            Preset.Guards.Add(BudgetEligible);
        }
    }
}

void UPerformanceLabWorldSubsystem::Tick(float DeltaTime)
{
    if (!bEnabled)
    {
        return;
    }

    if (bFinished)
    {
        const bool bCsvReady = !CsvWriteFuture.IsValid() || CsvWriteFuture->IsReady();
        if (bCsvReady && !bArtifactsFinalized)
        {
            bArtifactsFinalized = true;
            ImportCsvMetrics();
            EvaluateGuards();
            WriteRunJson(TEXT("summary.json"), true, bTimedOutResult);
            UE_LOG(LogTemp, Display, TEXT("[PerformanceLab] Run %s artifacts finalized. Valid=%s."), *Session.RunId, AreRequiredGuardsPassing() && !bTimedOutResult ? TEXT("true") : TEXT("false"));
        }
        if (bAutoExit && bCsvReady && !bExitRequested)
        {
            bExitRequested = true;
            FPlatformMisc::RequestExit(false);
        }
        return;
    }

    ElapsedSeconds += DeltaTime;
    CountAgents();
    UpdateAIDecisionPolicy(DeltaTime);
    TrySpawnReplicatedFixture();

    CSV_CUSTOM_STAT(PerformanceLab, PawnCount, PawnCount, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, AIControllerCount, AIControllerCount, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, PossessedAIPawnCount, PossessedAIPawnCount, ECsvCustomStatOp::Set);

    if (!bMeasuring)
    {
        const double Now = FPlatformTime::Seconds();
        const bool bClientRole = Session.ProcessRole.StartsWith(TEXT("Client"));
        const bool bAgentLoadReady = bClientRole
            ? PawnCount >= Preset.Load.AgentCount
            : AIControllerCount >= Preset.Load.AgentCount && PossessedAIPawnCount >= Preset.Load.AgentCount;
        const bool bConnectionsReady = TargetConnectionCount <= 0 || NetConnectionCount >= TargetConnectionCount;
        const bool bFixturesReady = Session.ProcessRole != TEXT("Server")
            || TargetReplicatedFixtureCount <= 0
            || ReplicatedFixtureCount >= TargetReplicatedFixtureCount;
        const bool bLoadReady = bAgentLoadReady && bConnectionsReady && bFixturesReady;
        if (bLoadReady && AgentsReadyWallSeconds < 0.0)
        {
            AgentsReadyWallSeconds = Now;
            LockInitialPlayerView();
            UE_LOG(LogTemp, Display, TEXT("[PerformanceLab] Agent contract reached. Warming up for %.1f seconds."), Preset.Capture.WarmupSeconds);
        }

        if (bLoadReady && Now - AgentsReadyWallSeconds >= Preset.Capture.WarmupSeconds)
        {
            BeginMeasurement();
        }
        else if (Now - RunStartWallSeconds >= Preset.Capture.ReadyTimeoutSeconds)
        {
            FinishMeasurement(true);
        }
        return;
    }

    MeasurementElapsedSeconds += DeltaTime;
    const double FrameTimeMs = static_cast<double>(DeltaTime) * 1000.0;
    FrameTimeSumMs += FrameTimeMs;
    MinFrameTimeMs = FMath::Min(MinFrameTimeMs, FrameTimeMs);
    MaxFrameTimeMs = FMath::Max(MaxFrameTimeMs, FrameTimeMs);
    ++SampleCount;
    const double GameThreadTimeMs = FPlatformTime::ToMilliseconds(GGameThreadTime);
    const double RenderThreadTimeMs = FPlatformTime::ToMilliseconds(GRenderThreadTime);
    const double RHIThreadTimeMs = FPlatformTime::ToMilliseconds(GRHIThreadTime);
    const double GPUTimeMs = FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
    const double DrawCalls = GNumDrawCallsRHI[0];
    const double DrawnPrimitives = GNumPrimitivesDrawnRHI[0];
    RecordMetric(TEXT("frameTimeMs"), FrameTimeMs);
    RecordMetric(TEXT("aiControllerCount"), AIControllerCount);
    RecordMetric(TEXT("possessedAIPawnCount"), PossessedAIPawnCount);
    RecordMetric(TEXT("gameThreadTimeMs"), GameThreadTimeMs);
    RecordMetric(TEXT("renderThreadTimeMs"), RenderThreadTimeMs);
    RecordMetric(TEXT("rhiThreadTimeMs"), RHIThreadTimeMs);
    RecordMetric(TEXT("gpuTimeMs"), GPUTimeMs);
    RecordMetric(TEXT("drawCalls"), DrawCalls);
    RecordMetric(TEXT("drawnPrimitives"), DrawnPrimitives);
    RecordMetric(TEXT("lyraCharacters"), LyraCharacterCount);
    RecordMetric(TEXT("shadowCastingCharacters"), ShadowCastingCharacterCount);
    RecordMetric(TEXT("managedSignificanceObjects"), ManagedSignificanceObjectCount);
    RecordMetric(TEXT("animationBudgetedMeshCount"), AnimationBudgetedMeshCount);
    RecordMetric(TEXT("animationFullRateProtectedCount"), AnimationFullRateProtectedCount);
    RecordMetric(TEXT("animationBudgetEligibleCount"), AnimationBudgetEligibleCount);
    RecordMetric(TEXT("minCharacterViewDistance"), MinCharacterViewDistance);
    RecordMetric(TEXT("maxCharacterViewDistance"), MaxCharacterViewDistance);
    RecordMetric(TEXT("replicatedFixtureCount"), ReplicatedFixtureCount);
    RecordAIDecisionMetrics();
    RecordNavigationMetrics();
    RecordNetworkMetrics();
    CSV_CUSTOM_STAT(PerformanceLab, FrameTimeMs, FrameTimeMs, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, GameThreadTimeMs, GameThreadTimeMs, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, RenderThreadTimeMs, RenderThreadTimeMs, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, RHIThreadTimeMs, RHIThreadTimeMs, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, GPUTimeMs, GPUTimeMs, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, DrawCalls, DrawCalls, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, DrawnPrimitives, DrawnPrimitives, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, LyraCharacters, LyraCharacterCount, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, ShadowCastingCharacters, ShadowCastingCharacterCount, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, ManagedSignificanceObjects, ManagedSignificanceObjectCount, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, AnimationBudgetedMeshes, AnimationBudgetedMeshCount, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, AnimationFullRateProtected, AnimationFullRateProtectedCount, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, AnimationBudgetEligible, AnimationBudgetEligibleCount, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, MinCharacterViewDistance, MinCharacterViewDistance, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, MaxCharacterViewDistance, MaxCharacterViewDistance, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, AIBrainComponents, AIBrainComponentCount, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, AIRunningBrains, AIRunningBrainCount, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, AIFullRateBrains, AIFullRateBrainCount, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, AIReducedRateBrains, AIReducedRateBrainCount, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, AIMovingPawns, AIMovingPawnCount, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, AIMaxDecisionBudgetMs, AIMaxDecisionBudgetMs, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, AIPolicyUpdateTimeMs, AIPolicyUpdateTimeMs, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, CrowdRegisteredAgents, CrowdRegisteredAgentCount, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, CrowdTargetedAgents, CrowdTargetedAgentCount, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, CrowdCandidateChecks, static_cast<double>(CrowdCandidateCheckCount), ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, CrowdSharedFactsUpdateTimeMs, CrowdSharedFactsUpdateTimeMs, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, CrowdTargetQueryTimeMs, CrowdTargetQueryTimeMs, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, CrowdSharedFactsWorkTimeMs, CrowdSharedFactsWorkTimeMs, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, CrowdDecisionWorkTimeMs, CrowdDecisionWorkTimeMs, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, NavValidRoutes, NavigationValidRouteCount, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, NavMovingAgents, NavigationMovingAgentCount, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, NavStuckAgents, NavigationStuckAgentCount, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, NavPendingRequests, NavigationPendingRequestCount, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, NavDistanceToGoal, NavigationAverageDistanceToGoal, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, NavBudgetWorkTimeMs, NavigationBudgetWorkTimeMs, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, NavDecisionWorkTimeMs, NavigationDecisionWorkTimeMs, ECsvCustomStatOp::Set);

    if (MeasurementElapsedSeconds >= Preset.Capture.DurationSeconds)
    {
        FinishMeasurement(false);
    }
}

void UPerformanceLabWorldSubsystem::RecordNetworkMetrics()
{
    UNetDriver* NetDriver = GetWorld() ? GetWorld()->GetNetDriver() : nullptr;
    if (!NetDriver)
    {
        NetConnectionCount = 0;
        ReplicationDriverName = TEXT("None");
        return;
    }

    NetConnectionCount = NetDriver->ServerConnection ? 1 : NetDriver->ClientConnections.Num();
    ReplicationDriverName = GetNameSafe(NetDriver->GetReplicationDriver());
    RecordMetric(TEXT("netConnectionCount"), NetConnectionCount);
    RecordMetric(TEXT("netInBytesPerSecond"), NetDriver->InBytesPerSecond);
    RecordMetric(TEXT("netOutBytesPerSecond"), NetDriver->OutBytesPerSecond);
    RecordMetric(TEXT("netInPacketsPerSecond"), NetDriver->InPackets);
    RecordMetric(TEXT("netOutPacketsPerSecond"), NetDriver->OutPackets);
    RecordMetric(TEXT("netInBunchesPerSecond"), NetDriver->InBunches);
    RecordMetric(TEXT("netOutBunchesPerSecond"), NetDriver->OutBunches);
    RecordMetric(TEXT("netInPacketsLost"), NetDriver->InPacketsLost);
    RecordMetric(TEXT("netOutPacketsLost"), NetDriver->OutPacketsLost);

    CSV_CUSTOM_STAT(PerformanceLab, NetInBytesPerSecond, static_cast<double>(NetDriver->InBytesPerSecond), ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, NetOutBytesPerSecond, static_cast<double>(NetDriver->OutBytesPerSecond), ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, NetConnections, NetConnectionCount, ECsvCustomStatOp::Set);

    UNetworkMetricsDatabase* Database = NetDriver->GetMetrics();
    if (!Database)
    {
        return;
    }

    const TPair<const TCHAR*, FName> IntegerMetrics[] = {
        { TEXT("netReplicatedActors"), UE::Net::Metric::NumReplicatedActors },
        { TEXT("netReplicatedActorBytes"), UE::Net::Metric::NumReplicatedActorBytes },
        { TEXT("netActiveActors"), UE::Net::Metric::NumberOfActiveActors },
        { TEXT("netFullyDormantActors"), UE::Net::Metric::NumberOfFullyDormantActors },
        { TEXT("netSkippedEmptyUpdates"), UE::Net::Metric::NumSkippedObjectEmptyUpdates },
        { TEXT("netOpenChannels"), UE::Net::Metric::NumOpenChannels },
        { TEXT("netConsideredActors"), UE::Net::Metric::NumConsideredActors },
        { TEXT("netPrioritizedActors"), UE::Net::Metric::PrioritizedActors }
    };
    for (const TPair<const TCHAR*, FName>& Metric : IntegerMetrics)
    {
        if (Database->Contains(Metric.Value))
        {
            RecordMetric(Metric.Key, static_cast<double>(Database->GetInt(Metric.Value)));
        }
    }

    const TPair<const TCHAR*, FName> FloatMetrics[] = {
        { TEXT("netServerReplicateActorTimeMs"), UE::Net::Metric::ServerReplicateActorTimeMS },
        { TEXT("netGatherPrioritizeTimeMs"), UE::Net::Metric::GatherPrioritizeTimeMS },
        { TEXT("netReplicateActorTimeMs"), UE::Net::Metric::ReplicateActorTimeMS },
        { TEXT("netReplicateActorCallsPerConnection"), UE::Net::Metric::NumReplicateActorCallsPerConAvg }
    };
    for (const TPair<const TCHAR*, FName>& Metric : FloatMetrics)
    {
        if (Database->Contains(Metric.Value))
        {
            RecordMetric(Metric.Key, Database->GetFloat(Metric.Value));
        }
    }
}

TStatId UPerformanceLabWorldSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UPerformanceLabWorldSubsystem, STATGROUP_Tickables);
}

void UPerformanceLabWorldSubsystem::CountAgents()
{
    PawnCount = 0;
    AIControllerCount = 0;
    PossessedAIPawnCount = 0;
    LyraCharacterCount = 0;
    ShadowCastingCharacterCount = 0;
    ManagedSignificanceObjectCount = 0;
    AnimationBudgetedMeshCount = 0;
    AnimationFullRateProtectedCount = 0;
    AnimationBudgetEligibleCount = 0;
    ReplicatedFixtureCount = 0;
    AIBrainComponentCount = 0;
    AIRunningBrainCount = 0;
    AIFullRateBrainCount = 0;
    AIReducedRateBrainCount = 0;
    AIFocusedControllerCount = 0;
    AIMovingPawnCount = 0;
    AIMaxDecisionBudgetMs = 0.0;
    MinCharacterViewDistance = 0.0;
    MaxCharacterViewDistance = 0.0;

    // Connection readiness must be visible before measurement begins. Metric
    // samples are still recorded only by RecordNetworkMetrics while measuring.
    if (UNetDriver* NetDriver = GetWorld() ? GetWorld()->GetNetDriver() : nullptr)
    {
        // Byte-rate fields are updated only while net stats collection is active.
        NetDriver->bCollectNetStats = true;
        NetConnectionCount = NetDriver->ServerConnection ? 1 : NetDriver->ClientConnections.Num();
        ReplicationDriverName = GetNameSafe(NetDriver->GetReplicationDriver());
        if (!bNetworkReadyArtifactWritten && GetWorld()->GetNetMode() != NM_Client)
        {
            const FString ReadyPath = FPaths::Combine(GetRunDirectory(), TEXT("network-ready.txt"));
            const FString ReadyText = FString::Printf(TEXT("%s\n%s\n%s\n"), *Session.RunId, *Session.SessionId, *Session.ProcessRole);
            bNetworkReadyArtifactWritten = FFileHelper::SaveStringToFile(ReadyText, *ReadyPath);
        }
    }
    else
    {
        NetConnectionCount = 0;
        ReplicationDriverName = TEXT("None");
    }

    for (TActorIterator<APawn> It(GetWorld()); It; ++It)
    {
        ++PawnCount;
    }

    for (TActorIterator<AAIController> It(GetWorld()); It; ++It)
    {
        ++AIControllerCount;
        if (It->GetPawn())
        {
            ++PossessedAIPawnCount;
            if (It->GetPawn()->GetVelocity().SizeSquared2D() > FMath::Square(10.0))
            {
                ++AIMovingPawnCount;
            }
        }
        if (const UBrainComponent* Brain = It->GetBrainComponent())
        {
            ++AIBrainComponentCount;
            AIRunningBrainCount += Brain->IsRunning() ? 1 : 0;
            if (const float* BudgetInterval = AIBrainDecisionIntervals.Find(Brain))
            {
                ++AIReducedRateBrainCount;
                AIMaxDecisionBudgetMs = FMath::Max(AIMaxDecisionBudgetMs, *BudgetInterval * 1000.0);
            }
            else
            {
                ++AIFullRateBrainCount;
            }
        }
        AIFocusedControllerCount += It->GetFocusActor() ? 1 : 0;
    }

    FVector ViewLocation = FVector::ZeroVector;
    bool bHasViewLocation = false;
    if (const APlayerController* PlayerController = GetWorld()->GetFirstPlayerController())
    {
        FRotator ViewRotation;
        PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
        bHasViewLocation = true;
    }
    double LocalMinDistance = TNumericLimits<double>::Max();
    for (TActorIterator<ALyraCharacter> It(GetWorld()); It; ++It)
    {
        ++LyraCharacterCount;
        const USkeletalMeshComponent* Mesh = It->GetMesh();
        if (Mesh && Mesh->CastShadow)
        {
            ++ShadowCastingCharacterCount;
        }
        if (Cast<USkeletalMeshComponentBudgeted>(Mesh))
        {
            ++AnimationBudgetedMeshCount;
        }
        if (bHasViewLocation)
        {
            const double Distance = FVector::Distance(ViewLocation, It->GetActorLocation());
            LocalMinDistance = FMath::Min(LocalMinDistance, Distance);
            MaxCharacterViewDistance = FMath::Max(MaxCharacterViewDistance, Distance);
            const bool bProtected = (It->IsPlayerControlled() && It->IsLocallyControlled())
                || Distance <= ULyraSignificanceManager::GetCharacterAnimationFullRateDistance();
            AnimationFullRateProtectedCount += bProtected ? 1 : 0;
            AnimationBudgetEligibleCount += bProtected ? 0 : 1;
        }
    }
    if (bHasViewLocation && LyraCharacterCount > 0)
    {
        MinCharacterViewDistance = LocalMinDistance;
    }
    if (const ULyraSignificanceManager* Manager = USignificanceManager::Get<ULyraSignificanceManager>(GetWorld()))
    {
        TArray<USignificanceManager::FManagedObjectInfo*> ManagedObjects;
        Manager->GetManagedObjects(ManagedObjects);
        ManagedSignificanceObjectCount = ManagedObjects.Num();
    }
    if (ReplicatedFixtureClass)
    {
        for (TActorIterator<AActor> It(GetWorld(), ReplicatedFixtureClass); It; ++It)
        {
            ++ReplicatedFixtureCount;
        }
    }
}

void UPerformanceLabWorldSubsystem::UpdateAIDecisionPolicy(float DeltaTime)
{
    if (!bAIDecisionBudgetEnabled || Session.Dimension != TEXT("AIDecision") || !GetWorld())
    {
        return;
    }

    const double StartSeconds = FPlatformTime::Seconds();
    AIPolicyUpdateAccumulator += DeltaTime;
    if (AIPolicyUpdateAccumulator >= 0.5)
    {
        AIPolicyUpdateAccumulator = 0.0;

        FVector ReferenceLocation = FVector::ZeroVector;
        if (const APlayerController* PlayerController = GetWorld()->GetFirstPlayerController())
        {
            FRotator UnusedRotation;
            PlayerController->GetPlayerViewPoint(ReferenceLocation, UnusedRotation);
        }

        TSet<TWeakObjectPtr<UBrainComponent>> SeenBrains;
        for (TActorIterator<AAIController> It(GetWorld()); It; ++It)
        {
            UBrainComponent* Brain = It->GetBrainComponent();
            const APawn* Pawn = It->GetPawn();
            if (!Brain || !Pawn)
            {
                continue;
            }
            SeenBrains.Add(Brain);

            const double Distance = FVector::Dist2D(Pawn->GetActorLocation(), ReferenceLocation);
            const bool bCombatCritical = It->GetFocusActor() != nullptr || Distance <= 1200.0;
            if (bCombatCritical)
            {
                if (AIBrainDecisionIntervals.Remove(Brain) > 0)
                {
                    AIBrainAccumulators.Remove(Brain);
                    Brain->SetComponentTickEnabled(true);
                }
                continue;
            }

            const float TargetInterval = Distance <= 2400.0 ? 0.1f : 0.2f;
            AIBrainDecisionIntervals.FindOrAdd(Brain) = TargetInterval;
            AIBrainAccumulators.FindOrAdd(Brain, 0.0);
            Brain->SetComponentTickEnabled(false);
        }

        for (auto It = AIBrainDecisionIntervals.CreateIterator(); It; ++It)
        {
            if (!It.Key().IsValid() || !SeenBrains.Contains(It.Key()))
            {
                if (UBrainComponent* Brain = It.Key().Get())
                {
                    Brain->SetComponentTickEnabled(true);
                }
                AIBrainAccumulators.Remove(It.Key());
                It.RemoveCurrent();
            }
        }
    }

    for (auto It = AIBrainDecisionIntervals.CreateIterator(); It; ++It)
    {
        UBrainComponent* Brain = It.Key().Get();
        if (!Brain || !Brain->IsRunning())
        {
            AIBrainAccumulators.Remove(It.Key());
            It.RemoveCurrent();
            continue;
        }

        // BehaviorTreeComponent::ScheduleNextTick enables its component tick after every
        // manual tick (and after event-driven flow requests). Keep reduced-rate brains
        // owned by this scheduler so automatic and manual ticks cannot run in parallel.
        if (Brain->IsComponentTickEnabled())
        {
            Brain->SetComponentTickEnabled(false);
        }

        double& AccumulatedSeconds = AIBrainAccumulators.FindOrAdd(It.Key());
        AccumulatedSeconds += DeltaTime;
        const double NativeRequestedInterval = FMath::Max(0.0f, Brain->GetComponentTickInterval());
        const double EffectiveInterval = FMath::Max<double>(It.Value(), NativeRequestedInterval);
        if (AccumulatedSeconds >= EffectiveInterval)
        {
            Brain->TickComponent(static_cast<float>(AccumulatedSeconds), LEVELTICK_All, nullptr);
            Brain->SetComponentTickEnabled(false);
            AccumulatedSeconds = 0.0;
        }
    }
    AIPolicyUpdateTimeMs = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
}

void UPerformanceLabWorldSubsystem::RestoreAIDecisionPolicy()
{
    for (const TPair<TWeakObjectPtr<UBrainComponent>, float>& Pair : AIBrainDecisionIntervals)
    {
        if (UBrainComponent* Brain = Pair.Key.Get())
        {
            Brain->SetComponentTickEnabled(true);
        }
    }
    AIBrainDecisionIntervals.Reset();
    AIBrainAccumulators.Reset();
}

void UPerformanceLabWorldSubsystem::RecordAIDecisionMetrics()
{
    if (Session.Dimension != TEXT("AIDecision"))
    {
        return;
    }
    RecordMetric(TEXT("aiBrainComponentCount"), AIBrainComponentCount);
    RecordMetric(TEXT("aiRunningBrainCount"), AIRunningBrainCount);
    RecordMetric(TEXT("aiFullRateBrainCount"), AIFullRateBrainCount);
    RecordMetric(TEXT("aiReducedRateBrainCount"), AIReducedRateBrainCount);
    RecordMetric(TEXT("aiFocusedControllerCount"), AIFocusedControllerCount);
    RecordMetric(TEXT("aiMovingPawnCount"), AIMovingPawnCount);
    RecordMetric(TEXT("aiMaxDecisionBudgetMs"), AIMaxDecisionBudgetMs);
    RecordMetric(TEXT("aiPolicyUpdateTimeMs"), AIPolicyUpdateTimeMs);
    if (const UPerformanceLabCrowdAISubsystem* CrowdSubsystem = GetWorld()->GetSubsystem<UPerformanceLabCrowdAISubsystem>())
    {
        const FPerformanceLabCrowdAISnapshot Snapshot = CrowdSubsystem->GetSnapshot();
        CrowdRegisteredAgentCount = Snapshot.RegisteredAgents;
        CrowdTargetedAgentCount = Snapshot.TargetedAgents;
        CrowdChasingAgentCount = Snapshot.ChasingAgents;
        CrowdEngagingAgentCount = Snapshot.EngagingAgents;
        CrowdDecisionCount = Snapshot.DecisionCount;
        CrowdCandidateCheckCount = Snapshot.CandidateCheckCount;
        CrowdSharedFactsUpdateTimeMs = Snapshot.SharedFactsUpdateTimeMs;
        const int64 DecisionDelta = FMath::Max<int64>(0, Snapshot.DecisionCount - PreviousCrowdDecisionCount);
        const int64 CandidateCheckDelta = FMath::Max<int64>(0, Snapshot.CandidateCheckCount - PreviousCrowdCandidateCheckCount);
        CrowdTargetQueryTimeMs = FMath::Max(0.0, Snapshot.TargetQueryTimeTotalMs - PreviousCrowdTargetQueryTimeTotalMs);
        CrowdSharedFactsWorkTimeMs = FMath::Max(0.0, Snapshot.SharedFactsUpdateTimeTotalMs - PreviousCrowdSharedFactsTimeTotalMs);
        CrowdDecisionWorkTimeMs = CrowdTargetQueryTimeMs + CrowdSharedFactsWorkTimeMs;
        PreviousCrowdDecisionCount = Snapshot.DecisionCount;
        PreviousCrowdCandidateCheckCount = Snapshot.CandidateCheckCount;
        PreviousCrowdTargetQueryTimeTotalMs = Snapshot.TargetQueryTimeTotalMs;
        PreviousCrowdSharedFactsTimeTotalMs = Snapshot.SharedFactsUpdateTimeTotalMs;
        RecordMetric(TEXT("crowdRegisteredAgentCount"), CrowdRegisteredAgentCount);
        RecordMetric(TEXT("crowdTargetedAgentCount"), CrowdTargetedAgentCount);
        RecordMetric(TEXT("crowdChasingAgentCount"), CrowdChasingAgentCount);
        RecordMetric(TEXT("crowdEngagingAgentCount"), CrowdEngagingAgentCount);
        RecordMetric(TEXT("crowdDecisionCount"), static_cast<double>(CrowdDecisionCount));
        RecordMetric(TEXT("crowdCandidateCheckCount"), static_cast<double>(CrowdCandidateCheckCount));
        RecordMetric(TEXT("crowdDecisionCountPerFrame"), static_cast<double>(DecisionDelta));
        RecordMetric(TEXT("crowdCandidateChecksPerFrame"), static_cast<double>(CandidateCheckDelta));
        RecordMetric(TEXT("crowdTargetQueryTimeMs"), CrowdTargetQueryTimeMs);
        RecordMetric(TEXT("crowdSharedFactsWorkTimeMs"), CrowdSharedFactsWorkTimeMs);
        RecordMetric(TEXT("crowdDecisionWorkTimeMs"), CrowdDecisionWorkTimeMs);
        RecordMetric(TEXT("crowdSharedFactsUpdateTimeMs"), CrowdSharedFactsUpdateTimeMs);
    }
}

void UPerformanceLabWorldSubsystem::RecordNavigationMetrics()
{
    if (Session.Dimension != TEXT("Navigation"))
    {
        return;
    }
    if (const UPerformanceLabCrowdAISubsystem* CrowdSubsystem = GetWorld()->GetSubsystem<UPerformanceLabCrowdAISubsystem>())
    {
        const FPerformanceLabCrowdAISnapshot Snapshot = CrowdSubsystem->GetSnapshot();
        NavigationValidRouteCount = Snapshot.NavigationValidRouteCount;
        NavigationMovingAgentCount = Snapshot.NavigationMovingAgentCount;
        NavigationStuckAgentCount = Snapshot.NavigationStuckAgentCount;
        NavigationPendingRequestCount = Snapshot.NavigationPendingRequestCount;
        NavigationRequestCount = Snapshot.NavigationRequestCount;
        NavigationReplanCount = Snapshot.NavigationReplanCount;
        NavigationFailureCount = Snapshot.NavigationFailureCount;
        NavigationArrivalCount = Snapshot.NavigationArrivalCount;
        NavigationAverageDistanceToGoal = Snapshot.NavigationAverageDistanceToGoal;
        NavigationFailureRatio = NavigationRequestCount > 0
            ? static_cast<double>(NavigationFailureCount) / static_cast<double>(NavigationRequestCount)
            : 0.0;
        const int64 RequestDelta = FMath::Max<int64>(0, NavigationRequestCount - PreviousNavigationRequestCount);
        const int64 ReplanDelta = FMath::Max<int64>(0, NavigationReplanCount - PreviousNavigationReplanCount);
        const int64 FailureDelta = FMath::Max<int64>(0, NavigationFailureCount - PreviousNavigationFailureCount);
        const int64 ArrivalDelta = FMath::Max<int64>(0, NavigationArrivalCount - PreviousNavigationArrivalCount);
        NavigationBudgetWorkTimeMs = FMath::Max(0.0, Snapshot.NavigationBudgetWorkTimeTotalMs - PreviousNavigationBudgetWorkTimeTotalMs);
        const double ControllerDecisionWorkTimeMs = FMath::Max(0.0, Snapshot.NavigationDecisionWorkTimeTotalMs - PreviousNavigationDecisionWorkTimeTotalMs);
        NavigationDecisionWorkTimeMs = ControllerDecisionWorkTimeMs + NavigationBudgetWorkTimeMs;
        PreviousNavigationRequestCount = NavigationRequestCount;
        PreviousNavigationReplanCount = NavigationReplanCount;
        PreviousNavigationFailureCount = NavigationFailureCount;
        PreviousNavigationArrivalCount = NavigationArrivalCount;
        PreviousNavigationBudgetWorkTimeTotalMs = Snapshot.NavigationBudgetWorkTimeTotalMs;
        PreviousNavigationDecisionWorkTimeTotalMs = Snapshot.NavigationDecisionWorkTimeTotalMs;

        RecordMetric(TEXT("navRegisteredAgentCount"), Snapshot.RegisteredAgents);
        RecordMetric(TEXT("navValidRouteCount"), NavigationValidRouteCount);
        RecordMetric(TEXT("navMovingAgentCount"), NavigationMovingAgentCount);
        RecordMetric(TEXT("navStuckAgentCount"), NavigationStuckAgentCount);
        RecordMetric(TEXT("navPendingRequestCount"), NavigationPendingRequestCount);
        RecordMetric(TEXT("navAverageDistanceToGoal"), NavigationAverageDistanceToGoal);
        RecordMetric(TEXT("navPathRequestCount"), static_cast<double>(NavigationRequestCount));
        RecordMetric(TEXT("navReplanCount"), static_cast<double>(NavigationReplanCount));
        RecordMetric(TEXT("navFailureCount"), static_cast<double>(NavigationFailureCount));
        RecordMetric(TEXT("navArrivalCount"), static_cast<double>(NavigationArrivalCount));
        RecordMetric(TEXT("navPathRequestsDuringMeasurement"), static_cast<double>(NavigationRequestCount - MeasurementStartNavigationRequestCount));
        RecordMetric(TEXT("navReplansDuringMeasurement"), static_cast<double>(NavigationReplanCount - MeasurementStartNavigationReplanCount));
        RecordMetric(TEXT("navFailuresDuringMeasurement"), static_cast<double>(NavigationFailureCount - MeasurementStartNavigationFailureCount));
        RecordMetric(TEXT("navArrivalsDuringMeasurement"), static_cast<double>(NavigationArrivalCount - MeasurementStartNavigationArrivalCount));
        RecordMetric(TEXT("navPathRequestsPerFrame"), static_cast<double>(RequestDelta));
        RecordMetric(TEXT("navReplansPerFrame"), static_cast<double>(ReplanDelta));
        RecordMetric(TEXT("navFailuresPerFrame"), static_cast<double>(FailureDelta));
        RecordMetric(TEXT("navArrivalsPerFrame"), static_cast<double>(ArrivalDelta));
        RecordMetric(TEXT("navPathRequestFailureRatio"), NavigationFailureRatio);
        RecordMetric(TEXT("navBudgetWorkTimeMs"), NavigationBudgetWorkTimeMs);
        RecordMetric(TEXT("navDecisionWorkTimeMs"), NavigationDecisionWorkTimeMs);
    }
}

void UPerformanceLabWorldSubsystem::TrySpawnReplicatedFixture()
{
    if (TargetReplicatedFixtureCount <= 0 || !GetWorld() || ReplicatedFixtureClassPath.IsEmpty())
    {
        return;
    }

    if (!ReplicatedFixtureClass)
    {
        ReplicatedFixtureClass = LoadClass<AActor>(nullptr, *ReplicatedFixtureClassPath);
        if (!ReplicatedFixtureClass)
        {
            return;
        }
    }

    // Clients never spawn the fixture, but they still resolve its class so
    // CountAgents can report how many actors were actually relevant to them.
    if (GetWorld()->GetNetMode() == NM_Client || ReplicatedFixtureCount >= TargetReplicatedFixtureCount
        || PossessedAIPawnCount < Preset.Load.AgentCount)
    {
        return;
    }

    FVector Anchor = FVector::ZeroVector;
    for (TActorIterator<APawn> It(GetWorld()); It; ++It)
    {
        Anchor = It->GetActorLocation();
        break;
    }
    const int32 Columns = FMath::CeilToInt(FMath::Sqrt(static_cast<double>(TargetReplicatedFixtureCount)));
    FActorSpawnParameters SpawnParameters;
    SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    SpawnParameters.ObjectFlags |= RF_Transient;
    for (int32 Index = ReplicatedFixtureCount; Index < TargetReplicatedFixtureCount; ++Index)
    {
        const int32 Row = Index / Columns;
        const int32 Column = Index % Columns;
        const FVector Location = Anchor + FVector(
            (Column - Columns / 2) * ReplicatedFixtureSpacing,
            (Row - Columns / 2) * ReplicatedFixtureSpacing,
            100.0);
        if (AActor* Fixture = GetWorld()->SpawnActor<AActor>(ReplicatedFixtureClass, Location, FRotator::ZeroRotator, SpawnParameters))
        {
            Fixture->SetLifeSpan(0.0f);
        }
    }
}

void UPerformanceLabWorldSubsystem::LockInitialPlayerView()
{
    if (!Preset.Capture.bLockInitialPlayerView || bInitialViewLocked || !GetWorld())
    {
        return;
    }

    APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
    if (!PlayerController || !PlayerController->IsLocalController())
    {
        return;
    }

    FVector Location;
    FRotator Rotation;
    PlayerController->GetPlayerViewPoint(Location, Rotation);
    FActorSpawnParameters Parameters;
    Parameters.ObjectFlags |= RF_Transient;
    if (ACameraActor* Camera = GetWorld()->SpawnActor<ACameraActor>(Location, Rotation, Parameters))
    {
        PlayerController->SetViewTarget(Camera);
        LockedViewActor = Camera;
        bInitialViewLocked = true;
    }
}

void UPerformanceLabWorldSubsystem::BeginMeasurement()
{
    bMeasuring = true;
    MeasurementElapsedSeconds = 0.0;
    if (const UPerformanceLabCrowdAISubsystem* CrowdSubsystem = GetWorld()->GetSubsystem<UPerformanceLabCrowdAISubsystem>())
    {
        const FPerformanceLabCrowdAISnapshot Snapshot = CrowdSubsystem->GetSnapshot();
        PreviousCrowdDecisionCount = Snapshot.DecisionCount;
        PreviousCrowdCandidateCheckCount = Snapshot.CandidateCheckCount;
        PreviousCrowdTargetQueryTimeTotalMs = Snapshot.TargetQueryTimeTotalMs;
        PreviousCrowdSharedFactsTimeTotalMs = Snapshot.SharedFactsUpdateTimeTotalMs;
        PreviousNavigationRequestCount = Snapshot.NavigationRequestCount;
        PreviousNavigationReplanCount = Snapshot.NavigationReplanCount;
        PreviousNavigationFailureCount = Snapshot.NavigationFailureCount;
        PreviousNavigationArrivalCount = Snapshot.NavigationArrivalCount;
        PreviousNavigationBudgetWorkTimeTotalMs = Snapshot.NavigationBudgetWorkTimeTotalMs;
        PreviousNavigationDecisionWorkTimeTotalMs = Snapshot.NavigationDecisionWorkTimeTotalMs;
        MeasurementStartNavigationRequestCount = Snapshot.NavigationRequestCount;
        MeasurementStartNavigationReplanCount = Snapshot.NavigationReplanCount;
        MeasurementStartNavigationFailureCount = Snapshot.NavigationFailureCount;
        MeasurementStartNavigationArrivalCount = Snapshot.NavigationArrivalCount;
    }

#if CSV_PROFILER
    if (FCsvProfiler* CsvProfiler = FCsvProfiler::Get())
    {
        TArray<FString> Categories;
        Preset.Capture.CsvCategories.ParseIntoArray(Categories, TEXT(","), true);
        for (FString& Category : Categories)
        {
            Category.TrimStartAndEndInline();
            CsvProfiler->EnableCategoryByString(Category);
        }
        CsvProfiler->SetMetadata(TEXT("PerfLabRunId"), *Session.RunId);
        CsvProfiler->SetMetadata(TEXT("PerfLabPresetId"), *Session.PresetId);
        CsvProfiler->SetMetadata(TEXT("PerfLabDimension"), *Session.Dimension);
        CsvProfiler->SetMetadata(TEXT("PerfLabVariant"), *Session.Variant);
        CsvProfiler->SetMetadata(TEXT("PerfLabTargetAgents"), *FString::FromInt(Preset.Load.AgentCount));
        CsvProfiler->SetMetadata(TEXT("PerfLabSeed"), *FString::FromInt(Preset.Load.Seed));
        CsvProfiler->BeginCapture(-1, GetRunDirectory(), TEXT("capture.csv"));
        bCsvCaptureStarted = true;
    }
#endif

    UE_LOG(LogTemp, Display, TEXT("[PerformanceLab] Measurement started: %d AI controllers, %d possessed pawns."), AIControllerCount, PossessedAIPawnCount);
}

void UPerformanceLabWorldSubsystem::FinishMeasurement(bool bTimedOut)
{
    if (bFinished)
    {
        return;
    }

    bFinished = true;
    bMeasuring = false;
    bTimedOutResult = bTimedOut;

#if CSV_PROFILER
    if (bCsvCaptureStarted)
    {
        if (FCsvProfiler* CsvProfiler = FCsvProfiler::Get(); CsvProfiler && !CsvProfiler->IsEndCapturePending())
        {
            CsvWriteFuture = MakeShared<TSharedFuture<FString>>(CsvProfiler->EndCapture());
        }
    }
#endif

    bArtifactsFinalized = !CsvWriteFuture.IsValid();
    EvaluateGuards();
    WriteRunJson(TEXT("summary.json"), true, bTimedOut);
    if (bArtifactsFinalized)
    {
        const bool bValid = !bTimedOut && AreRequiredGuardsPassing();
        UE_LOG(LogTemp, Display, TEXT("[PerformanceLab] Run %s finished. Valid=%s, AI=%d/%d."), *Session.RunId, bValid ? TEXT("true") : TEXT("false"), PossessedAIPawnCount, Preset.Load.AgentCount);
    }
    else
    {
        UE_LOG(LogTemp, Display, TEXT("[PerformanceLab] Run %s measurement finished; waiting for capture artifacts."), *Session.RunId);
    }

    if (bAutoExit && !CsvWriteFuture.IsValid())
    {
        bExitRequested = true;
        FPlatformMisc::RequestExit(false);
    }
}

void UPerformanceLabWorldSubsystem::ImportCsvMetrics()
{
    FString CsvText;
    if (!FFileHelper::LoadFileToString(CsvText, *FPaths::Combine(GetRunDirectory(), TEXT("capture.csv"))))
    {
        return;
    }

    const FCsvParser Parser(MoveTemp(CsvText));
    const FCsvParser::FRows& Rows = Parser.GetRows();
    if (Rows.Num() < 2)
    {
        return;
    }

    const TMap<FString, FString> RequestedColumns = {
        { TEXT("FrameTime"), TEXT("csvFrameTimeMs") },
        { TEXT("GameThreadTime"), TEXT("csvGameThreadTimeMs") },
        { TEXT("Exclusive/GameThread/TickActors"), TEXT("csvTickActorsTimeMs") },
        { TEXT("Exclusive/GameThread/BehaviorTreeTick"), TEXT("csvBehaviorTreeTickTimeMs") },
        { TEXT("Exclusive/GameThread/BehaviorTreeSearch"), TEXT("csvBehaviorTreeSearchTimeMs") },
        { TEXT("Exclusive/GameThread/AIPerception"), TEXT("csvAIPerceptionTimeMs") },
        { TEXT("Exclusive/GameThread/EnvQueryManager"), TEXT("csvEnvQueryTimeMs") },
        { TEXT("Exclusive/GameThread/Animation"), TEXT("csvAnimationTimeMs") },
        { TEXT("Exclusive/GameThread/AnimationBudgetAllocator"), TEXT("csvAnimationBudgetAllocatorTimeMs") },
        { TEXT("AnimationBudget/GameThread/BudgetedAnimation"), TEXT("csvBudgetedAnimationTimeMs") },
        { TEXT("Exclusive/GameThread/CharacterMovement"), TEXT("csvCharacterMovementTimeMs") },
        { TEXT("Exclusive/GameThread/NavigationBuild"), TEXT("csvNavigationTimeMs") }
        ,{ TEXT("NavigationBuildDetailed/GameThread/Navigation_CrowdManager"), TEXT("csvCrowdManagerTimeMs") }
        ,{ TEXT("Ticks/PathFollowingComponent"), TEXT("csvPathFollowingTickCount") }
        ,{ TEXT("Ticks/RecastNavMesh"), TEXT("csvRecastNavMeshTickCount") }
        ,{ TEXT("Ticks/PerformanceLabCrowdAIController"), TEXT("csvCrowdControllerTickCount") }
        ,{ TEXT("Ticks/SkeletalMeshComponentBudgeted"), TEXT("csvBudgetedSkeletalMeshTickCount") }
    };

    TMap<int32, FString> ColumnMetrics;
    for (int32 Column = 0; Column < Rows[0].Num(); ++Column)
    {
        if (const FString* MetricName = RequestedColumns.Find(Rows[0][Column]))
        {
            ColumnMetrics.Add(Column, *MetricName);
        }
    }

    for (int32 RowIndex = 1; RowIndex < Rows.Num(); ++RowIndex)
    {
        const TArray<const TCHAR*>& Row = Rows[RowIndex];
        for (const TPair<int32, FString>& ColumnMetric : ColumnMetrics)
        {
            if (Row.IsValidIndex(ColumnMetric.Key) && Row[ColumnMetric.Key] && Row[ColumnMetric.Key][0] != 0)
            {
                double Value = 0.0;
                if (LexTryParseString(Value, Row[ColumnMetric.Key]))
                {
                    RecordMetric(ColumnMetric.Value, Value);
                }
            }
        }
    }

    for (const FString& MetricName : {
        FString(TEXT("csvTickActorsTimeMs")),
        FString(TEXT("csvBehaviorTreeTickTimeMs")),
        FString(TEXT("csvBehaviorTreeSearchTimeMs")),
        FString(TEXT("csvAIPerceptionTimeMs")),
        FString(TEXT("csvEnvQueryTimeMs")),
        FString(TEXT("csvAnimationTimeMs")),
        FString(TEXT("csvAnimationBudgetAllocatorTimeMs")),
        FString(TEXT("csvBudgetedAnimationTimeMs")),
        FString(TEXT("csvCharacterMovementTimeMs")),
        FString(TEXT("csvNavigationTimeMs")),
        FString(TEXT("csvCrowdManagerTimeMs")),
        FString(TEXT("csvPathFollowingTickCount")),
        FString(TEXT("csvRecastNavMeshTickCount")),
        FString(TEXT("csvCrowdControllerTickCount")),
        FString(TEXT("csvBudgetedSkeletalMeshTickCount")) })
    {
        if (const TArray<double>* Samples = MetricSamples.Find(MetricName); Samples && !Samples->IsEmpty())
        {
            double TotalTimeMs = 0.0;
            for (const double Value : *Samples)
            {
                TotalTimeMs += Value;
            }
            RecordMetric(MetricName + TEXT("PerSecond"), TotalTimeMs / Preset.Capture.DurationSeconds);
        }
    }
}

FString UPerformanceLabWorldSubsystem::GetRunDirectory() const
{
    return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PerformanceLab"), TEXT("Runs"), Session.RunId);
}

void UPerformanceLabWorldSubsystem::RecordMetric(const FString& Name, double Value)
{
    if (FMath::IsFinite(Value) && Value >= 0.0)
    {
        MetricSamples.FindOrAdd(Name).Add(Value);
    }
}

double UPerformanceLabWorldSubsystem::GetMetricAggregate(const FString& Name, const FString& Aggregate) const
{
    const TArray<double>* Samples = MetricSamples.Find(Name);
    if (!Samples || Samples->IsEmpty())
    {
        return 0.0;
    }
    if (Aggregate == TEXT("min"))
    {
        double Result = TNumericLimits<double>::Max();
        for (const double Value : *Samples) Result = FMath::Min(Result, Value);
        return Result;
    }
    if (Aggregate == TEXT("max"))
    {
        double Result = 0.0;
        for (const double Value : *Samples) Result = FMath::Max(Result, Value);
        return Result;
    }
    if (Aggregate == TEXT("p95"))
    {
        TArray<double> Sorted = *Samples;
        Sorted.Sort();
        const int32 Index = FMath::Clamp(FMath::CeilToInt(Sorted.Num() * 0.95) - 1, 0, Sorted.Num() - 1);
        return Sorted[Index];
    }
    double Sum = 0.0;
    for (const double Value : *Samples)
    {
        Sum += Value;
    }
    return Sum / Samples->Num();
}

bool UPerformanceLabWorldSubsystem::ResolveGuardMetric(const FString& Metric, double& OutValue) const
{
    if (Metric == TEXT("aiControllerCount")) OutValue = AIControllerCount;
    else if (Metric == TEXT("possessedAIPawnCount")) OutValue = PossessedAIPawnCount;
    else if (Metric == TEXT("pawnCount")) OutValue = PawnCount;
    else if (Metric == TEXT("sampleCount")) OutValue = SampleCount;
    else if (Metric == TEXT("lyraCharacterCount")) OutValue = LyraCharacterCount;
    else if (Metric == TEXT("shadowCastingCharacterCount")) OutValue = ShadowCastingCharacterCount;
    else if (Metric == TEXT("normalRHI")) OutValue = GUsingNullRHI ? 0.0 : 1.0;
    else if (Metric == TEXT("initialViewLocked")) OutValue = bInitialViewLocked ? 1.0 : 0.0;
    else if (Metric == TEXT("netConnectionCount"))
    {
        const TArray<double>* ConnectionSamples = MetricSamples.Find(TEXT("netConnectionCount"));
        OutValue = ConnectionSamples && !ConnectionSamples->IsEmpty()
            ? GetMetricAggregate(TEXT("netConnectionCount"), TEXT("max"))
            : NetConnectionCount;
    }
    else if (Metric == TEXT("replicatedFixtureCount")) OutValue = ReplicatedFixtureCount;
    else
    {
        FString MetricName;
        FString Aggregate;
        if (!Metric.Split(TEXT("."), &MetricName, &Aggregate) || !MetricSamples.Contains(MetricName))
        {
            return false;
        }
        OutValue = GetMetricAggregate(MetricName, Aggregate);
    }
    return true;
}

void UPerformanceLabWorldSubsystem::EvaluateGuards()
{
    GuardResults.Reset();
    for (const FPerformanceQualityGuard& Guard : Preset.Guards)
    {
        FPerformanceGuardResult Result;
        Result.Id = Guard.Id;
        if (Guard.Operator == EPerformanceGuardOperator::FileNotEmpty)
        {
            const FString Path = FPaths::Combine(GetRunDirectory(), Guard.MetricOrPath);
            Result.ActualValue = static_cast<double>(IFileManager::Get().FileSize(*Path));
            Result.bPassed = Result.ActualValue > 0.0;
            Result.Detail = FString::Printf(TEXT("%s: %.0f bytes"), *Guard.MetricOrPath, Result.ActualValue);
        }
        else
        {
            double Actual = 0.0;
            const bool bResolved = ResolveGuardMetric(Guard.MetricOrPath, Actual);
            const double NumericTolerance = FMath::Max(1.e-6, FMath::Abs(Guard.ExpectedValue) * 1.e-6);
            Result.ActualValue = Actual;
            switch (Guard.Operator)
            {
            case EPerformanceGuardOperator::Equal: Result.bPassed = FMath::IsNearlyEqual(Actual, Guard.ExpectedValue, NumericTolerance); break;
            case EPerformanceGuardOperator::LessOrEqual: Result.bPassed = Actual <= Guard.ExpectedValue || FMath::IsNearlyEqual(Actual, Guard.ExpectedValue, NumericTolerance); break;
            default: Result.bPassed = Actual >= Guard.ExpectedValue || FMath::IsNearlyEqual(Actual, Guard.ExpectedValue, NumericTolerance); break;
            }
            Result.bPassed = bResolved && Result.bPassed;
            Result.Detail = bResolved
                ? FString::Printf(TEXT("%s: %.2f / %.2f"), *Guard.MetricOrPath, Actual, Guard.ExpectedValue)
                : FString::Printf(TEXT("unsupported metric: %s"), *Guard.MetricOrPath);
        }
        GuardResults.Add(MoveTemp(Result));
    }
}

bool UPerformanceLabWorldSubsystem::AreRequiredGuardsPassing() const
{
    for (int32 Index = 0; Index < Preset.Guards.Num(); ++Index)
    {
        if (Preset.Guards[Index].bRequired && (!GuardResults.IsValidIndex(Index) || !GuardResults[Index].bPassed))
        {
            return false;
        }
    }
    return true;
}

void UPerformanceLabWorldSubsystem::WriteRunJson(const TCHAR* FileName, bool bFinal, bool bTimedOut) const
{
    const double AverageFrameTimeMs = SampleCount > 0 ? FrameTimeSumMs / static_cast<double>(SampleCount) : 0.0;
    const bool bGuardsValid = AreRequiredGuardsPassing();

    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("schemaVersion"), TEXT("2.0"));
    Root->SetStringField(TEXT("runId"), Session.RunId);
    Root->SetStringField(TEXT("sessionId"), Session.SessionId);
    Root->SetStringField(TEXT("processRole"), Session.ProcessRole);
    Root->SetStringField(TEXT("presetId"), Session.PresetId);
    Root->SetStringField(TEXT("map"), GetWorld() ? GetWorld()->GetMapName() : TEXT("Unknown"));
    Root->SetStringField(TEXT("dimension"), Session.Dimension);
    Root->SetStringField(TEXT("variant"), Session.Variant);
    Root->SetStringField(TEXT("sourceRevision"), Session.SourceRevision);
    Root->SetStringField(TEXT("startedUtc"), Session.StartedUtc);
    Root->SetNumberField(TEXT("seed"), Preset.Load.Seed);
    Root->SetNumberField(TEXT("targetAgentCount"), Preset.Load.AgentCount);
    Root->SetNumberField(TEXT("pawnCount"), PawnCount);
    Root->SetNumberField(TEXT("aiControllerCount"), AIControllerCount);
    Root->SetNumberField(TEXT("possessedAIPawnCount"), PossessedAIPawnCount);
    Root->SetNumberField(TEXT("lyraCharacterCount"), LyraCharacterCount);
    Root->SetNumberField(TEXT("shadowCastingCharacterCount"), ShadowCastingCharacterCount);
    Root->SetNumberField(TEXT("managedSignificanceObjectCount"), ManagedSignificanceObjectCount);
    Root->SetNumberField(TEXT("netConnectionCount"), NetConnectionCount);
    Root->SetNumberField(TEXT("replicatedFixtureCount"), ReplicatedFixtureCount);
    Root->SetNumberField(TEXT("targetReplicatedFixtureCount"), TargetReplicatedFixtureCount);
    Root->SetStringField(TEXT("replicatedFixtureClass"), ReplicatedFixtureClassPath);
    Root->SetNumberField(TEXT("minCharacterViewDistance"), MinCharacterViewDistance);
    Root->SetNumberField(TEXT("maxCharacterViewDistance"), MaxCharacterViewDistance);
    Root->SetNumberField(TEXT("warmupSeconds"), Preset.Capture.WarmupSeconds);
    Root->SetNumberField(TEXT("measurementSeconds"), Preset.Capture.DurationSeconds);
    Root->SetBoolField(TEXT("final"), bFinal);
    Root->SetBoolField(TEXT("valid"), bFinal && bArtifactsFinalized && !bTimedOut && bGuardsValid);
    Root->SetBoolField(TEXT("readyTimedOut"), bTimedOut);
    Root->SetBoolField(TEXT("csvCaptureStarted"), bCsvCaptureStarted);
    Root->SetBoolField(TEXT("artifactsFinalized"), bArtifactsFinalized);
    Root->SetNumberField(TEXT("sampleCount"), SampleCount);
    Root->SetNumberField(TEXT("averageFrameTimeMs"), AverageFrameTimeMs);
    Root->SetNumberField(TEXT("minFrameTimeMs"), SampleCount > 0 ? MinFrameTimeMs : 0.0);
    Root->SetNumberField(TEXT("maxFrameTimeMs"), MaxFrameTimeMs);
    Root->SetStringField(TEXT("utc"), FDateTime::UtcNow().ToIso8601());
    Root->SetStringField(TEXT("rhiName"), RHIName);
    Root->SetStringField(TEXT("netMode"), NetModeName);
    Root->SetStringField(TEXT("replicationDriver"), ReplicationDriverName);
    Root->SetStringField(TEXT("aiControllerMode"), bCrowdStateMachineRequested ? TEXT("CrowdStateMachine") : (bActiveAIRequested ? TEXT("ActiveShooter") : TEXT("Configured")));
    Root->SetBoolField(TEXT("aiDecisionBudgetEnabled"), bAIDecisionBudgetEnabled);
    Root->SetBoolField(TEXT("sharedBattleFactsEnabled"), bSharedBattleFactsRequested);
    Root->SetBoolField(TEXT("navigationLoadEnabled"), bNavigationLoadRequested);
    Root->SetBoolField(TEXT("navigationBudgetEnabled"), bNavigationBudgetRequested);
    Root->SetBoolField(TEXT("animationBudgetEnabled"), bAnimationBudgetRequested);
    Root->SetNumberField(TEXT("animationFullRateDistance"), ULyraSignificanceManager::GetCharacterAnimationFullRateDistance());
    Root->SetNumberField(TEXT("navigationDecisionIntervalMs"), 200.0);
    Root->SetNumberField(TEXT("navigationReplanCooldownMs"), bNavigationBudgetRequested ? 800.0 : 0.0);
    Root->SetNumberField(TEXT("navigationGoalMergeDistance"), bNavigationBudgetRequested ? 40.0 : 0.0);
    Root->SetNumberField(TEXT("navigationRequestBudgetPerFrame"), bNavigationBudgetRequested ? 12.0 : 0.0);
    Root->SetNumberField(TEXT("aiNearDecisionRadius"), 1200.0);
    Root->SetNumberField(TEXT("aiMidDecisionRadius"), 2400.0);
    Root->SetNumberField(TEXT("aiMidDecisionIntervalMs"), 100.0);
    Root->SetNumberField(TEXT("aiFarDecisionIntervalMs"), 200.0);
    Root->SetBoolField(TEXT("normalRHI"), !GUsingNullRHI);
    Root->SetBoolField(TEXT("initialViewLocked"), bInitialViewLocked);

    TSharedRef<FJsonObject> Environment = MakeShared<FJsonObject>();
    Environment->SetStringField(TEXT("cpu"), FPlatformMisc::GetCPUBrand());
    Environment->SetStringField(TEXT("gpu"), GRHIAdapterName);
    Environment->SetStringField(TEXT("gpuDriverInternal"), GRHIAdapterInternalDriverVersion);
    Environment->SetStringField(TEXT("gpuDriverUser"), GRHIAdapterUserDriverVersion);
    Environment->SetNumberField(TEXT("gpuVendorId"), GRHIVendorId);
    Environment->SetNumberField(TEXT("gpuDeviceId"), GRHIDeviceId);
    Environment->SetStringField(TEXT("os"), FPlatformMisc::GetOSVersion());
    Environment->SetNumberField(TEXT("resolutionX"), Preset.Capture.ResolutionX);
    Environment->SetNumberField(TEXT("resolutionY"), Preset.Capture.ResolutionY);
    Environment->SetBoolField(TEXT("windowed"), Preset.Capture.bWindowed);
    Root->SetObjectField(TEXT("environment"), Environment);

    TSharedRef<FJsonObject> Metrics = MakeShared<FJsonObject>();
    for (const TPair<FString, TArray<double>>& Pair : MetricSamples)
    {
        TSharedRef<FJsonObject> Metric = MakeShared<FJsonObject>();
        Metric->SetNumberField(TEXT("sampleCount"), Pair.Value.Num());
        Metric->SetNumberField(TEXT("mean"), GetMetricAggregate(Pair.Key, TEXT("mean")));
        Metric->SetNumberField(TEXT("p95"), GetMetricAggregate(Pair.Key, TEXT("p95")));
        Metric->SetNumberField(TEXT("min"), GetMetricAggregate(Pair.Key, TEXT("min")));
        Metric->SetNumberField(TEXT("max"), GetMetricAggregate(Pair.Key, TEXT("max")));
        Metrics->SetObjectField(Pair.Key, Metric);
    }
    Root->SetObjectField(TEXT("metrics"), Metrics);

    TArray<TSharedPtr<FJsonValue>> Domains;
    for (const EPerformanceExperimentDomain Domain : Preset.Domains)
    {
        Domains.Add(MakeShared<FJsonValueString>(PerformanceLabCommandLine::DomainToString(Domain)));
    }
    Root->SetArrayField(TEXT("domains"), Domains);

    TArray<TSharedPtr<FJsonValue>> Guards;
    for (int32 Index = 0; Index < Preset.Guards.Num(); ++Index)
    {
        const FPerformanceQualityGuard& Contract = Preset.Guards[Index];
        const FPerformanceGuardResult* Result = GuardResults.IsValidIndex(Index) ? &GuardResults[Index] : nullptr;
        TSharedRef<FJsonObject> GuardJson = MakeShared<FJsonObject>();
        GuardJson->SetStringField(TEXT("id"), Contract.Id);
        GuardJson->SetBoolField(TEXT("required"), Contract.bRequired);
        GuardJson->SetBoolField(TEXT("passed"), Result && Result->bPassed);
        GuardJson->SetNumberField(TEXT("actual"), Result ? Result->ActualValue : 0.0);
        GuardJson->SetStringField(TEXT("detail"), Result ? Result->Detail : TEXT("not evaluated"));
        Guards.Add(MakeShared<FJsonValueObject>(GuardJson));
    }
    Root->SetArrayField(TEXT("guards"), Guards);

    TArray<TSharedPtr<FJsonValue>> Artifacts;
    for (const TCHAR* ArtifactName : { TEXT("manifest.json"), TEXT("summary.json"), TEXT("capture.csv") })
    {
        TSharedRef<FJsonObject> ArtifactJson = MakeShared<FJsonObject>();
        ArtifactJson->SetStringField(TEXT("path"), ArtifactName);
        ArtifactJson->SetNumberField(TEXT("sizeBytes"), static_cast<double>(FMath::Max<int64>(0, IFileManager::Get().FileSize(*FPaths::Combine(GetRunDirectory(), ArtifactName)))));
        Artifacts.Add(MakeShared<FJsonValueObject>(ArtifactJson));
    }
    Root->SetArrayField(TEXT("artifacts"), Artifacts);

    FString Json;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
    FJsonSerializer::Serialize(Root, Writer);
    FFileHelper::SaveStringToFile(Json, *FPaths::Combine(GetRunDirectory(), FileName));
}
