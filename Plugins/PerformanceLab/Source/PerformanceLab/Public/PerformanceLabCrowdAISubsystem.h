#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "PerformanceLabCrowdAISubsystem.generated.h"

class APerformanceLabCrowdAIController;

struct FPerformanceLabCrowdAISnapshot
{
    int32 RegisteredAgents = 0;
    int32 TargetedAgents = 0;
    int32 ChasingAgents = 0;
    int32 EngagingAgents = 0;
    int64 DecisionCount = 0;
    int64 CandidateCheckCount = 0;
    double TargetQueryTimeTotalMs = 0.0;
    double SharedFactsUpdateTimeMs = 0.0;
    double SharedFactsUpdateTimeTotalMs = 0.0;
    int64 SharedFactsUpdateCount = 0;
    int32 NavigationValidRouteCount = 0;
    int32 NavigationMovingAgentCount = 0;
    int32 NavigationStuckAgentCount = 0;
    int32 NavigationPendingRequestCount = 0;
	double NavigationMaxPendingWaitMs = 0.0;
    int64 NavigationRequestCount = 0;
    int64 NavigationReplanCount = 0;
    int64 NavigationFailureCount = 0;
    int64 NavigationArrivalCount = 0;
    double NavigationAverageDistanceToGoal = 0.0;
    double NavigationBudgetWorkTimeTotalMs = 0.0;
    double NavigationDecisionWorkTimeTotalMs = 0.0;
};

/** Provides the interchangeable target-query implementations for C200-AI. */
UCLASS()
class PERFORMANCELAB_API UPerformanceLabCrowdAISubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;

    int32 RegisterAgent(APerformanceLabCrowdAIController* Controller);
    void UnregisterAgent(APerformanceLabCrowdAIController* Controller);
    AActor* FindTarget(APerformanceLabCrowdAIController* Requester);
    void QueueNavigationMove(APerformanceLabCrowdAIController* Controller, const FVector& Goal);
    FPerformanceLabCrowdAISnapshot GetSnapshot() const;
    bool UsesSharedBattleFacts() const { return bUseSharedBattleFacts; }

private:
    void RebuildSharedBattleFacts();
    void ProcessNavigationRequestBudget();
    AActor* FindTargetByFullScan(APerformanceLabCrowdAIController* Requester);

    TArray<TWeakObjectPtr<APerformanceLabCrowdAIController>> Agents;
    TMap<TWeakObjectPtr<APerformanceLabCrowdAIController>, TWeakObjectPtr<AActor>> SharedTargets;
    bool bUseSharedBattleFacts = false;
    double SharedFactsAccumulator = 0.0;
    double SharedFactsUpdateTimeMs = 0.0;
    int64 DecisionCount = 0;
    int64 CandidateCheckCount = 0;
    double TargetQueryTimeTotalMs = 0.0;
    double SharedFactsUpdateTimeTotalMs = 0.0;
    int64 SharedFactsUpdateCount = 0;
    struct FPendingNavigationMove
    {
        TWeakObjectPtr<APerformanceLabCrowdAIController> Controller;
        FVector Goal = FVector::ZeroVector;
		double QueuedAtSeconds = 0.0;
		bool bStuckRecovery = false;
    };
    TArray<FPendingNavigationMove> PendingNavigationMoves;
    bool bUseNavigationBudget = false;
    double NavigationBudgetWorkTimeTotalMs = 0.0;
};
