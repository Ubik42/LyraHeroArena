#include "PerformanceLabBotCreationComponent.h"

#include "PerformanceLabCrowdAIController.h"

#include "AIController.h"
#include "Character/LyraPawnExtensionComponent.h"
#include "GameFramework/PlayerState.h"
#include "GameModes/LyraGameMode.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

void UPerformanceLabBotCreationComponent::SpawnOneBot()
{
#if WITH_SERVER_CODE
    TSubclassOf<AAIController> ControllerClass = BotControllerClass;
    if (FParse::Param(FCommandLine::Get(), TEXT("PerfLabCrowdStateMachine"))
        || FParse::Param(FCommandLine::Get(), TEXT("PerfLabNavigationLoad")))
    {
        ControllerClass = APerformanceLabCrowdAIController::StaticClass();
    }
    else if (FParse::Param(FCommandLine::Get(), TEXT("PerfLabActiveAI")))
    {
        ControllerClass = LoadClass<AAIController>(nullptr,
            TEXT("/ShooterCore/Bot/B_AI_Controller_LyraShooter.B_AI_Controller_LyraShooter_C"));
    }
    if (!ControllerClass)
    {
        UE_LOG(LogTemp, Error, TEXT("[PerformanceLab] BotControllerClass is not configured."));
        return;
    }

    FActorSpawnParameters ControllerSpawnInfo;
    ControllerSpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ControllerSpawnInfo.OverrideLevel = GetComponentLevel();
    ControllerSpawnInfo.ObjectFlags |= RF_Transient;

    AAIController* NewController = GetWorld()->SpawnActor<AAIController>(
        ControllerClass,
        FVector::ZeroVector,
        FRotator::ZeroRotator,
        ControllerSpawnInfo);
    if (!NewController)
    {
        return;
    }

    ALyraGameMode* GameMode = GetGameMode<ALyraGameMode>();
    check(GameMode);

    if (NewController->PlayerState)
    {
        NewController->PlayerState->SetPlayerName(CreateBotName(NewController->PlayerState->GetPlayerId()));
    }

    GameMode->GenericPlayerInitialization(NewController);

    const int32 SpawnIndex = SpawnedBotList.Num();
    const int32 SafeColumns = FMath::Max(1, GridColumns);
    const int32 Column = SpawnIndex % SafeColumns;
    const int32 Row = SpawnIndex / SafeColumns;
    const FVector SpawnLocation = GridOrigin + FVector(Column * GridSpacingX, Row * GridSpacingY, 0.0);
    GameMode->RestartPlayerAtTransform(NewController, FTransform(FRotator::ZeroRotator, SpawnLocation));

    if (APawn* Pawn = NewController->GetPawn())
    {
        if (ULyraPawnExtensionComponent* PawnExtension = Pawn->FindComponentByClass<ULyraPawnExtensionComponent>())
        {
            PawnExtension->CheckDefaultInitialization();
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[PerformanceLab] Grid spawn failed at index %d, location %s."), SpawnIndex, *SpawnLocation.ToCompactString());
    }

    SpawnedBotList.Add(NewController);
#else
    ensureMsgf(false, TEXT("Performance Lab bots require server code."));
#endif
}
