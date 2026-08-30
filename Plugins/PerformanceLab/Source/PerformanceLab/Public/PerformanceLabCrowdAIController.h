#pragma once

#include "Player/LyraPlayerBotController.h"
#include "PerformanceLabCrowdAIController.generated.h"

class APerformanceLabCrowdAIController;

UENUM()
enum class EPerformanceLabCrowdAIState : uint8
{
    Acquire,
    Chase,
    Engage
};

/**
 * Controlled Crowd 200 state-machine agent. Baseline and optimized variants
 * share this behavior; only target discovery is exchanged by the subsystem.
 */
UCLASS()
class PERFORMANCELAB_API APerformanceLabCrowdAIController : public ALyraPlayerBotController
{
    GENERATED_BODY()

public:
    APerformanceLabCrowdAIController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void OnPossess(APawn* InPawn) override;
    virtual void OnUnPossess() override;

    EPerformanceLabCrowdAIState GetCrowdState() const { return CrowdState; }
    AActor* GetCrowdTarget() const { return TargetActor.Get(); }
    bool HasValidNavigationRoute() const { return bNavigationRouteValid; }
    bool IsNavigationStuck() const { return bNavigationStuck; }
    double GetNavigationDistanceToGoal() const;
    int64 GetNavigationRequestCount() const { return NavigationRequestCount; }
    int64 GetNavigationReplanCount() const { return NavigationReplanCount; }
    int64 GetNavigationFailureCount() const { return NavigationFailureCount; }
    int64 GetNavigationArrivalCount() const { return NavigationArrivalCount; }
    double GetNavigationDecisionWorkTimeTotalMs() const { return NavigationDecisionWorkTimeTotalMs; }
    void ExecuteNavigationMove(const FVector& Goal);

private:
    void RunDecision();
    void RunNavigationDecision();
    void InitializeNavigationRoute();
    FVector GetCurrentNavigationGoal() const;
    void SetCrowdTarget(AActor* NewTarget);

    TWeakObjectPtr<AActor> TargetActor;
    EPerformanceLabCrowdAIState CrowdState = EPerformanceLabCrowdAIState::Acquire;
    double DecisionAccumulator = 0.0;
    double DecisionIntervalSeconds = 0.2;
    bool bNavigationLoad = false;
    bool bNavigationBudget = false;
    bool bNavigationRouteInitialized = false;
    bool bNavigationRouteValid = false;
    bool bNavigationStuck = false;
    int32 NavigationAgentIndex = INDEX_NONE;
    int32 NavigationRouteIndex = 1;
    TArray<FVector> NavigationRoute;
    FVector LastSubmittedNavigationGoal = FVector(UE_BIG_NUMBER);
    FVector StuckWindowStartLocation = FVector::ZeroVector;
    double LastNavigationRequestTime = -UE_BIG_NUMBER;
    double StuckWindowStartTime = 0.0;
    int64 NavigationRequestCount = 0;
    int64 NavigationReplanCount = 0;
    int64 NavigationFailureCount = 0;
    int64 NavigationArrivalCount = 0;
    double NavigationDecisionWorkTimeTotalMs = 0.0;
};
