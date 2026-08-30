#include "PerformanceLabCrowdAIController.h"

#include "PerformanceLabCrowdAISubsystem.h"
#include "AITypes.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"

APerformanceLabCrowdAIController::APerformanceLabCrowdAIController(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    PrimaryActorTick.bCanEverTick = true;
}

void APerformanceLabCrowdAIController::BeginPlay()
{
    Super::BeginPlay();
    bNavigationLoad = FParse::Param(FCommandLine::Get(), TEXT("PerfLabNavigationLoad"));
    bNavigationBudget = FParse::Param(FCommandLine::Get(), TEXT("PerfLabNavigationBudget"));
    DecisionAccumulator = (static_cast<double>(GetUniqueID() % 20) / 20.0) * DecisionIntervalSeconds;
    if (UPerformanceLabCrowdAISubsystem* Subsystem = GetWorld()->GetSubsystem<UPerformanceLabCrowdAISubsystem>())
    {
        NavigationAgentIndex = Subsystem->RegisterAgent(this);
    }
}

void APerformanceLabCrowdAIController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UWorld* World = GetWorld())
    {
        if (UPerformanceLabCrowdAISubsystem* Subsystem = World->GetSubsystem<UPerformanceLabCrowdAISubsystem>())
        {
            Subsystem->UnregisterAgent(this);
        }
    }
    Super::EndPlay(EndPlayReason);
}

void APerformanceLabCrowdAIController::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    DecisionAccumulator += DeltaSeconds;
    if (DecisionAccumulator >= DecisionIntervalSeconds)
    {
        DecisionAccumulator = FMath::Fmod(DecisionAccumulator, DecisionIntervalSeconds);
        RunDecision();
    }
}

void APerformanceLabCrowdAIController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);
    CrowdState = EPerformanceLabCrowdAIState::Acquire;
    TargetActor.Reset();
}

void APerformanceLabCrowdAIController::OnUnPossess()
{
    TargetActor.Reset();
    CrowdState = EPerformanceLabCrowdAIState::Acquire;
    bNavigationRouteInitialized = false;
    bNavigationRouteValid = false;
    bNavigationStuck = false;
    NavigationRouteIndex = 1;
    NavigationRoute.Reset();
    Super::OnUnPossess();
}

void APerformanceLabCrowdAIController::RunDecision()
{
    if (bNavigationLoad)
    {
        RunNavigationDecision();
        return;
    }
    APawn* ControlledPawn = GetPawn();
    UPerformanceLabCrowdAISubsystem* Subsystem = GetWorld()->GetSubsystem<UPerformanceLabCrowdAISubsystem>();
    if (!ControlledPawn || !Subsystem)
    {
        return;
    }

    AActor* NewTarget = Subsystem->FindTarget(this);
    SetCrowdTarget(NewTarget);
    if (!NewTarget)
    {
        CrowdState = EPerformanceLabCrowdAIState::Acquire;
        ClearFocus(EAIFocusPriority::Gameplay);
        StopMovement();
        return;
    }

    SetFocus(NewTarget, EAIFocusPriority::Gameplay);
    const double DistanceSquared = FVector::DistSquared2D(ControlledPawn->GetActorLocation(), NewTarget->GetActorLocation());
    if (DistanceSquared <= FMath::Square(100.0))
    {
        CrowdState = EPerformanceLabCrowdAIState::Engage;
        StopMovement();
    }
    else
    {
        const bool bWasChasingSameTarget = CrowdState == EPerformanceLabCrowdAIState::Chase && TargetActor.Get() == NewTarget;
        CrowdState = EPerformanceLabCrowdAIState::Chase;
        if (!bWasChasingSameTarget || GetMoveStatus() == EPathFollowingStatus::Idle)
        {
            MoveToActor(NewTarget, 75.0f, true, true, true, nullptr, true);
        }
    }
}

void APerformanceLabCrowdAIController::InitializeNavigationRoute()
{
    bNavigationRouteInitialized = true;
    APawn* ControlledPawn = GetPawn();
    UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if (!ControlledPawn || !NavigationSystem || NavigationAgentIndex == INDEX_NONE)
    {
        return;
    }

    const FVector Start = ControlledPawn->GetActorLocation();
    const FVector ProjectionExtent(500.0, 500.0, 500.0);
    const double RouteAngle = static_cast<double>(FMath::Max(0, NavigationAgentIndex)) * 2.399963229728653;
    const FVector RouteForward(FMath::Cos(RouteAngle), FMath::Sin(RouteAngle), 0.0);
    const FVector RouteRight(-RouteForward.Y, RouteForward.X, 0.0);
    const FVector DesiredRoute[] = {
        Start,
        Start + RouteForward * 600.0,
        Start + RouteForward * 600.0 + RouteRight * 400.0,
        Start + RouteRight * 400.0
    };
    NavigationRoute.Reset(UE_ARRAY_COUNT(DesiredRoute));
    bNavigationRouteValid = true;
    for (const FVector& DesiredPoint : DesiredRoute)
    {
        FNavLocation ProjectedPoint;
        if (!NavigationSystem->ProjectPointToNavigation(DesiredPoint, ProjectedPoint, ProjectionExtent)
            || FVector::DistSquared2D(DesiredPoint, ProjectedPoint.Location) > FMath::Square(200.0))
        {
            bNavigationRouteValid = false;
            NavigationRoute.Reset();
            break;
        }
        NavigationRoute.Add(ProjectedPoint.Location);
    }
    if (!bNavigationRouteValid)
    {
        return;
    }

    NavigationRouteIndex = 1;
    StuckWindowStartLocation = Start;
    StuckWindowStartTime = GetWorld()->GetTimeSeconds();
}

void APerformanceLabCrowdAIController::RunNavigationDecision()
{
    const double WorkStartSeconds = FPlatformTime::Seconds();
    ON_SCOPE_EXIT
    {
        NavigationDecisionWorkTimeTotalMs += (FPlatformTime::Seconds() - WorkStartSeconds) * 1000.0;
    };

    if (!bNavigationRouteInitialized)
    {
        InitializeNavigationRoute();
    }
    APawn* ControlledPawn = GetPawn();
    UPerformanceLabCrowdAISubsystem* Subsystem = GetWorld()->GetSubsystem<UPerformanceLabCrowdAISubsystem>();
    if (!ControlledPawn || !Subsystem || !bNavigationRouteValid)
    {
        return;
    }

    FVector Endpoint = GetCurrentNavigationGoal();
    bool bForceRequest = LastSubmittedNavigationGoal.X >= UE_BIG_NUMBER * 0.5;
    if (FVector::DistSquared2D(ControlledPawn->GetActorLocation(), Endpoint) <= FMath::Square(150.0))
    {
        ++NavigationArrivalCount;
        NavigationRouteIndex = (NavigationRouteIndex + 1) % NavigationRoute.Num();
        Endpoint = GetCurrentNavigationGoal();
        bForceRequest = true;
        bNavigationStuck = false;
        StuckWindowStartLocation = ControlledPawn->GetActorLocation();
        StuckWindowStartTime = GetWorld()->GetTimeSeconds();
    }

    const double Now = GetWorld()->GetTimeSeconds();
    const double AgentIndex = static_cast<double>(FMath::Max(0, NavigationAgentIndex));
    const double Phase = AgentIndex * 0.173;
    const double RouteAngle = AgentIndex * 2.399963229728653;
    const FVector RouteRight(-FMath::Sin(RouteAngle), FMath::Cos(RouteAngle), 0.0);
    const FVector DesiredGoal = Endpoint + RouteRight * (FMath::Sin(Now * 0.8 + Phase) * 60.0);
    if (!bNavigationBudget)
    {
        ExecuteNavigationMove(DesiredGoal);
    }
    else
    {
        const bool bCooldownElapsed = Now - LastNavigationRequestTime >= 0.8;
        const bool bGoalChangedEnough = FVector::DistSquared2D(DesiredGoal, LastSubmittedNavigationGoal) >= FMath::Square(40.0);
        const bool bNeedsRecovery = GetMoveStatus() == EPathFollowingStatus::Idle && bCooldownElapsed;
        const bool bNeedsStuckRecovery = bNavigationStuck && bCooldownElapsed;
        if (bForceRequest || (bCooldownElapsed && bGoalChangedEnough) || bNeedsRecovery || bNeedsStuckRecovery)
        {
            Subsystem->QueueNavigationMove(this, DesiredGoal);
        }
    }

    if (Now - StuckWindowStartTime >= 4.0)
    {
        const double ProgressSquared = FVector::DistSquared2D(ControlledPawn->GetActorLocation(), StuckWindowStartLocation);
        bNavigationStuck = GetNavigationDistanceToGoal() > 300.0 && ProgressSquared < FMath::Square(50.0);
        StuckWindowStartLocation = ControlledPawn->GetActorLocation();
        StuckWindowStartTime = Now;
    }
}

void APerformanceLabCrowdAIController::ExecuteNavigationMove(const FVector& Goal)
{
    if (!GetPawn())
    {
        return;
    }
    if (LastSubmittedNavigationGoal.X < UE_BIG_NUMBER * 0.5)
    {
        ++NavigationReplanCount;
    }
    ++NavigationRequestCount;
    const EPathFollowingRequestResult::Type Result = MoveToLocation(Goal, 75.0f, true, true, false, true, nullptr, true);
    if (Result == EPathFollowingRequestResult::Failed)
    {
        ++NavigationFailureCount;
    }
    LastSubmittedNavigationGoal = Goal;
    LastNavigationRequestTime = GetWorld()->GetTimeSeconds();
}

double APerformanceLabCrowdAIController::GetNavigationDistanceToGoal() const
{
    const APawn* ControlledPawn = GetPawn();
    if (!ControlledPawn || !bNavigationRouteValid)
    {
        return 0.0;
    }
    return FVector::Dist2D(ControlledPawn->GetActorLocation(), GetCurrentNavigationGoal());
}

FVector APerformanceLabCrowdAIController::GetCurrentNavigationGoal() const
{
    return NavigationRoute.IsValidIndex(NavigationRouteIndex)
        ? NavigationRoute[NavigationRouteIndex]
        : FVector::ZeroVector;
}

void APerformanceLabCrowdAIController::SetCrowdTarget(AActor* NewTarget)
{
    TargetActor = NewTarget;
}
