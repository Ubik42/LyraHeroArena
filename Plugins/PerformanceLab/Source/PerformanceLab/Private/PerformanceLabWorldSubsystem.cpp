#include "PerformanceLabWorldSubsystem.h"

#include "PerformanceLabCrowdAISubsystem.h"

#include "AIController.h"
#include "Algo/Sort.h"
#include "BrainComponent.h"
#include "Camera/CameraActor.h"
#include "Character/LyraCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "DynamicRHI.h"
#include "Engine/NetDriver.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Net/NetworkMetricsDatabase.h"
#include "Net/NetworkMetricsDefs.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "NiagaraEffectType.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "RenderTimer.h"
#include "RHIGlobals.h"
#include "RHIStats.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/Csv/CsvParser.h"
#include "System/LyraSignificanceManager.h"
#include "UObject/UObjectIterator.h"

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
    if (bDetailedFXStatsApplied)
    {
        if (IConsoleVariable* DetailedStats = IConsoleManager::Get().FindConsoleVariable(TEXT("fx.DetailedCSVStats")))
        {
            DetailedStats->Set(PreviousDetailedFXStats, ECVF_SetByCode);
        }
        if (IConsoleVariable* MemoryMode = IConsoleManager::Get().FindConsoleVariable(TEXT("fx.DetailedCSVStats.MemoryMode")))
        {
            MemoryMode->Set(PreviousDetailedFXMemoryMode, ECVF_SetByCode);
        }
        bDetailedFXStatsApplied = false;
    }
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
    bool bUsingAdHocContract = false;

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
            Preset = FPerformanceExperimentPresetData::MakeAdHoc();
            bUsingAdHocContract = true;
            UE_LOG(LogTemp, Warning,
                TEXT("[PerformanceLab] Could not load preset '%s'; using the minimal ad-hoc contract."), *PresetPath);
        }
    }
    else if (RequestedPresetId == TEXT("hero-arena-character-rendering"))
    {
        Preset = FPerformanceExperimentPresetData::MakeHeroArenaRendering();
    }
    else if (RequestedPresetId == TEXT("hero-arena-weapon-spawner-tick"))
    {
        Preset = FPerformanceExperimentPresetData::MakeHeroArenaRendering();
        Preset.PresetId = RequestedPresetId;
    }
	else if (RequestedPresetId == TEXT("hero-arena-hud-refresh"))
	{
		Preset = FPerformanceExperimentPresetData::MakeHeroArenaRendering();
		Preset.PresetId = RequestedPresetId;
	}
    else if (RequestedPresetId == TEXT("hero-arena-physics"))
    {
        Preset = FPerformanceExperimentPresetData::MakeControlledPhysicsLoad();
    }
    else if (RequestedPresetId == TEXT("hero-arena-niagara"))
    {
        Preset = FPerformanceExperimentPresetData::MakeControlledNiagaraLoad();
    }
    else if (RequestedPresetId == TEXT("hero-arena-niagara-status-lifecycle"))
    {
        Preset = FPerformanceExperimentPresetData::MakeControlledNiagaraLoad();
        Preset.PresetId = RequestedPresetId;
    }
    else if (RequestedPresetId == TEXT("hero-arena-weapon-vfx-budget"))
    {
        Preset = FPerformanceExperimentPresetData::MakeControlledNiagaraLoad();
        Preset.PresetId = RequestedPresetId;
    }
    else if (RequestedPresetId == TEXT("hero-arena-live-weapon-vfx-budget"))
    {
        Preset = FPerformanceExperimentPresetData::MakeControlledNiagaraLoad();
        Preset.PresetId = RequestedPresetId;
    }
    else if (RequestedPresetId == TEXT("hero-arena-network"))
    {
        Preset = FPerformanceExperimentPresetData::MakeHeroArenaNetwork();
    }
    else if (!RequestedPresetId.IsEmpty())
    {
        Preset = FPerformanceExperimentPresetData::MakeAdHoc();
        Preset.PresetId = RequestedPresetId;
        bUsingAdHocContract = true;
        UE_LOG(LogTemp, Display,
            TEXT("[PerformanceLab] Preset '%s' is not registered; using the minimal ad-hoc contract."),
            *RequestedPresetId);
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
    bAnimationLODRequested = FParse::Param(FCommandLine::Get(), TEXT("PerfLabAnimationLOD"))
        || FParse::Param(FCommandLine::Get(), TEXT("LyraCharacterAnimationLOD"));
    bCharacterRenderSignificanceRequested = FParse::Param(FCommandLine::Get(), TEXT("PerfLabCharacterRenderSignificance"))
        || FParse::Param(FCommandLine::Get(), TEXT("LyraCharacterRenderSignificance"));
    bCharacterRayTracingLODRequested = FParse::Param(FCommandLine::Get(), TEXT("LyraCharacterRayTracingLOD"));
    bCharacterCustomDepthLODRequested = FParse::Param(FCommandLine::Get(), TEXT("LyraCharacterCustomDepthLOD"));
    bPhysicsLoadRequested = FParse::Param(FCommandLine::Get(), TEXT("PerfLabPhysicsLoad"));
    bPhysicsLODRequested = FParse::Param(FCommandLine::Get(), TEXT("PerfLabPhysicsLOD"));
    bWeaponVFXLoadRequested = FParse::Param(FCommandLine::Get(), TEXT("PerfLabWeaponVFXLoad"));
    bNiagaraCombatLoadRequested = FParse::Param(FCommandLine::Get(), TEXT("PerfLabNiagaraCombatLoad"))
        || bWeaponVFXLoadRequested;
    bNiagaraBudgetRequested = FParse::Param(FCommandLine::Get(), TEXT("PerfLabNiagaraDistanceBudget"));
    bNiagaraPersistentStatusReuseRequested = FParse::Param(FCommandLine::Get(), TEXT("PerfLabNiagaraPersistentStatusReuse"));
    bDetailedFXStatsRequested = FParse::Param(FCommandLine::Get(), TEXT("PerfLabDetailedFXStats"));
    PerformanceLabCommandLine::ReadInt(TEXT("PerfLabPhysicsBodies="), TargetPhysicsBodyCount);
    PerformanceLabCommandLine::ReadDouble(TEXT("PerfLabPhysicsFullDistance="), PhysicsFullSimulationDistance);
    PerformanceLabCommandLine::ReadInt(TEXT("PerfLabNiagaraStatusSlots="), TargetNiagaraStatusSlotCount);
    PerformanceLabCommandLine::ReadDouble(TEXT("PerfLabNiagaraBurstsPerSecond="), NiagaraBurstsPerSecond);
    PerformanceLabCommandLine::ReadDouble(TEXT("PerfLabNiagaraFullDistance="), NiagaraFullEffectDistance);
    if (!RequestedPresetId.IsEmpty())
    {
        Session.PresetId = RequestedPresetId;
    }
    if (bUsingAdHocContract && Preset.Load.AgentCount > 0)
    {
        FPerformanceQualityGuard Agents;
        Agents.Id = TEXT("possessed-ai-pawn-count");
        Agents.MetricOrPath = TEXT("possessedAIPawnCount");
        Agents.ExpectedValue = Preset.Load.AgentCount;
        Preset.Guards.Add(Agents);
    }
    Preset.Normalize();
    TargetConnectionCount = FMath::Max(0, TargetConnectionCount);
    TargetReplicatedFixtureCount = FMath::Max(0, TargetReplicatedFixtureCount);
    TargetPhysicsBodyCount = FMath::Clamp(TargetPhysicsBodyCount, 16, 2000);
    PhysicsFullSimulationDistance = FMath::Max(500.0, PhysicsFullSimulationDistance);
    TargetNiagaraStatusSlotCount = FMath::Clamp(TargetNiagaraStatusSlotCount, 1, 256);
    NiagaraBurstsPerSecond = FMath::Clamp(NiagaraBurstsPerSecond, 1.0, 120.0);
    NiagaraFullEffectDistance = FMath::Max(500.0, NiagaraFullEffectDistance);
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
    if (Session.Dimension == TEXT("Rendering"))
    {
        if (bCharacterCustomDepthLODRequested)
        {
            FPerformanceQualityGuard ManagedCharacters;
            ManagedCharacters.Id = TEXT("custom-depth-managed-character-count");
            ManagedCharacters.MetricOrPath = TEXT("managedSignificanceObjects.mean");
            ManagedCharacters.ExpectedValue = Preset.Load.AgentCount * 0.9;
            Preset.Guards.Add(ManagedCharacters);

            FPerformanceQualityGuard NearFieldPreserved;
            NearFieldPreserved.Id = TEXT("custom-depth-near-field-preserved");
            NearFieldPreserved.MetricOrPath = TEXT("customDepthCharacters.mean");
            NearFieldPreserved.ExpectedValue = 1.0;
            Preset.Guards.Add(NearFieldPreserved);

            FPerformanceQualityGuard FarFieldReduced;
            FarFieldReduced.Id = TEXT("custom-depth-far-field-reduced");
            FarFieldReduced.MetricOrPath = TEXT("customDepthCharacters.mean");
            FarFieldReduced.Operator = EPerformanceGuardOperator::LessOrEqual;
            FarFieldReduced.ExpectedValue = Preset.Load.AgentCount * 0.75;
            Preset.Guards.Add(FarFieldReduced);
        }
        else
        {
            FPerformanceQualityGuard BaselineCustomDepth;
            BaselineCustomDepth.Id = TEXT("custom-depth-baseline-load");
            BaselineCustomDepth.MetricOrPath = TEXT("customDepthCharacters.mean");
            BaselineCustomDepth.ExpectedValue = Preset.Load.AgentCount * 0.9;
            Preset.Guards.Add(BaselineCustomDepth);
        }
    }
    if (bPhysicsLoadRequested)
    {
        FPerformanceQualityGuard Bodies;
        Bodies.Id = TEXT("physics-body-count");
        Bodies.MetricOrPath = TEXT("physicsBodyCount.mean");
        Bodies.ExpectedValue = TargetPhysicsBodyCount * 0.95;
        Preset.Guards.Add(Bodies);

        FPerformanceQualityGuard AwakeBodies;
        AwakeBodies.Id = TEXT("physics-active-body-load");
        AwakeBodies.MetricOrPath = TEXT("physicsAwakeBodyCount.mean");
        AwakeBodies.ExpectedValue = TargetPhysicsBodyCount * 0.1;
        Preset.Guards.Add(AwakeBodies);

        if (bPhysicsLODRequested)
        {
            FPerformanceQualityGuard SimulatedBodies;
            SimulatedBodies.Id = TEXT("physics-near-field-simulation");
            SimulatedBodies.MetricOrPath = TEXT("physicsSimulatedBodyCount.mean");
            SimulatedBodies.ExpectedValue = TargetPhysicsBodyCount * 0.2;
            Preset.Guards.Add(SimulatedBodies);

            FPerformanceQualityGuard ReducedBodies;
            ReducedBodies.Id = TEXT("physics-far-field-lod");
            ReducedBodies.MetricOrPath = TEXT("physicsLODDisabledBodyCount.mean");
            ReducedBodies.ExpectedValue = TargetPhysicsBodyCount * 0.2;
            Preset.Guards.Add(ReducedBodies);
        }
    }
    if (bNiagaraCombatLoadRequested)
    {
        if (bWeaponVFXLoadRequested)
        {
            Preset.Guards.RemoveAll([](const FPerformanceQualityGuard& Guard)
            {
                return Guard.Id == TEXT("possessed-ai-pawn-count");
            });
            FPerformanceQualityGuard PlayerPawn;
            PlayerPawn.Id = TEXT("weapon-vfx-anchor-pawn");
            PlayerPawn.MetricOrPath = TEXT("pawnCount");
            PlayerPawn.ExpectedValue = 1.0;
            Preset.Guards.Add(PlayerPawn);
        }

        if (!bWeaponVFXLoadRequested)
        {
            FPerformanceQualityGuard StatusActivations;
            StatusActivations.Id = TEXT("niagara-status-activation-load");
            StatusActivations.MetricOrPath = TEXT("niagaraStatusActivationCount.max");
            StatusActivations.ExpectedValue = TargetNiagaraStatusSlotCount;
            Preset.Guards.Add(StatusActivations);

            FPerformanceQualityGuard ActiveEffects;
            ActiveEffects.Id = TEXT("niagara-active-component-load");
            ActiveEffects.MetricOrPath = TEXT("niagaraActiveComponentCount.mean");
            ActiveEffects.ExpectedValue = TargetNiagaraStatusSlotCount;
            Preset.Guards.Add(ActiveEffects);
        }

        FPerformanceQualityGuard BurstEffects;
        BurstEffects.Id = TEXT("niagara-burst-spawned");
        BurstEffects.MetricOrPath = TEXT("niagaraBurstSpawnCount.max");
        BurstEffects.ExpectedValue = 1.0;
        Preset.Guards.Add(BurstEffects);

        if (bNiagaraBudgetRequested)
        {
            FPerformanceQualityGuard CulledEffects;
            CulledEffects.Id = TEXT("niagara-distance-culling-active");
            CulledEffects.MetricOrPath = TEXT("niagaraCulledBurstCount.max");
            CulledEffects.ExpectedValue = 1.0;
            Preset.Guards.Add(CulledEffects);

            FPerformanceQualityGuard CriticalEffects;
            CriticalEffects.Id = TEXT("niagara-critical-effects-preserved");
            CriticalEffects.MetricOrPath = TEXT("niagaraCriticalPreservationRatio.min");
            CriticalEffects.ExpectedValue = 1.0;
            Preset.Guards.Add(CriticalEffects);
        }
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
            TargetedAgents.bRequired = false;
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
        Failures.bRequired = false;
        Preset.Guards.Add(Failures);

        FPerformanceQualityGuard MovingAgents;
        MovingAgents.Id = TEXT("navigation-moving-agent-count");
        MovingAgents.MetricOrPath = TEXT("navMovingAgentCount.mean");
        MovingAgents.ExpectedValue = Preset.Load.AgentCount * 0.8;
        MovingAgents.bRequired = false;
        Preset.Guards.Add(MovingAgents);

        FPerformanceQualityGuard Arrivals;
        Arrivals.Id = TEXT("navigation-arrivals-during-measurement");
        Arrivals.MetricOrPath = TEXT("navArrivalsDuringMeasurement.max");
        Arrivals.ExpectedValue = 1.0;
        Arrivals.bRequired = false;
        Preset.Guards.Add(Arrivals);

        FPerformanceQualityGuard StuckAgents;
        StuckAgents.Id = TEXT("navigation-stuck-agent-budget");
        StuckAgents.MetricOrPath = TEXT("navStuckAgentCount.mean");
        StuckAgents.Operator = EPerformanceGuardOperator::LessOrEqual;
        StuckAgents.ExpectedValue = Preset.Load.AgentCount * 0.25;
        StuckAgents.bRequired = false;
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

        if (bAnimationLODRequested)
        {
            FPerformanceQualityGuard ManagedCharacters;
            ManagedCharacters.Id = TEXT("animation-managed-character-count");
            ManagedCharacters.MetricOrPath = TEXT("managedSignificanceObjects.mean");
            ManagedCharacters.ExpectedValue = Preset.Load.AgentCount * 0.9;
            Preset.Guards.Add(ManagedCharacters);

            FPerformanceQualityGuard AppliedSignificance;
            AppliedSignificance.Id = TEXT("animation-significance-applied");
            AppliedSignificance.MetricOrPath = TEXT("animationSignificanceAppliedCount.mean");
            AppliedSignificance.ExpectedValue = Preset.Load.AgentCount * 0.9;
            Preset.Guards.Add(AppliedSignificance);

			FPerformanceQualityGuard UROCharacters;
			UROCharacters.Id = TEXT("animation-uro-load");
			UROCharacters.MetricOrPath = TEXT("animationUROEnabledCount.mean");
			UROCharacters.ExpectedValue = Preset.Load.AgentCount * 0.25;
			Preset.Guards.Add(UROCharacters);

            FPerformanceQualityGuard LocalPlayerProtected;
            LocalPlayerProtected.Id = TEXT("animation-local-player-protected");
            LocalPlayerProtected.MetricOrPath = TEXT("animationLocalPlayerProtectedCount.mean");
            LocalPlayerProtected.ExpectedValue = 1.0;
            LocalPlayerProtected.bRequired = false;
            Preset.Guards.Add(LocalPlayerProtected);

            FPerformanceQualityGuard ProtectedNearField;
            ProtectedNearField.Id = TEXT("animation-near-field-protected");
            ProtectedNearField.MetricOrPath = TEXT("animationFullRateProtectedCount.mean");
            ProtectedNearField.ExpectedValue = 1.0;
            ProtectedNearField.bRequired = false;
            Preset.Guards.Add(ProtectedNearField);

        }
		if (bNavigationLoadRequested)
		{
			FPerformanceQualityGuard MovingCharacters;
			MovingCharacters.Id = TEXT("animation-moving-character-load");
			MovingCharacters.MetricOrPath = TEXT("navMovingAgentCount.mean");
			MovingCharacters.ExpectedValue = Preset.Load.AgentCount * 0.5;
			Preset.Guards.Add(MovingCharacters);
		}
    }
    else if (Session.Dimension == TEXT("RenderingLOD"))
    {
        Preset.Capture.bLockInitialPlayerView = true;
        Preset.Guards.RemoveAll([](const FPerformanceQualityGuard& Guard)
        {
            return Guard.Id == TEXT("possessed-ai-pawn-count");
        });

        FPerformanceQualityGuard LivePawns;
        LivePawns.Id = TEXT("rendering-live-pawn-average");
        LivePawns.MetricOrPath = TEXT("possessedAIPawnCount.mean");
        LivePawns.ExpectedValue = Preset.Load.AgentCount * 0.9;
        Preset.Guards.Add(LivePawns);

        FPerformanceQualityGuard CharacterLoad;
        CharacterLoad.Id = TEXT("rendering-character-load");
        CharacterLoad.MetricOrPath = TEXT("lyraCharacters.mean");
        CharacterLoad.ExpectedValue = Preset.Load.AgentCount * 0.9;
        Preset.Guards.Add(CharacterLoad);

        FPerformanceQualityGuard NormalRHI;
        NormalRHI.Id = TEXT("rendering-normal-rhi");
        NormalRHI.MetricOrPath = TEXT("normalRHI");
        NormalRHI.Operator = EPerformanceGuardOperator::Equal;
        NormalRHI.ExpectedValue = 1.0;
        Preset.Guards.Add(NormalRHI);

        FPerformanceQualityGuard FixedView;
        FixedView.Id = TEXT("rendering-fixed-player-view");
        FixedView.MetricOrPath = TEXT("initialViewLocked");
        FixedView.Operator = EPerformanceGuardOperator::Equal;
        FixedView.ExpectedValue = 1.0;
        Preset.Guards.Add(FixedView);

		if (bNavigationLoadRequested)
		{
			FPerformanceQualityGuard MovingCharacters;
			MovingCharacters.Id = TEXT("rendering-moving-character-load");
			MovingCharacters.MetricOrPath = TEXT("navMovingAgentCount.mean");
			MovingCharacters.ExpectedValue = Preset.Load.AgentCount * 0.5;
			Preset.Guards.Add(MovingCharacters);
		}

        FPerformanceQualityGuard FarFieldLoad;
        FarFieldLoad.Id = TEXT("rendering-far-field-load");
        FarFieldLoad.MetricOrPath = TEXT("maxCharacterViewDistance.mean");
        FarFieldLoad.ExpectedValue = ULyraSignificanceManager::GetCharacterShadowMaxDistance() * 1.25;
        Preset.Guards.Add(FarFieldLoad);

        if (bCharacterRenderSignificanceRequested)
        {
            FPerformanceQualityGuard ManagedCharacters;
            ManagedCharacters.Id = TEXT("rendering-managed-character-count");
            ManagedCharacters.MetricOrPath = TEXT("managedSignificanceObjects.mean");
            ManagedCharacters.ExpectedValue = Preset.Load.AgentCount * 0.9;
            Preset.Guards.Add(ManagedCharacters);

            FPerformanceQualityGuard NearFieldShadows;
            NearFieldShadows.Id = TEXT("rendering-near-field-shadows-preserved");
            NearFieldShadows.MetricOrPath = TEXT("shadowCastingCharacters.mean");
            NearFieldShadows.ExpectedValue = 1.0;
            NearFieldShadows.bRequired = false;
            Preset.Guards.Add(NearFieldShadows);

            FPerformanceQualityGuard FarFieldShadowsReduced;
            FarFieldShadowsReduced.Id = TEXT("rendering-far-field-shadows-reduced");
            FarFieldShadowsReduced.MetricOrPath = TEXT("shadowCastingCharacters.mean");
            FarFieldShadowsReduced.Operator = EPerformanceGuardOperator::LessOrEqual;
            FarFieldShadowsReduced.ExpectedValue = Preset.Load.AgentCount * 0.75;
            FarFieldShadowsReduced.bRequired = false;
            Preset.Guards.Add(FarFieldShadowsReduced);
        }
        else
        {
            FPerformanceQualityGuard BaselineShadows;
            BaselineShadows.Id = TEXT("rendering-baseline-shadow-load");
            BaselineShadows.MetricOrPath = TEXT("shadowCastingCharacters.mean");
            BaselineShadows.ExpectedValue = Preset.Load.AgentCount * 0.9;
            Preset.Guards.Add(BaselineShadows);
        }

		if (bCharacterRayTracingLODRequested)
		{
			FPerformanceQualityGuard RayTracingLODActive;
			RayTracingLODActive.Id = TEXT("rendering-ray-tracing-lod-active");
			RayTracingLODActive.MetricOrPath = TEXT("rayTracingExcludedCharacters.mean");
			RayTracingLODActive.ExpectedValue = 1.0;
			Preset.Guards.Add(RayTracingLODActive);
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
    TryCreatePhysicsLoad();
    UpdatePhysicsLoad(DeltaTime);
    TryCreateNiagaraCombatLoad();
    UpdateNiagaraCombatLoad(DeltaTime);

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
        const bool bPhysicsReady = !bPhysicsLoadRequested || bPhysicsLoadCreated;
        const bool bNiagaraReady = !bNiagaraCombatLoadRequested || bNiagaraCombatLoadCreated;
        const bool bLoadReady = bAgentLoadReady && bConnectionsReady && bFixturesReady && bPhysicsReady && bNiagaraReady;
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
    RecordMetric(TEXT("reducedAttachedShadowPrimitives"), ReducedAttachedShadowPrimitiveCount);
    RecordMetric(TEXT("rayTracingExcludedCharacters"), RayTracingExcludedCharacterCount);
    RecordMetric(TEXT("rayTracingExcludedAttachedPrimitives"), RayTracingExcludedAttachedPrimitiveCount);
    RecordMetric(TEXT("distanceCulledEquipmentPrimitives"), DistanceCulledEquipmentPrimitiveCount);
    RecordMetric(TEXT("customDepthCharacters"), CustomDepthCharacterCount);
    RecordMetric(TEXT("managedSignificanceObjects"), ManagedSignificanceObjectCount);
    RecordMetric(TEXT("animationSignificanceAppliedCount"), AnimationSignificanceAppliedCount);
    RecordMetric(TEXT("animationFullRateProtectedCount"), AnimationFullRateProtectedCount);
    RecordMetric(TEXT("animationLocalPlayerProtectedCount"), AnimationLocalPlayerProtectedCount);
    RecordMetric(TEXT("animationUROEnabledCount"), AnimationUROEnabledCount);
    RecordMetric(TEXT("animationFarFieldCount"), AnimationFarFieldCount);
    RecordMetric(TEXT("animationPostProcessDisabledCount"), AnimationPostProcessDisabledCount);
    RecordMetric(TEXT("minCharacterViewDistance"), MinCharacterViewDistance);
    RecordMetric(TEXT("maxCharacterViewDistance"), MaxCharacterViewDistance);
    RecordMetric(TEXT("replicatedFixtureCount"), ReplicatedFixtureCount);
    RecordMetric(TEXT("physicsBodyCount"), PhysicsBodyCount);
    RecordMetric(TEXT("physicsAwakeBodyCount"), PhysicsAwakeBodyCount);
    RecordMetric(TEXT("physicsSimulatedBodyCount"), PhysicsSimulatedBodyCount);
    RecordMetric(TEXT("physicsLODDisabledBodyCount"), PhysicsLODDisabledBodyCount);
    RecordMetric(TEXT("niagaraStatusActivationCount"), NiagaraStatusActivationCount);
    RecordMetric(TEXT("niagaraActiveComponentCount"), NiagaraActiveComponentCount);
    RecordMetric(TEXT("niagaraRequestedBurstCount"), NiagaraRequestedBurstCount);
    RecordMetric(TEXT("niagaraBurstSpawnCount"), NiagaraBurstSpawnCount);
    RecordMetric(TEXT("niagaraCulledBurstCount"), NiagaraCulledBurstCount);
    RecordMetric(TEXT("niagaraCriticalRequestedCount"), NiagaraCriticalRequestedCount);
    RecordMetric(TEXT("niagaraCriticalSpawnedCount"), NiagaraCriticalSpawnedCount);
    RecordMetric(TEXT("niagaraCriticalPreservationRatio"), NiagaraCriticalPreservationRatio);
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
    CSV_CUSTOM_STAT(PerformanceLab, ReducedAttachedShadowPrimitives, ReducedAttachedShadowPrimitiveCount, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, RayTracingExcludedCharacters, RayTracingExcludedCharacterCount, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, RayTracingExcludedAttachedPrimitives, RayTracingExcludedAttachedPrimitiveCount, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, DistanceCulledEquipmentPrimitives, DistanceCulledEquipmentPrimitiveCount, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, CustomDepthCharacters, CustomDepthCharacterCount, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, ManagedSignificanceObjects, ManagedSignificanceObjectCount, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, AnimationSignificanceApplied, AnimationSignificanceAppliedCount, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, AnimationFullRateProtected, AnimationFullRateProtectedCount, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, AnimationLocalPlayerProtected, AnimationLocalPlayerProtectedCount, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, AnimationUROEnabled, AnimationUROEnabledCount, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, AnimationFarField, AnimationFarFieldCount, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, AnimationPostProcessDisabled, AnimationPostProcessDisabledCount, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, PhysicsBodyCount, PhysicsBodyCount, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, PhysicsAwakeBodyCount, PhysicsAwakeBodyCount, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, PhysicsSimulatedBodyCount, PhysicsSimulatedBodyCount, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, PhysicsLODDisabledBodyCount, PhysicsLODDisabledBodyCount, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, NiagaraStatusActivationCount, NiagaraStatusActivationCount, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, NiagaraActiveComponentCount, NiagaraActiveComponentCount, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, NiagaraRequestedBurstCount, NiagaraRequestedBurstCount, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, NiagaraBurstSpawnCount, NiagaraBurstSpawnCount, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, NiagaraCulledBurstCount, NiagaraCulledBurstCount, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, NiagaraCriticalPreservationRatio, NiagaraCriticalPreservationRatio, ECsvCustomStatOp::Set);
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
    CSV_CUSTOM_STAT(PerformanceLab, CrowdChasingAgents, CrowdChasingAgentCount, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, CrowdEngagingAgents, CrowdEngagingAgentCount, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(PerformanceLab, CrowdDecisionCount, static_cast<double>(CrowdDecisionCount), ECsvCustomStatOp::Set);
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

void UPerformanceLabWorldSubsystem::TryCreatePhysicsLoad()
{
    if (!bPhysicsLoadRequested || bPhysicsLoadCreated || !GetWorld())
    {
        return;
    }

    APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
    APawn* AnchorPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
    if (!AnchorPawn)
    {
        return;
    }

    UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (!CubeMesh)
    {
        UE_LOG(LogTemp, Error, TEXT("[PerformanceLab] Physics load could not load the engine cube mesh."));
        return;
    }

    const FVector BaseLocation = AnchorPawn->GetActorLocation() + FVector(2500.0, 0.0, 150.0);
    FActorSpawnParameters SpawnParameters;
    SpawnParameters.ObjectFlags |= RF_Transient;
    SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    auto SpawnBoundary = [this, CubeMesh, &SpawnParameters](const FVector& Location, const FVector& Scale)
    {
        if (AStaticMeshActor* Actor = GetWorld()->SpawnActor<AStaticMeshActor>(Location, FRotator::ZeroRotator, SpawnParameters))
        {
            UStaticMeshComponent* Component = Actor->GetStaticMeshComponent();
            Component->SetStaticMesh(CubeMesh);
            Component->SetCollisionProfileName(TEXT("BlockAll"));
            Component->SetGenerateOverlapEvents(false);
            Component->SetCanEverAffectNavigation(false);
            Component->SetCastShadow(false);
            Actor->SetActorScale3D(Scale);
        }
    };

    SpawnBoundary(BaseLocation + FVector(0.0, 0.0, -50.0), FVector(8.0, 8.0, 0.5));
    SpawnBoundary(BaseLocation + FVector(0.0, -775.0, 350.0), FVector(8.0, 0.25, 4.0));
    SpawnBoundary(BaseLocation + FVector(0.0, 775.0, 350.0), FVector(8.0, 0.25, 4.0));
    SpawnBoundary(BaseLocation + FVector(-775.0, 0.0, 350.0), FVector(0.25, 8.0, 4.0));
    SpawnBoundary(BaseLocation + FVector(775.0, 0.0, 350.0), FVector(0.25, 8.0, 4.0));

    constexpr int32 Columns = 10;
    constexpr int32 Rows = 10;
    constexpr double Spacing = 94.0;
    for (int32 Index = 0; Index < TargetPhysicsBodyCount; ++Index)
    {
        const int32 Layer = Index / (Columns * Rows);
        const int32 Cell = Index % (Columns * Rows);
        const int32 X = Cell % Columns;
        const int32 Y = Cell / Columns;
        const FVector Location = BaseLocation + FVector(
            (X - (Columns - 1) * 0.5) * Spacing,
            (Y - (Rows - 1) * 0.5) * Spacing,
            48.0 + Layer * Spacing);

        AStaticMeshActor* Actor = GetWorld()->SpawnActor<AStaticMeshActor>(Location, FRotator::ZeroRotator, SpawnParameters);
        if (!Actor)
        {
            continue;
        }
        UStaticMeshComponent* Component = Actor->GetStaticMeshComponent();
        Component->SetMobility(EComponentMobility::Movable);
        Component->SetStaticMesh(CubeMesh);
        Component->SetCollisionProfileName(TEXT("PhysicsActor"));
        Component->SetGenerateOverlapEvents(false);
        Component->SetCanEverAffectNavigation(false);
        Component->SetCastShadow(false);
        Actor->SetActorScale3D(FVector(0.45));
        const bool bUseFullSimulation = !bPhysicsLODRequested
            || FVector::DistSquared(Location, AnchorPawn->GetActorLocation()) <= FMath::Square(PhysicsFullSimulationDistance);
        Component->SetSimulatePhysics(bUseFullSimulation);
        Component->SetEnableGravity(false);
        Component->SetLinearDamping(0.05);
        PhysicsSimulatedBodyCount += bUseFullSimulation ? 1 : 0;
        PhysicsLODDisabledBodyCount += bUseFullSimulation ? 0 : 1;
        PhysicsBodyComponents.Add(Component);
    }

    PhysicsBodyCount = PhysicsBodyComponents.Num();
    bPhysicsLoadCreated = true;
    UE_LOG(LogTemp, Display, TEXT("[PerformanceLab] Created controlled Chaos load with %d/%d bodies."), PhysicsBodyCount, TargetPhysicsBodyCount);
}

void UPerformanceLabWorldSubsystem::UpdatePhysicsLoad(float DeltaTime)
{
    if (!bPhysicsLoadCreated)
    {
        return;
    }

    PhysicsImpulseAccumulator += DeltaTime;
    PhysicsStateSampleAccumulator += DeltaTime;
    if (PhysicsImpulseAccumulator >= 0.75)
    {
        PhysicsImpulseAccumulator = FMath::Fmod(PhysicsImpulseAccumulator, 0.75);
        const int32 Phase = FMath::FloorToInt(ElapsedSeconds / 0.75) % 6;
        for (int32 Index = Phase; Index < PhysicsBodyComponents.Num(); Index += 6)
        {
            if (UStaticMeshComponent* Component = PhysicsBodyComponents[Index].Get(); Component && Component->IsSimulatingPhysics())
            {
                const double Direction = ((Index / 6) & 1) == 0 ? 1.0 : -1.0;
                const double VerticalDrift = ((Index / 3) % 3 - 1) * 35.0;
                Component->AddImpulse(FVector(360.0 * Direction, 240.0 * -Direction, VerticalDrift), NAME_None, true);
            }
        }
    }

    if (PhysicsStateSampleAccumulator >= 0.1)
    {
        PhysicsStateSampleAccumulator = FMath::Fmod(PhysicsStateSampleAccumulator, 0.1);
        PhysicsAwakeBodyCount = 0;
        PhysicsBodyCount = 0;
        PhysicsSimulatedBodyCount = 0;
        PhysicsLODDisabledBodyCount = 0;
        for (const TWeakObjectPtr<UStaticMeshComponent>& Body : PhysicsBodyComponents)
        {
            if (UStaticMeshComponent* Component = Body.Get())
            {
                ++PhysicsBodyCount;
                const bool bSimulating = Component->IsSimulatingPhysics();
                PhysicsSimulatedBodyCount += bSimulating ? 1 : 0;
                PhysicsLODDisabledBodyCount += bSimulating ? 0 : 1;
                PhysicsAwakeBodyCount += bSimulating && Component->IsAnyRigidBodyAwake() ? 1 : 0;
            }
        }
    }
}

void UPerformanceLabWorldSubsystem::TryCreateNiagaraCombatLoad()
{
    if (!bNiagaraCombatLoadRequested || bNiagaraCombatLoadCreated || !GetWorld())
    {
        return;
    }

    const APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
    const APawn* AnchorPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
    if (!AnchorPawn)
    {
        return;
    }

    static const TCHAR* CombatSystemPaths[] = {
        TEXT("/Game/Effects/Particles/Environmental/NS_ElectricMovement.NS_ElectricMovement"),
        TEXT("/Game/Effects/Particles/Explosion/NS_Grenade_Explosion.NS_Grenade_Explosion"),
        TEXT("/Game/Effects/Particles/Item/NS_Heal.NS_Heal"),
        TEXT("/Game/Effects/Particles/Impacts/NS_ImpactConcrete.NS_ImpactConcrete")
    };
    static const TCHAR* WeaponSystemPaths[] = {
        TEXT("/Game/Effects/Particles/Weapons/NS_WeaponFire.NS_WeaponFire"),
        TEXT("/Game/Effects/Particles/Weapons/NS_WeaponFire_ShellEject.NS_WeaponFire_ShellEject"),
        TEXT("/Game/Effects/Particles/Weapons/NS_WeaponFire_Tracer.NS_WeaponFire_Tracer"),
        TEXT("/Game/Effects/Particles/Impacts/NS_ImpactConcrete.NS_ImpactConcrete")
    };
    const TCHAR* const* SystemPaths = bWeaponVFXLoadRequested ? WeaponSystemPaths : CombatSystemPaths;
    const int32 SystemPathCount = bWeaponVFXLoadRequested ? UE_ARRAY_COUNT(WeaponSystemPaths) : UE_ARRAY_COUNT(CombatSystemPaths);
    NiagaraCombatSystems.Reset();
    for (int32 Index = 0; Index < SystemPathCount; ++Index)
    {
        if (UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, SystemPaths[Index]))
        {
            NiagaraCombatSystems.Add(System);
        }
    }
    if (NiagaraCombatSystems.Num() != SystemPathCount)
    {
        UE_LOG(LogTemp, Error, TEXT("[PerformanceLab] Niagara combat load resolved %d/%d systems."),
            NiagaraCombatSystems.Num(), SystemPathCount);
        NiagaraCombatSystems.Reset();
        return;
    }

    NiagaraViewOrigin = AnchorPawn->GetActorLocation();
    const FVector Forward = AnchorPawn->GetActorForwardVector().GetSafeNormal2D();
    NiagaraNearCenter = NiagaraViewOrigin + Forward * 1200.0 + FVector::UpVector * 120.0;
    NiagaraFarCenter = NiagaraViewOrigin + Forward * 3600.0 + FVector::UpVector * 120.0;
    NiagaraStatusComponents.Reset();
    if (bWeaponVFXLoadRequested)
    {
        WeaponVFXShellMesh = LoadObject<UStaticMesh>(nullptr,
            TEXT("/Game/Effects/Meshes/BulletShells/SM_rifleshell.SM_rifleshell"));
        bNiagaraCombatLoadCreated = WeaponVFXShellMesh != nullptr;
        UE_LOG(LogTemp, Display, TEXT("[PerformanceLab] Created fixed weapon VFX load with %.1f component events/s."),
            NiagaraBurstsPerSecond);
        return;
    }

    for (int32 Index = 0; Index < TargetNiagaraStatusSlotCount; ++Index)
    {
        const double Angle = 2.0 * PI * static_cast<double>(Index) / TargetNiagaraStatusSlotCount;
        const double Radius = 350.0 + 140.0 * (Index % 4);
        const FVector Center = (Index & 1) == 0 ? NiagaraNearCenter : NiagaraFarCenter;
        const FVector Location = Center + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 40.0 * (Index % 3));
        if (UNiagaraComponent* Component = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
            GetWorld(), NiagaraCombatSystems[0], Location, FRotator::ZeroRotator, FVector(0.65f),
            false, true, ENCPoolMethod::None, true))
        {
            NiagaraStatusComponents.Add(Component);
        }
    }

    NiagaraStatusActivationCount = NiagaraStatusComponents.Num();
    bNiagaraCombatLoadCreated = NiagaraStatusComponents.Num() >= TargetNiagaraStatusSlotCount;
    UE_LOG(LogTemp, Display, TEXT("[PerformanceLab] Created controlled Niagara/VFX load with %d/%d status slots and %.1f bursts/s."),
        NiagaraStatusComponents.Num(), TargetNiagaraStatusSlotCount, NiagaraBurstsPerSecond);
}

void UPerformanceLabWorldSubsystem::UpdateNiagaraCombatLoad(float DeltaTime)
{
    if (!bNiagaraCombatLoadCreated || NiagaraCombatSystems.Num() < 4)
    {
        return;
    }

    NiagaraBurstAccumulator += DeltaTime;
    NiagaraStatusRefreshAccumulator += DeltaTime;
    NiagaraStateSampleAccumulator += DeltaTime;
    const double BurstInterval = 1.0 / NiagaraBurstsPerSecond;
    while (NiagaraBurstAccumulator >= BurstInterval)
    {
        NiagaraBurstAccumulator -= BurstInterval;
        const int32 BurstIndex = NiagaraRequestedBurstCount++;
        const int32 SystemIndex = bWeaponVFXLoadRequested ? BurstIndex % 4 : 1 + BurstIndex % 3;
        const int32 ShotIndex = bWeaponVFXLoadRequested ? BurstIndex / 4 : BurstIndex;
        const double Angle = ShotIndex * 2.399963229728653;
        const double Radius = 180.0 + 55.0 * (BurstIndex % 12);
        const FVector Center = (ShotIndex & 1) == 0 ? NiagaraNearCenter : NiagaraFarCenter;
        const FVector MuzzleLocation = Center + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 20.0 + 35.0 * (ShotIndex % 4));
        const FVector ShotDirection = FVector(1.0, FMath::Sin(Angle) * 0.25, -0.05).GetSafeNormal();
        const FVector ImpactLocation = MuzzleLocation + ShotDirection * 900.0;
        const FVector Location = bWeaponVFXLoadRequested && SystemIndex == 3 ? ImpactLocation : MuzzleLocation;
        const float Scale = bWeaponVFXLoadRequested ? 0.75f : (SystemIndex == 1 ? 0.55f : (SystemIndex == 2 ? 0.70f : 0.60f));
        const bool bCritical = bWeaponVFXLoadRequested ? SystemIndex >= 2 : SystemIndex == 1;
        const bool bFar = FVector::DistSquared(Location, NiagaraViewOrigin) > FMath::Square(NiagaraFullEffectDistance);
        NiagaraCriticalRequestedCount += bCritical ? 1 : 0;
        if (bNiagaraBudgetRequested && bFar && !bCritical)
        {
            ++NiagaraCulledBurstCount;
        }
        else
        {
            UNiagaraComponent* SpawnedComponent = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
                GetWorld(), NiagaraCombatSystems[SystemIndex], Location, FRotator::ZeroRotator, FVector(Scale),
                true, true, ENCPoolMethod::AutoRelease, !bWeaponVFXLoadRequested);
            if (bWeaponVFXLoadRequested && SpawnedComponent)
            {
                SpawnedComponent->SetVariablePosition(TEXT("User.MuzzlePosition"), MuzzleLocation);
                SpawnedComponent->SetVariableVec3(TEXT("User.Direction"), ShotDirection);
                SpawnedComponent->SetVariableBool(TEXT("User.Trigger"), true);
                SpawnedComponent->SetVariableInt(TEXT("User.NumberOfHits"), 1);
                SpawnedComponent->SetVariableStaticMesh(TEXT("User.ShellEjectStaticMesh"), WeaponVFXShellMesh);
                UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayPosition(
                    SpawnedComponent, TEXT("User.ImpactPositions"), { ImpactLocation });
                UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(
                    SpawnedComponent, TEXT("User.ImpactNormals"), { -ShotDirection });
                SpawnedComponent->Activate(true);
            }
            ++NiagaraBurstSpawnCount;
            NiagaraCriticalSpawnedCount += bCritical ? 1 : 0;
        }
        NiagaraCriticalPreservationRatio = NiagaraCriticalRequestedCount > 0
            ? static_cast<double>(NiagaraCriticalSpawnedCount) / NiagaraCriticalRequestedCount
            : 1.0;
    }

    if (!bWeaponVFXLoadRequested && NiagaraStatusRefreshAccumulator >= 0.25)
    {
        NiagaraStatusRefreshAccumulator = FMath::Fmod(NiagaraStatusRefreshAccumulator, 0.25);
        const int32 Phase = FMath::FloorToInt(ElapsedSeconds / 0.25) % 4;
        const int32 FirstIndex = bNiagaraPersistentStatusReuseRequested ? 0 : Phase;
        const int32 IndexStride = bNiagaraPersistentStatusReuseRequested ? 1 : 4;
        for (int32 Index = FirstIndex; Index < NiagaraStatusComponents.Num(); Index += IndexStride)
        {
            if (UNiagaraComponent* Component = NiagaraStatusComponents[Index].Get())
            {
                const bool bFar = FVector::DistSquared(Component->GetComponentLocation(), NiagaraViewOrigin)
                    > FMath::Square(NiagaraFullEffectDistance);
                if ((!bNiagaraBudgetRequested || !bFar)
                    && (!bNiagaraPersistentStatusReuseRequested || !Component->IsActive()))
                {
                    Component->Activate(!bNiagaraPersistentStatusReuseRequested);
                    ++NiagaraStatusActivationCount;
                }
            }
        }
    }

    if (NiagaraStateSampleAccumulator >= 0.25)
    {
        NiagaraStateSampleAccumulator = FMath::Fmod(NiagaraStateSampleAccumulator, 0.25);
        NiagaraActiveComponentCount = 0;
        for (TObjectIterator<UNiagaraComponent> It; It; ++It)
        {
            if (It->GetWorld() == GetWorld() && It->IsActive())
            {
                ++NiagaraActiveComponentCount;
            }
        }
    }
}

void UPerformanceLabWorldSubsystem::CaptureWorldNiagaraSnapshot(bool bMeasurementStart)
{
    int32 RegisteredCount = 0;
    int32 ActiveCount = 0;
    TMap<FString, int32> ActiveSystems;
    TMap<FString, int32> ActiveOwnerClasses;
    UWorld* World = GetWorld();

    for (TObjectIterator<UNiagaraComponent> It; It; ++It)
    {
        UNiagaraComponent* Component = *It;
        if (!Component || Component->GetWorld() != World || !Component->IsRegistered())
        {
            continue;
        }

        ++RegisteredCount;
        if (Component->IsActive())
        {
            ++ActiveCount;
            const FString SystemPath = Component->GetAsset()
                ? Component->GetAsset()->GetPathName()
                : TEXT("<No Niagara System>");
            ++ActiveSystems.FindOrAdd(SystemPath);
            const AActor* Owner = Component->GetOwner();
            const FString OwnerClass = Owner
                ? Owner->GetClass()->GetPathName()
                : TEXT("<No Actor Owner>");
            ++ActiveOwnerClasses.FindOrAdd(OwnerClass);
        }
    }

    if (bMeasurementStart)
    {
        WorldNiagaraRegisteredAtStart = RegisteredCount;
        WorldNiagaraActiveAtStart = ActiveCount;
    }
    else
    {
        WorldNiagaraRegisteredAtEnd = RegisteredCount;
        WorldNiagaraActiveAtEnd = ActiveCount;
        WorldNiagaraActiveSystemsAtEnd = MoveTemp(ActiveSystems);
        WorldNiagaraActiveOwnerClassesAtEnd = MoveTemp(ActiveOwnerClasses);
    }
    NiagaraActiveComponentCount = ActiveCount;

    UE_LOG(LogTemp, Display, TEXT("[PerformanceLab] World Niagara snapshot (%s): %d active / %d registered."),
        bMeasurementStart ? TEXT("start") : TEXT("end"), ActiveCount, RegisteredCount);
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
    ReducedAttachedShadowPrimitiveCount = 0;
    RayTracingExcludedCharacterCount = 0;
    RayTracingExcludedAttachedPrimitiveCount = 0;
    DistanceCulledEquipmentPrimitiveCount = 0;
    CustomDepthCharacterCount = 0;
    ManagedSignificanceObjectCount = 0;
    AnimationSignificanceAppliedCount = 0;
    AnimationFullRateProtectedCount = 0;
    AnimationLocalPlayerProtectedCount = 0;
    AnimationUROEnabledCount = 0;
    AnimationFarFieldCount = 0;
    AnimationPostProcessDisabledCount = 0;
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
		ReducedAttachedShadowPrimitiveCount += It->GetReducedAttachedShadowPrimitiveCount();
		RayTracingExcludedCharacterCount += It->IsRayTracingExcluded() ? 1 : 0;
		RayTracingExcludedAttachedPrimitiveCount += It->GetRayTracingExcludedAttachedPrimitiveCount();
		DistanceCulledEquipmentPrimitiveCount += It->GetDistanceCulledEquipmentPrimitiveCount();
        if (Mesh && Mesh->bRenderCustomDepth)
        {
            ++CustomDepthCharacterCount;
        }
        if (It->HasAnimationLODSignificance())
        {
            ++AnimationSignificanceAppliedCount;
            const bool bProtected = It->IsAnimationFullRateProtected();
            AnimationFullRateProtectedCount += bProtected ? 1 : 0;
            AnimationLocalPlayerProtectedCount += bProtected && It->IsPlayerControlled() && It->IsLocallyControlled() ? 1 : 0;
			AnimationUROEnabledCount += It->IsAnimationUROEnabled() ? 1 : 0;
			AnimationFarFieldCount += It->IsAnimationFarField() ? 1 : 0;
			AnimationPostProcessDisabledCount += It->IsAnimationPostProcessDisabled() ? 1 : 0;
        }
        if (bHasViewLocation)
        {
            const double Distance = FVector::Distance(ViewLocation, It->GetActorLocation());
            LocalMinDistance = FMath::Min(LocalMinDistance, Distance);
            MaxCharacterViewDistance = FMath::Max(MaxCharacterViewDistance, Distance);
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
    if (Session.Dimension != TEXT("Navigation")
		&& !((Session.Dimension == TEXT("AnimationLOD") || Session.Dimension == TEXT("RenderingLOD")) && bNavigationLoadRequested))
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
		RecordMetric(TEXT("navMaxPendingWaitMs"), Snapshot.NavigationMaxPendingWaitMs);
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
    CaptureWorldNiagaraSnapshot(true);
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
        if (bDetailedFXStatsRequested && !bDetailedFXStatsApplied)
        {
            if (IConsoleVariable* MemoryMode = IConsoleManager::Get().FindConsoleVariable(TEXT("fx.DetailedCSVStats.MemoryMode")))
            {
                PreviousDetailedFXMemoryMode = MemoryMode->GetInt();
                MemoryMode->Set(0, ECVF_SetByCode);
            }
            if (IConsoleVariable* DetailedStats = IConsoleManager::Get().FindConsoleVariable(TEXT("fx.DetailedCSVStats")))
            {
                PreviousDetailedFXStats = DetailedStats->GetInt();
                DetailedStats->Set(1, ECVF_SetByCode);
                bDetailedFXStatsApplied = true;
            }
        }
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

    CaptureWorldNiagaraSnapshot(false);

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
        { TEXT("Exclusive/GameThread/UI"), TEXT("csvUITimeMs") },
		{ TEXT("HeroArenaHUD/GameThread/Refresh"), TEXT("csvHeroArenaHUDRefreshTimeMs") },
		{ TEXT("HeroArenaHUD/RefreshCount"), TEXT("csvHeroArenaHUDRefreshCount") },
        { TEXT("Exclusive/GameThread/BehaviorTreeTick"), TEXT("csvBehaviorTreeTickTimeMs") },
        { TEXT("Exclusive/GameThread/BehaviorTreeSearch"), TEXT("csvBehaviorTreeSearchTimeMs") },
        { TEXT("Exclusive/GameThread/AIPerception"), TEXT("csvAIPerceptionTimeMs") },
        { TEXT("Exclusive/GameThread/EnvQueryManager"), TEXT("csvEnvQueryTimeMs") },
        { TEXT("Exclusive/GameThread/Animation"), TEXT("csvAnimationTimeMs") },
		{ TEXT("Exclusive/GameThread/Physics"), TEXT("csvPhysicsGameThreadTimeMs") },
		{ TEXT("Exclusive/AllWorkers/Physics"), TEXT("csvPhysicsAllWorkersTimeMs") },
		{ TEXT("Exclusive/GameThread/EventWait/StartPhysics"), TEXT("csvStartPhysicsWaitTimeMs") },
		{ TEXT("Exclusive/GameThread/EventWait/EndPhysics"), TEXT("csvEndPhysicsWaitTimeMs") },
		{ TEXT("Exclusive/RenderThread/Niagara"), TEXT("csvNiagaraRenderThreadTimeMs") },
		{ TEXT("Exclusive/AllWorkers/Niagara"), TEXT("csvNiagaraAllWorkersTimeMs") },
		{ TEXT("Exclusive/RenderThread/FXSystem"), TEXT("csvFXSystemRenderThreadTimeMs") },
		{ TEXT("Exclusive/AllWorkers/FXSystem"), TEXT("csvFXSystemAllWorkersTimeMs") },
		{ TEXT("GPU/ShadowDepths"), TEXT("csvGpuShadowDepthsTimeMs") },
		{ TEXT("GPU/TemporalSuperResolution"), TEXT("csvGpuTemporalSuperResolutionTimeMs") },
		{ TEXT("GPU/LumenReflections"), TEXT("csvGpuLumenReflectionsTimeMs") },
		{ TEXT("GPU/NaniteVisBuffer"), TEXT("csvGpuNaniteVisBufferTimeMs") },
		{ TEXT("GPU/SlateUI"), TEXT("csvGpuSlateUITimeMs") },
		{ TEXT("GPU/NaniteBasePass"), TEXT("csvGpuNaniteBasePassTimeMs") },
		{ TEXT("GPU/CustomDepth"), TEXT("csvGpuCustomDepthTimeMs") },
		{ TEXT("GPU/Postprocessing"), TEXT("csvGpuPostProcessingTimeMs") },
		{ TEXT("GPU/LumenSceneUpdate"), TEXT("csvGpuLumenSceneUpdateTimeMs") },
		{ TEXT("GPU/Translucency"), TEXT("csvGpuTranslucencyTimeMs") },
		{ TEXT("GPU/RenderDeferredLighting"), TEXT("csvGpuDeferredLightingTimeMs") },
		{ TEXT("GPU/Lights"), TEXT("csvGpuLightsTimeMs") },
		{ TEXT("GPU/RayTracingDynamicGeometry"), TEXT("csvGpuRayTracingDynamicGeometryTimeMs") },
		{ TEXT("GPU/BasePass"), TEXT("csvGpuBasePassTimeMs") },
		{ TEXT("GPU/RayTracingScene"), TEXT("csvGpuRayTracingSceneTimeMs") },
		{ TEXT("GPU/NiagaraGPUSimulation"), TEXT("csvGpuNiagaraSimulationTimeMs") },
        { TEXT("LyraSignificance/GameThread/Update"), TEXT("csvSignificanceUpdateTimeMs") },
        { TEXT("Exclusive/GameThread/CharacterMovement"), TEXT("csvCharacterMovementTimeMs") },
        { TEXT("Exclusive/GameThread/NavigationBuild"), TEXT("csvNavigationTimeMs") }
        ,{ TEXT("NavigationBuildDetailed/GameThread/Navigation_CrowdManager"), TEXT("csvCrowdManagerTimeMs") }
        ,{ TEXT("Ticks/PathFollowingComponent"), TEXT("csvPathFollowingTickCount") }
        ,{ TEXT("Ticks/RecastNavMesh"), TEXT("csvRecastNavMeshTickCount") }
        ,{ TEXT("Ticks/PerformanceLabCrowdAIController"), TEXT("csvCrowdControllerTickCount") }
		,{ TEXT("Ticks/LyraWeaponSpawner"), TEXT("csvWeaponSpawnerTickCount") }
		,{ TEXT("Ticks/FNiagaraWorldManagerTickFunction"), TEXT("csvNiagaraWorldManagerTickCount") }
		,{ TEXT("Ticks/ParticleSystemManager"), TEXT("csvParticleSystemManagerTickCount") }
		,{ TEXT("LyraWeaponVFX/PresentationEvents"), TEXT("csvWeaponVFXPresentationEvents") }
		,{ TEXT("LyraWeaponVFX/FullDetailEvents"), TEXT("csvWeaponVFXFullDetailEvents") }
		,{ TEXT("LyraWeaponVFX/ReducedDetailEvents"), TEXT("csvWeaponVFXReducedDetailEvents") }
		,{ TEXT("LyraWeaponVFX/ShellEjectSuppressed"), TEXT("csvWeaponVFXShellSuppressed") }
		,{ TEXT("LyraWeaponVFX/MuzzleFlashSuppressed"), TEXT("csvWeaponVFXMuzzleSuppressed") }
		,{ TEXT("LyraWeaponVFX/MissingBudgetProperties"), TEXT("csvWeaponVFXMissingProperties") }
		,{ TEXT("Particles/Count/NS_GunPad"), TEXT("csvGunPadInstanceCount") }
		,{ TEXT("Particles/Total/NS_GunPad"), TEXT("csvGunPadCpuTimeMs") }
    };

    TMap<int32, FString> ColumnMetrics;
    for (int32 Column = 0; Column < Rows[0].Num(); ++Column)
    {
        if (const FString* MetricName = RequestedColumns.Find(Rows[0][Column]))
        {
            ColumnMetrics.Add(Column, *MetricName);
        }
    }

    int32 RejectedCsvSampleCount = 0;
    for (int32 RowIndex = 1; RowIndex < Rows.Num(); ++RowIndex)
    {
        const TArray<const TCHAR*>& Row = Rows[RowIndex];
		// UE appends a repeated header and metadata record after the frame rows.
		// Their numeric values include resolution and build fields, so they must
		// never participate in timing aggregates.
		if (!Row.IsEmpty() && Row[0]
			&& (FCString::Stricmp(Row[0], TEXT("EVENTS")) == 0 || Row[0][0] == TEXT('[')))
		{
			continue;
		}
        for (const TPair<int32, FString>& ColumnMetric : ColumnMetrics)
        {
            if (Row.IsValidIndex(ColumnMetric.Key) && Row[ColumnMetric.Key] && Row[ColumnMetric.Key][0] != 0)
            {
                double Value = 0.0;
                const bool bTimeMetric = ColumnMetric.Value.EndsWith(TEXT("TimeMs"));
                if (LexTryParseString(Value, Row[ColumnMetric.Key]) && FMath::IsFinite(Value))
                {
					if (ColumnMetric.Value == TEXT("csvGunPadCpuTimeMs"))
					{
						// Niagara detailed total columns are stored in microseconds.
						Value /= 1000.0;
					}
					if (FMath::Abs(Value) < (bTimeMetric ? 1000.0 : 1.0e12))
					{
						RecordMetric(ColumnMetric.Value, Value);
					}
					else
					{
						++RejectedCsvSampleCount;
					}
                }
                else
                {
                    ++RejectedCsvSampleCount;
                }
            }
        }
    }
    if (RejectedCsvSampleCount > 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PerformanceLab] Ignored %d invalid CSV profiler samples."), RejectedCsvSampleCount);
    }

    for (const FString& MetricName : {
        FString(TEXT("csvTickActorsTimeMs")),
        FString(TEXT("csvUITimeMs")),
		FString(TEXT("csvHeroArenaHUDRefreshTimeMs")),
		FString(TEXT("csvHeroArenaHUDRefreshCount")),
        FString(TEXT("csvBehaviorTreeTickTimeMs")),
        FString(TEXT("csvBehaviorTreeSearchTimeMs")),
        FString(TEXT("csvAIPerceptionTimeMs")),
        FString(TEXT("csvEnvQueryTimeMs")),
        FString(TEXT("csvAnimationTimeMs")),
		FString(TEXT("csvPhysicsGameThreadTimeMs")),
		FString(TEXT("csvPhysicsAllWorkersTimeMs")),
		FString(TEXT("csvStartPhysicsWaitTimeMs")),
		FString(TEXT("csvEndPhysicsWaitTimeMs")),
		FString(TEXT("csvNiagaraRenderThreadTimeMs")),
		FString(TEXT("csvNiagaraAllWorkersTimeMs")),
		FString(TEXT("csvFXSystemRenderThreadTimeMs")),
		FString(TEXT("csvFXSystemAllWorkersTimeMs")),
        FString(TEXT("csvSignificanceUpdateTimeMs")),
        FString(TEXT("csvCharacterMovementTimeMs")),
        FString(TEXT("csvNavigationTimeMs")),
        FString(TEXT("csvCrowdManagerTimeMs")),
        FString(TEXT("csvPathFollowingTickCount")),
        FString(TEXT("csvRecastNavMeshTickCount")),
		FString(TEXT("csvCrowdControllerTickCount")),
		FString(TEXT("csvNiagaraWorldManagerTickCount")),
		FString(TEXT("csvParticleSystemManagerTickCount")),
		FString(TEXT("csvWeaponVFXPresentationEvents")),
		FString(TEXT("csvWeaponVFXFullDetailEvents")),
		FString(TEXT("csvWeaponVFXReducedDetailEvents")),
		FString(TEXT("csvWeaponVFXShellSuppressed")),
		FString(TEXT("csvWeaponVFXMuzzleSuppressed")),
		FString(TEXT("csvWeaponVFXMissingProperties")) })
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
    Root->SetStringField(TEXT("schemaVersion"), TEXT("2.1"));
    Root->SetStringField(TEXT("runId"), Session.RunId);
    Root->SetStringField(TEXT("sessionId"), Session.SessionId);
    Root->SetStringField(TEXT("processRole"), Session.ProcessRole);
    Root->SetStringField(TEXT("presetId"), Session.PresetId);
    Root->SetStringField(TEXT("map"), GetWorld() ? GetWorld()->GetMapName() : TEXT("Unknown"));
    Root->SetStringField(TEXT("dimension"), Session.Dimension);
    Root->SetStringField(TEXT("variant"), Session.Variant);
    Root->SetStringField(TEXT("sourceRevision"), Session.SourceRevision);
    Root->SetStringField(TEXT("engineVersion"),
        FEngineVersion::Current().ToString(EVersionComponent::Changelist));
    Root->SetStringField(TEXT("startedUtc"), Session.StartedUtc);
    Root->SetNumberField(TEXT("seed"), Preset.Load.Seed);
    Root->SetNumberField(TEXT("targetAgentCount"), Preset.Load.AgentCount);
    Root->SetNumberField(TEXT("pawnCount"), PawnCount);
    Root->SetNumberField(TEXT("aiControllerCount"), AIControllerCount);
    Root->SetNumberField(TEXT("possessedAIPawnCount"), PossessedAIPawnCount);
    Root->SetNumberField(TEXT("lyraCharacterCount"), LyraCharacterCount);
    Root->SetNumberField(TEXT("shadowCastingCharacterCount"), ShadowCastingCharacterCount);
    Root->SetNumberField(TEXT("rayTracingExcludedCharacterCount"), RayTracingExcludedCharacterCount);
    Root->SetNumberField(TEXT("rayTracingExcludedAttachedPrimitiveCount"), RayTracingExcludedAttachedPrimitiveCount);
    Root->SetNumberField(TEXT("distanceCulledEquipmentPrimitiveCount"), DistanceCulledEquipmentPrimitiveCount);
    Root->SetNumberField(TEXT("customDepthCharacterCount"), CustomDepthCharacterCount);
    Root->SetNumberField(TEXT("managedSignificanceObjectCount"), ManagedSignificanceObjectCount);
    Root->SetNumberField(TEXT("netConnectionCount"), NetConnectionCount);
    Root->SetNumberField(TEXT("replicatedFixtureCount"), ReplicatedFixtureCount);
    Root->SetNumberField(TEXT("targetReplicatedFixtureCount"), TargetReplicatedFixtureCount);
    Root->SetBoolField(TEXT("physicsLoadEnabled"), bPhysicsLoadRequested);
    Root->SetBoolField(TEXT("physicsLODEnabled"), bPhysicsLODRequested);
    Root->SetNumberField(TEXT("targetPhysicsBodyCount"), TargetPhysicsBodyCount);
    Root->SetNumberField(TEXT("physicsBodyCount"), PhysicsBodyCount);
    Root->SetNumberField(TEXT("physicsAwakeBodyCount"), PhysicsAwakeBodyCount);
    Root->SetNumberField(TEXT("physicsSimulatedBodyCount"), PhysicsSimulatedBodyCount);
    Root->SetNumberField(TEXT("physicsLODDisabledBodyCount"), PhysicsLODDisabledBodyCount);
    Root->SetNumberField(TEXT("physicsFullSimulationDistance"), PhysicsFullSimulationDistance);
    Root->SetBoolField(TEXT("niagaraCombatLoadEnabled"), bNiagaraCombatLoadRequested);
    Root->SetBoolField(TEXT("weaponVFXLoadEnabled"), bWeaponVFXLoadRequested);
    Root->SetNumberField(TEXT("targetNiagaraStatusSlotCount"), TargetNiagaraStatusSlotCount);
    Root->SetNumberField(TEXT("niagaraStatusActivationCount"), NiagaraStatusActivationCount);
    Root->SetNumberField(TEXT("niagaraActiveComponentCount"), NiagaraActiveComponentCount);
    Root->SetNumberField(TEXT("niagaraRequestedBurstCount"), NiagaraRequestedBurstCount);
    Root->SetNumberField(TEXT("niagaraBurstSpawnCount"), NiagaraBurstSpawnCount);
    Root->SetNumberField(TEXT("niagaraCulledBurstCount"), NiagaraCulledBurstCount);
    Root->SetNumberField(TEXT("niagaraCriticalRequestedCount"), NiagaraCriticalRequestedCount);
    Root->SetNumberField(TEXT("niagaraCriticalSpawnedCount"), NiagaraCriticalSpawnedCount);
    Root->SetNumberField(TEXT("niagaraCriticalPreservationRatio"), NiagaraCriticalPreservationRatio);
    Root->SetNumberField(TEXT("worldNiagaraRegisteredAtStart"), WorldNiagaraRegisteredAtStart);
    Root->SetNumberField(TEXT("worldNiagaraActiveAtStart"), WorldNiagaraActiveAtStart);
    Root->SetNumberField(TEXT("worldNiagaraRegisteredAtEnd"), WorldNiagaraRegisteredAtEnd);
    Root->SetNumberField(TEXT("worldNiagaraActiveAtEnd"), WorldNiagaraActiveAtEnd);
    Root->SetNumberField(TEXT("niagaraBurstsPerSecond"), NiagaraBurstsPerSecond);
    Root->SetBoolField(TEXT("niagaraDistanceBudgetEnabled"), bNiagaraBudgetRequested);
    Root->SetBoolField(TEXT("niagaraPersistentStatusReuseEnabled"), bNiagaraPersistentStatusReuseRequested);
    Root->SetBoolField(TEXT("detailedFXStatsEnabled"), bDetailedFXStatsRequested);
    Root->SetNumberField(TEXT("niagaraFullEffectDistance"), NiagaraFullEffectDistance);
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
    Root->SetBoolField(TEXT("animationLODEnabled"), ULyraSignificanceManager::IsCharacterAnimationLODEnabled());
    Root->SetBoolField(TEXT("animationLODExplicitlyRequested"), bAnimationLODRequested);
    Root->SetBoolField(TEXT("characterRenderSignificanceEnabled"), ULyraSignificanceManager::IsCharacterRenderSignificanceEnabled());
    Root->SetBoolField(TEXT("characterRenderSignificanceExplicitlyRequested"), bCharacterRenderSignificanceRequested);
	Root->SetBoolField(TEXT("characterRayTracingLODEnabled"), ULyraSignificanceManager::IsCharacterRayTracingLODEnabled());
	Root->SetBoolField(TEXT("characterRayTracingLODExplicitlyRequested"), bCharacterRayTracingLODRequested);
	Root->SetBoolField(TEXT("characterEquipmentDistanceCullEnabled"), ULyraSignificanceManager::IsCharacterEquipmentDistanceCullEnabled());
	Root->SetBoolField(TEXT("characterEquipmentDistanceCullExplicitlyRequested"),
		FParse::Param(FCommandLine::Get(), TEXT("LyraCharacterEquipmentDistanceCull")));
    Root->SetBoolField(TEXT("characterCustomDepthLODEnabled"), ULyraSignificanceManager::IsCharacterCustomDepthLODEnabled());
    Root->SetBoolField(TEXT("characterCustomDepthLODExplicitlyRequested"), bCharacterCustomDepthLODRequested);
    Root->SetNumberField(TEXT("animationFullRateDistance"), ULyraSignificanceManager::GetCharacterAnimationFullRateDistance());
	Root->SetNumberField(TEXT("animationFarFieldDistance"), ULyraSignificanceManager::GetCharacterAnimationFarFieldDistance());
	Root->SetNumberField(TEXT("animationFarMinLOD"), ULyraSignificanceManager::GetCharacterAnimationFarMinLOD());
    Root->SetNumberField(TEXT("characterShadowMaxDistance"), ULyraSignificanceManager::GetCharacterShadowMaxDistance());
	Root->SetNumberField(TEXT("characterShadowDistanceHysteresis"), ULyraSignificanceManager::GetCharacterShadowDistanceHysteresis());
	Root->SetNumberField(TEXT("characterRayTracingMaxDistance"), ULyraSignificanceManager::GetCharacterRayTracingMaxDistance());
	Root->SetNumberField(TEXT("characterRayTracingDistanceHysteresis"), ULyraSignificanceManager::GetCharacterRayTracingDistanceHysteresis());
	Root->SetNumberField(TEXT("characterEquipmentMaxDrawDistance"), ULyraSignificanceManager::GetCharacterEquipmentMaxDrawDistance());
	Root->SetNumberField(TEXT("characterCustomDepthMaxDistance"), ULyraSignificanceManager::GetCharacterCustomDepthMaxDistance());
	Root->SetNumberField(TEXT("characterCustomDepthDistanceHysteresis"), ULyraSignificanceManager::GetCharacterCustomDepthDistanceHysteresis());
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

    TArray<TPair<FString, int32>> SortedNiagaraSystems;
    SortedNiagaraSystems.Reserve(WorldNiagaraActiveSystemsAtEnd.Num());
    for (const TPair<FString, int32>& Pair : WorldNiagaraActiveSystemsAtEnd)
    {
        SortedNiagaraSystems.Add(Pair);
    }
    SortedNiagaraSystems.Sort([](const TPair<FString, int32>& Left, const TPair<FString, int32>& Right)
    {
        return Left.Value == Right.Value ? Left.Key < Right.Key : Left.Value > Right.Value;
    });
    TArray<TSharedPtr<FJsonValue>> ActiveNiagaraSystems;
    for (int32 Index = 0; Index < FMath::Min(10, SortedNiagaraSystems.Num()); ++Index)
    {
        TSharedRef<FJsonObject> System = MakeShared<FJsonObject>();
        System->SetStringField(TEXT("system"), SortedNiagaraSystems[Index].Key);
        System->SetNumberField(TEXT("activeComponents"), SortedNiagaraSystems[Index].Value);
        if (const UNiagaraSystem* NiagaraSystem = FindObject<UNiagaraSystem>(nullptr, *SortedNiagaraSystems[Index].Key))
        {
            if (const UNiagaraEffectType* EffectType = NiagaraSystem->GetEffectType())
            {
                const FNiagaraSystemScalabilitySettings& Settings = EffectType->GetActiveSystemScalabilitySettings();
                System->SetStringField(TEXT("effectType"), EffectType->GetPathName());
                System->SetBoolField(TEXT("cullByDistance"), Settings.bCullByDistance != 0);
                System->SetNumberField(TEXT("maxDistance"), Settings.MaxDistance);
                System->SetBoolField(TEXT("cullByEffectTypeCount"), Settings.bCullMaxInstanceCount != 0);
                System->SetNumberField(TEXT("maxEffectTypeInstances"), Settings.MaxInstances);
                System->SetBoolField(TEXT("cullBySystemCount"), Settings.bCullPerSystemMaxInstanceCount != 0);
                System->SetNumberField(TEXT("maxSystemInstances"), Settings.MaxSystemInstances);
            }
            else
            {
                System->SetStringField(TEXT("effectType"), TEXT(""));
            }
        }
        ActiveNiagaraSystems.Add(MakeShared<FJsonValueObject>(System));
    }
    Root->SetArrayField(TEXT("activeNiagaraSystemsAtEnd"), ActiveNiagaraSystems);

    TArray<TPair<FString, int32>> SortedNiagaraOwners;
    SortedNiagaraOwners.Reserve(WorldNiagaraActiveOwnerClassesAtEnd.Num());
    for (const TPair<FString, int32>& Pair : WorldNiagaraActiveOwnerClassesAtEnd)
    {
        SortedNiagaraOwners.Add(Pair);
    }
    SortedNiagaraOwners.Sort([](const TPair<FString, int32>& Left, const TPair<FString, int32>& Right)
    {
        return Left.Value == Right.Value ? Left.Key < Right.Key : Left.Value > Right.Value;
    });
    TArray<TSharedPtr<FJsonValue>> ActiveNiagaraOwners;
    for (int32 Index = 0; Index < FMath::Min(10, SortedNiagaraOwners.Num()); ++Index)
    {
        TSharedRef<FJsonObject> Owner = MakeShared<FJsonObject>();
        Owner->SetStringField(TEXT("ownerClass"), SortedNiagaraOwners[Index].Key);
        Owner->SetNumberField(TEXT("activeComponents"), SortedNiagaraOwners[Index].Value);
        ActiveNiagaraOwners.Add(MakeShared<FJsonValueObject>(Owner));
    }
    Root->SetArrayField(TEXT("activeNiagaraOwnerClassesAtEnd"), ActiveNiagaraOwners);

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
