#include "PerformanceLabCrowdAISubsystem.h"

#include "PerformanceLabCrowdAIController.h"
#include "GenericTeamAgentInterface.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Navigation/PathFollowingComponent.h"

namespace
{
    constexpr double SharedFactsIntervalSeconds = 0.2;
    constexpr double SpatialCellSize = 1200.0;

    FIntPoint ToCell(const FVector& Location)
    {
        return FIntPoint(
            FMath::FloorToInt(Location.X / SpatialCellSize),
            FMath::FloorToInt(Location.Y / SpatialCellSize));
    }
}

void UPerformanceLabCrowdAISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    bUseSharedBattleFacts = FParse::Param(FCommandLine::Get(), TEXT("PerfLabSharedBattleFacts"));
    bUseNavigationBudget = FParse::Param(FCommandLine::Get(), TEXT("PerfLabNavigationBudget"));
}

void UPerformanceLabCrowdAISubsystem::Tick(float DeltaTime)
{
    if (bUseNavigationBudget)
    {
        ProcessNavigationRequestBudget();
    }
    if (!bUseSharedBattleFacts)
    {
        return;
    }

    SharedFactsAccumulator += DeltaTime;
    if (SharedFactsAccumulator >= SharedFactsIntervalSeconds)
    {
        SharedFactsAccumulator = FMath::Fmod(SharedFactsAccumulator, SharedFactsIntervalSeconds);
        RebuildSharedBattleFacts();
    }
}

TStatId UPerformanceLabCrowdAISubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UPerformanceLabCrowdAISubsystem, STATGROUP_Tickables);
}

int32 UPerformanceLabCrowdAISubsystem::RegisterAgent(APerformanceLabCrowdAIController* Controller)
{
    const int32 ExistingIndex = Agents.IndexOfByKey(Controller);
    if (ExistingIndex != INDEX_NONE)
    {
        return ExistingIndex;
    }
    return Agents.Add(Controller);
}

void UPerformanceLabCrowdAISubsystem::UnregisterAgent(APerformanceLabCrowdAIController* Controller)
{
    Agents.Remove(Controller);
    SharedTargets.Remove(Controller);
    PendingNavigationMoves.RemoveAllSwap([Controller](const FPendingNavigationMove& Request)
    {
        return Request.Controller.Get() == Controller;
    });
}

void UPerformanceLabCrowdAISubsystem::QueueNavigationMove(APerformanceLabCrowdAIController* Controller, const FVector& Goal)
{
    if (!Controller)
    {
        return;
    }
    if (FPendingNavigationMove* Existing = PendingNavigationMoves.FindByPredicate([Controller](const FPendingNavigationMove& Request)
    {
        return Request.Controller.Get() == Controller;
    }))
    {
        Existing->Goal = Goal;
        return;
    }
    FPendingNavigationMove& Request = PendingNavigationMoves.AddDefaulted_GetRef();
    Request.Controller = Controller;
    Request.Goal = Goal;
}

void UPerformanceLabCrowdAISubsystem::ProcessNavigationRequestBudget()
{
    const double StartSeconds = FPlatformTime::Seconds();
    constexpr int32 MaxRequestsPerFrame = 12;
    int32 Processed = 0;
    while (!PendingNavigationMoves.IsEmpty() && Processed < MaxRequestsPerFrame)
    {
        const FPendingNavigationMove Request = PendingNavigationMoves[0];
        PendingNavigationMoves.RemoveAt(0, 1, EAllowShrinking::No);
        if (APerformanceLabCrowdAIController* Controller = Request.Controller.Get())
        {
            Controller->ExecuteNavigationMove(Request.Goal);
        }
        ++Processed;
    }
    NavigationBudgetWorkTimeTotalMs += (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
}

AActor* UPerformanceLabCrowdAISubsystem::FindTarget(APerformanceLabCrowdAIController* Requester)
{
    const double StartSeconds = FPlatformTime::Seconds();
    ++DecisionCount;
    AActor* Result = nullptr;
    if (bUseSharedBattleFacts)
    {
        if (const TWeakObjectPtr<AActor>* Target = SharedTargets.Find(Requester))
        {
            Result = Target->Get();
        }
    }
    else
    {
        Result = FindTargetByFullScan(Requester);
    }
    TargetQueryTimeTotalMs += (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
    return Result;
}

AActor* UPerformanceLabCrowdAISubsystem::FindTargetByFullScan(APerformanceLabCrowdAIController* Requester)
{
    APawn* RequesterPawn = Requester ? Requester->GetPawn() : nullptr;
    if (!RequesterPawn)
    {
        return nullptr;
    }

    const FGenericTeamId RequesterTeam = Requester->GetGenericTeamId();
    double BestDistanceSquared = TNumericLimits<double>::Max();
    AActor* BestTarget = nullptr;
    for (const TWeakObjectPtr<APerformanceLabCrowdAIController>& CandidatePtr : Agents)
    {
        APerformanceLabCrowdAIController* Candidate = CandidatePtr.Get();
        APawn* CandidatePawn = Candidate ? Candidate->GetPawn() : nullptr;
        if (!CandidatePawn || Candidate == Requester || Candidate->GetGenericTeamId() == RequesterTeam)
        {
            continue;
        }
        ++CandidateCheckCount;
        const double DistanceSquared = FVector::DistSquared2D(RequesterPawn->GetActorLocation(), CandidatePawn->GetActorLocation());
        if (DistanceSquared < BestDistanceSquared)
        {
            BestDistanceSquared = DistanceSquared;
            BestTarget = CandidatePawn;
        }
    }
    return BestTarget;
}

void UPerformanceLabCrowdAISubsystem::RebuildSharedBattleFacts()
{
    const double StartSeconds = FPlatformTime::Seconds();
    Agents.RemoveAllSwap([](const TWeakObjectPtr<APerformanceLabCrowdAIController>& Agent) { return !Agent.IsValid(); });
    SharedTargets.Reset();

    TMap<FIntPoint, TArray<APerformanceLabCrowdAIController*>> Cells;
    for (const TWeakObjectPtr<APerformanceLabCrowdAIController>& AgentPtr : Agents)
    {
        if (APerformanceLabCrowdAIController* Agent = AgentPtr.Get())
        {
            if (const APawn* Pawn = Agent->GetPawn())
            {
                Cells.FindOrAdd(ToCell(Pawn->GetActorLocation())).Add(Agent);
            }
        }
    }

    for (const TWeakObjectPtr<APerformanceLabCrowdAIController>& RequesterPtr : Agents)
    {
        APerformanceLabCrowdAIController* Requester = RequesterPtr.Get();
        APawn* RequesterPawn = Requester ? Requester->GetPawn() : nullptr;
        if (!RequesterPawn)
        {
            continue;
        }

        const FGenericTeamId RequesterTeam = Requester->GetGenericTeamId();
        const FIntPoint OriginCell = ToCell(RequesterPawn->GetActorLocation());
        double BestDistanceSquared = TNumericLimits<double>::Max();
        AActor* BestTarget = nullptr;
        for (int32 Radius = 0; Radius <= 3 && !BestTarget; ++Radius)
        {
            for (int32 X = -Radius; X <= Radius; ++X)
            {
                for (int32 Y = -Radius; Y <= Radius; ++Y)
                {
                    if (Radius > 0 && FMath::Abs(X) != Radius && FMath::Abs(Y) != Radius)
                    {
                        continue;
                    }
                    const TArray<APerformanceLabCrowdAIController*>* Bucket = Cells.Find(OriginCell + FIntPoint(X, Y));
                    if (!Bucket)
                    {
                        continue;
                    }
                    for (APerformanceLabCrowdAIController* Candidate : *Bucket)
                    {
                        APawn* CandidatePawn = Candidate ? Candidate->GetPawn() : nullptr;
                        if (!CandidatePawn || Candidate == Requester || Candidate->GetGenericTeamId() == RequesterTeam)
                        {
                            continue;
                        }
                        ++CandidateCheckCount;
                        const double DistanceSquared = FVector::DistSquared2D(RequesterPawn->GetActorLocation(), CandidatePawn->GetActorLocation());
                        if (DistanceSquared < BestDistanceSquared)
                        {
                            BestDistanceSquared = DistanceSquared;
                            BestTarget = CandidatePawn;
                        }
                    }
                }
            }
        }
        SharedTargets.Add(Requester, BestTarget);
    }
    SharedFactsUpdateTimeMs = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
    SharedFactsUpdateTimeTotalMs += SharedFactsUpdateTimeMs;
    ++SharedFactsUpdateCount;
}

FPerformanceLabCrowdAISnapshot UPerformanceLabCrowdAISubsystem::GetSnapshot() const
{
    FPerformanceLabCrowdAISnapshot Snapshot;
    Snapshot.RegisteredAgents = Agents.Num();
    Snapshot.DecisionCount = DecisionCount;
    Snapshot.CandidateCheckCount = CandidateCheckCount;
    Snapshot.TargetQueryTimeTotalMs = TargetQueryTimeTotalMs;
    Snapshot.SharedFactsUpdateTimeMs = SharedFactsUpdateTimeMs;
    Snapshot.SharedFactsUpdateTimeTotalMs = SharedFactsUpdateTimeTotalMs;
    Snapshot.SharedFactsUpdateCount = SharedFactsUpdateCount;
    Snapshot.NavigationPendingRequestCount = PendingNavigationMoves.Num();
    Snapshot.NavigationBudgetWorkTimeTotalMs = NavigationBudgetWorkTimeTotalMs;
    double TotalDistance = 0.0;
    for (const TWeakObjectPtr<APerformanceLabCrowdAIController>& AgentPtr : Agents)
    {
        const APerformanceLabCrowdAIController* Agent = AgentPtr.Get();
        if (!Agent)
        {
            continue;
        }
        Snapshot.TargetedAgents += Agent->GetCrowdTarget() != nullptr ? 1 : 0;
        Snapshot.ChasingAgents += Agent->GetCrowdState() == EPerformanceLabCrowdAIState::Chase ? 1 : 0;
        Snapshot.EngagingAgents += Agent->GetCrowdState() == EPerformanceLabCrowdAIState::Engage ? 1 : 0;
        if (Agent->HasValidNavigationRoute())
        {
            ++Snapshot.NavigationValidRouteCount;
            TotalDistance += Agent->GetNavigationDistanceToGoal();
        }
        Snapshot.NavigationMovingAgentCount += Agent->GetMoveStatus() == EPathFollowingStatus::Moving ? 1 : 0;
        Snapshot.NavigationStuckAgentCount += Agent->IsNavigationStuck() ? 1 : 0;
        Snapshot.NavigationRequestCount += Agent->GetNavigationRequestCount();
        Snapshot.NavigationReplanCount += Agent->GetNavigationReplanCount();
        Snapshot.NavigationFailureCount += Agent->GetNavigationFailureCount();
        Snapshot.NavigationArrivalCount += Agent->GetNavigationArrivalCount();
        Snapshot.NavigationDecisionWorkTimeTotalMs += Agent->GetNavigationDecisionWorkTimeTotalMs();
    }
    Snapshot.NavigationAverageDistanceToGoal = Snapshot.NavigationValidRouteCount > 0
        ? TotalDistance / Snapshot.NavigationValidRouteCount
        : 0.0;
    return Snapshot;
}
