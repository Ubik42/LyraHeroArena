#include "PerformanceLabModule.h"

#include "Modules/ModuleManager.h"

#if !UE_BUILD_SHIPPING
#include "AIController.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Character/LyraCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace PerformanceLabPresentation
{
    void SetView(const TArray<FString>& Args, UWorld* World)
    {
        APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
        if (!PC || Args.Num() != 7) return;
        FVector Location(FCString::Atod(*Args[0]), FCString::Atod(*Args[1]), FCString::Atod(*Args[2]));
        FRotator Rotation(FCString::Atod(*Args[3]), FCString::Atod(*Args[4]), FCString::Atod(*Args[5]));
        ACameraActor* Camera = Cast<ACameraActor>(PC->GetViewTarget());
        if (!Camera)
        {
            FActorSpawnParameters Spawn;
            Spawn.ObjectFlags |= RF_Transient;
            Camera = World->SpawnActor<ACameraActor>(Location, Rotation, Spawn);
        }
        if (!Camera) return;
        Camera->SetActorLocationAndRotation(Location, Rotation);
        Camera->GetCameraComponent()->SetFieldOfView(FMath::Clamp(FCString::Atof(*Args[6]), 10.f, 140.f));
        PC->SetViewTarget(Camera);
    }

    void LogAnimation(const TArray<FString>& Args, UWorld* World)
    {
        APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
        if (!PC) return;
        FVector Location; FRotator Rotation;
        PC->GetPlayerViewPoint(Location, Rotation);
        const auto Root = MakeShared<FJsonObject>();
        Root->SetNumberField(TEXT("time"), World->GetTimeSeconds());
        Root->SetStringField(TEXT("camera"), Location.ToString());
        Root->SetStringField(TEXT("rotation"), Rotation.ToString());
        TArray<TSharedPtr<FJsonValue>> Characters;
        for (TActorIterator<ALyraCharacter> It(World); It; ++It)
        {
            const USkeletalMeshComponent* Mesh = It->GetMesh();
            if (!Mesh) continue;
            const auto Row = MakeShared<FJsonObject>();
            Row->SetStringField(TEXT("name"), It->GetName());
            Row->SetStringField(TEXT("position"), It->GetActorLocation().ToString());
            Row->SetNumberField(TEXT("distance"), FVector::Distance(Location, It->GetActorLocation()));
            Row->SetNumberField(TEXT("speed"), It->GetVelocity().Size2D());
            Row->SetBoolField(TEXT("ai"), Cast<AAIController>(It->GetController()) != nullptr);
            Row->SetBoolField(TEXT("protected"), It->IsAnimationFullRateProtected());
            Row->SetBoolField(TEXT("uro"), Mesh->bEnableUpdateRateOptimizations);
            Row->SetBoolField(TEXT("postProcessDisabled"), Mesh->GetDisablePostProcessBlueprint());
            Row->SetBoolField(TEXT("recentlyRendered"), Mesh->WasRecentlyRendered(.2f));
            Row->SetNumberField(TEXT("lod"), Mesh->GetPredictedLODLevel());
            Row->SetNumberField(TEXT("availableLODs"), Mesh->GetNumLODs());
            if (Mesh->AnimUpdateRateParams)
            {
                Row->SetNumberField(TEXT("updateRate"), Mesh->AnimUpdateRateParams->UpdateRate);
                Row->SetNumberField(TEXT("evaluationRate"), Mesh->AnimUpdateRateParams->EvaluationRate);
            }
            Characters.Add(MakeShared<FJsonValueObject>(Row));
        }
        Root->SetArrayField(TEXT("characters"), Characters);
        FString Json;
        const auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
        FJsonSerializer::Serialize(Root, Writer);
        UE_LOG(LogTemp, Display, TEXT("PerformanceLab animation snapshot: %s"), *Json);
    }

    FAutoConsoleCommandWithWorldAndArgs ViewCommand(TEXT("PerformanceLab.Debug.SetView"),
        TEXT("Development presentation camera: x y z pitch yaw roll fov. Does not move pawns."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetView));
    FAutoConsoleCommandWithWorldAndArgs SnapshotCommand(TEXT("PerformanceLab.Debug.LogAnimation"),
        TEXT("Development read-only animation policy snapshot. Do not run during timing samples."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&LogAnimation));
}
#endif

IMPLEMENT_MODULE(FPerformanceLabModule, PerformanceLab)
