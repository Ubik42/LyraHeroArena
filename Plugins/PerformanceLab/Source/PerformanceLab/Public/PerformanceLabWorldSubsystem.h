#pragma once

#include "Async/Future.h"
#include "PerformanceExperimentTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "PerformanceLabWorldSubsystem.generated.h"

class AActor;
class UBrainComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * Runtime contract for reproducible performance runs.
 * Enable with -PerfLab. The subsystem waits for the requested number of real
 * AI controllers and possessed pawns before measuring, then writes a JSON
 * manifest and summary under Saved/PerformanceLab/Runs.
 */
UCLASS()
class PERFORMANCELAB_API UPerformanceLabWorldSubsystem final : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual bool IsTickableInEditor() const override { return false; }

private:
    void ReadCommandLine();
    void CountAgents();
    void UpdateAIDecisionPolicy(float DeltaTime);
    void RestoreAIDecisionPolicy();
    void RecordAIDecisionMetrics();
    void RecordNavigationMetrics();
    void ImportCsvMetrics();
    void TrySpawnReplicatedFixture();
    void TryCreatePhysicsLoad();
    void UpdatePhysicsLoad(float DeltaTime);
    void TryCreateNiagaraCombatLoad();
    void UpdateNiagaraCombatLoad(float DeltaTime);
    void CaptureWorldNiagaraSnapshot(bool bMeasurementStart);
    void RecordNetworkMetrics();
    void LockInitialPlayerView();
    void BeginMeasurement();
    void FinishMeasurement(bool bTimedOut);
    void RecordMetric(const FString& Name, double Value);
    double GetMetricAggregate(const FString& Name, const FString& Aggregate) const;
    bool ResolveGuardMetric(const FString& Metric, double& OutValue) const;
    void EvaluateGuards();
    bool AreRequiredGuardsPassing() const;
    void WriteRunJson(const TCHAR* FileName, bool bFinal, bool bTimedOut) const;
    FString GetRunDirectory() const;

    bool bEnabled = false;
    bool bMeasuring = false;
    bool bFinished = false;
    bool bAutoExit = false;
    bool bCsvCaptureStarted = false;
    bool bExitRequested = false;
    bool bArtifactsFinalized = false;
    bool bTimedOutResult = false;
    bool bInitialViewLocked = false;
    bool bNetworkReadyArtifactWritten = false;
    bool bAIDecisionBudgetEnabled = false;
    bool bActiveAIRequested = false;
    bool bCrowdStateMachineRequested = false;
    bool bSharedBattleFactsRequested = false;
    bool bNavigationLoadRequested = false;
    bool bNavigationBudgetRequested = false;
    bool bAnimationLODRequested = false;
    bool bCharacterRenderSignificanceRequested = false;
    bool bCharacterRayTracingLODRequested = false;
    bool bCharacterCustomDepthLODRequested = false;
    bool bPhysicsLoadRequested = false;
    bool bPhysicsLODRequested = false;
    bool bPhysicsLoadCreated = false;
    bool bNiagaraCombatLoadRequested = false;
    bool bWeaponVFXLoadRequested = false;
    bool bNiagaraBudgetRequested = false;
    bool bNiagaraPersistentStatusReuseRequested = false;
    bool bNiagaraCombatLoadCreated = false;
    bool bDetailedFXStatsRequested = false;
    bool bDetailedFXStatsApplied = false;
    int32 PreviousDetailedFXStats = 0;
    int32 PreviousDetailedFXMemoryMode = 1;
    int32 PawnCount = 0;
    int32 AIControllerCount = 0;
    int32 PossessedAIPawnCount = 0;
    int32 LyraCharacterCount = 0;
    int32 ShadowCastingCharacterCount = 0;
    int32 ReducedAttachedShadowPrimitiveCount = 0;
    int32 RayTracingExcludedCharacterCount = 0;
    int32 RayTracingExcludedAttachedPrimitiveCount = 0;
    int32 DistanceCulledEquipmentPrimitiveCount = 0;
    int32 CustomDepthCharacterCount = 0;
    int32 ManagedSignificanceObjectCount = 0;
    int32 AnimationSignificanceAppliedCount = 0;
    int32 AnimationFullRateProtectedCount = 0;
    int32 AnimationLocalPlayerProtectedCount = 0;
    int32 AnimationUROEnabledCount = 0;
    int32 AnimationFarFieldCount = 0;
    int32 AnimationPostProcessDisabledCount = 0;
    int32 TargetPhysicsBodyCount = 300;
    int32 PhysicsBodyCount = 0;
    int32 PhysicsAwakeBodyCount = 0;
    int32 PhysicsSimulatedBodyCount = 0;
    int32 PhysicsLODDisabledBodyCount = 0;
    int32 TargetNiagaraStatusSlotCount = 24;
    int32 NiagaraStatusActivationCount = 0;
    int32 NiagaraActiveComponentCount = 0;
    int32 NiagaraRequestedBurstCount = 0;
    int32 NiagaraBurstSpawnCount = 0;
    int32 NiagaraCulledBurstCount = 0;
    int32 NiagaraCriticalRequestedCount = 0;
    int32 NiagaraCriticalSpawnedCount = 0;
    int32 WorldNiagaraRegisteredAtStart = 0;
    int32 WorldNiagaraActiveAtStart = 0;
    int32 WorldNiagaraRegisteredAtEnd = 0;
    int32 WorldNiagaraActiveAtEnd = 0;
    int32 NetConnectionCount = 0;
    int32 TargetConnectionCount = 0;
    int32 ReplicatedFixtureCount = 0;
    int32 TargetReplicatedFixtureCount = 0;
    int32 AIBrainComponentCount = 0;
    int32 AIRunningBrainCount = 0;
    int32 AIFullRateBrainCount = 0;
    int32 AIReducedRateBrainCount = 0;
    int32 AIFocusedControllerCount = 0;
    int32 AIMovingPawnCount = 0;
    int32 CrowdRegisteredAgentCount = 0;
    int32 CrowdTargetedAgentCount = 0;
    int32 CrowdChasingAgentCount = 0;
    int32 CrowdEngagingAgentCount = 0;
    int64 CrowdDecisionCount = 0;
    int64 CrowdCandidateCheckCount = 0;
    double CrowdSharedFactsUpdateTimeMs = 0.0;
    double CrowdTargetQueryTimeMs = 0.0;
    double CrowdSharedFactsWorkTimeMs = 0.0;
    double CrowdDecisionWorkTimeMs = 0.0;
    int64 PreviousCrowdDecisionCount = 0;
    int64 PreviousCrowdCandidateCheckCount = 0;
    double PreviousCrowdTargetQueryTimeTotalMs = 0.0;
    double PreviousCrowdSharedFactsTimeTotalMs = 0.0;
    int32 NavigationValidRouteCount = 0;
    int32 NavigationMovingAgentCount = 0;
    int32 NavigationStuckAgentCount = 0;
    int32 NavigationPendingRequestCount = 0;
    int64 NavigationRequestCount = 0;
    int64 NavigationReplanCount = 0;
    int64 NavigationFailureCount = 0;
    int64 NavigationArrivalCount = 0;
    double NavigationAverageDistanceToGoal = 0.0;
    double NavigationFailureRatio = 0.0;
    double NavigationBudgetWorkTimeMs = 0.0;
    double NavigationDecisionWorkTimeMs = 0.0;
    int64 PreviousNavigationRequestCount = 0;
    int64 PreviousNavigationReplanCount = 0;
    int64 PreviousNavigationFailureCount = 0;
    int64 PreviousNavigationArrivalCount = 0;
    double PreviousNavigationBudgetWorkTimeTotalMs = 0.0;
    double PreviousNavigationDecisionWorkTimeTotalMs = 0.0;
    int64 MeasurementStartNavigationRequestCount = 0;
    int64 MeasurementStartNavigationReplanCount = 0;
    int64 MeasurementStartNavigationFailureCount = 0;
    int64 MeasurementStartNavigationArrivalCount = 0;
    double ReplicatedFixtureSpacing = 4000.0;
    double MinCharacterViewDistance = 0.0;
    double MaxCharacterViewDistance = 0.0;
    int32 SampleCount = 0;
    double ElapsedSeconds = 0.0;
    double MeasurementElapsedSeconds = 0.0;
    double RunStartWallSeconds = 0.0;
    double AgentsReadyWallSeconds = -1.0;
    double AIPolicyUpdateAccumulator = 0.0;
    double AIMaxDecisionBudgetMs = 0.0;
    double AIPolicyUpdateTimeMs = 0.0;
    double PhysicsImpulseAccumulator = 0.0;
    double PhysicsStateSampleAccumulator = 0.0;
    double PhysicsFullSimulationDistance = 2400.0;
    double NiagaraBurstsPerSecond = 12.0;
    double NiagaraBurstAccumulator = 0.0;
    double NiagaraStatusRefreshAccumulator = 0.0;
    double NiagaraStateSampleAccumulator = 0.0;
    double NiagaraFullEffectDistance = 2400.0;
    double NiagaraCriticalPreservationRatio = 1.0;
    double FrameTimeSumMs = 0.0;
    double MinFrameTimeMs = TNumericLimits<double>::Max();
    double MaxFrameTimeMs = 0.0;
    FString RHIName;
    FString NetModeName;
    FString ReplicationDriverName;
    FString ReplicatedFixtureClassPath;
    TSubclassOf<AActor> ReplicatedFixtureClass;
    TMap<FString, TArray<double>> MetricSamples;
    TMap<FString, int32> WorldNiagaraActiveSystemsAtEnd;
    TMap<FString, int32> WorldNiagaraActiveOwnerClassesAtEnd;
    TMap<TWeakObjectPtr<UBrainComponent>, double> AIBrainAccumulators;
    TMap<TWeakObjectPtr<UBrainComponent>, float> AIBrainDecisionIntervals;
    TArray<TWeakObjectPtr<UStaticMeshComponent>> PhysicsBodyComponents;
    TArray<TWeakObjectPtr<UNiagaraComponent>> NiagaraStatusComponents;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UNiagaraSystem>> NiagaraCombatSystems;
    UPROPERTY(Transient)
    TObjectPtr<UStaticMesh> WeaponVFXShellMesh;
    FVector NiagaraViewOrigin = FVector::ZeroVector;
    FVector NiagaraNearCenter = FVector::ZeroVector;
    FVector NiagaraFarCenter = FVector::ZeroVector;
    TWeakObjectPtr<AActor> LockedViewActor;
    FPerformanceExperimentPresetData Preset;
    FPerformanceExperimentSession Session;
    TArray<FPerformanceGuardResult> GuardResults;
    TSharedPtr<TSharedFuture<FString>> CsvWriteFuture;
};
