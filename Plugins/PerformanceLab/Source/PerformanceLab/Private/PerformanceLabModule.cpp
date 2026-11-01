#include "PerformanceLabModule.h"

#include "Modules/ModuleManager.h"

#if !UE_BUILD_SHIPPING
#include "AIController.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Character/LyraCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Containers/Ticker.h"
#include "Debug/DebugDrawService.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "CanvasItem.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
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

    // Diagnostic observer: never enabled in performance timing runs.
    // All values below are read from the selected live skeletal component.
    TWeakObjectPtr<ALyraCharacter> Observed;
    TWeakObjectPtr<AActor> PreviousView;
    TWeakObjectPtr<ACameraActor> ObserverCamera;
    FTSTicker::FDelegateHandle ObserverTicker;
    FDelegateHandle ObserverDraw;
    float ObserverDistance = 600.f;
    bool bRampRoute = false;
    bool bSavedControllerTick = true;
    float SavedWalkSpeed = 0.f;
    FTransform SavedActorTransform;
    int32 RouteGoal = 1;
    TArray<TWeakObjectPtr<AStaticMeshActor>> RouteGeometry;

    void StopRampRoute()
    {
        if (bRampRoute)
        {
            if (ALyraCharacter* Actor = Observed.Get())
            {
                Actor->GetCharacterMovement()->StopMovementImmediately();
                Actor->GetCharacterMovement()->MaxWalkSpeed = SavedWalkSpeed;
                Actor->SetActorTransform(SavedActorTransform, false, nullptr, ETeleportType::TeleportPhysics);
                if (AController* Controller = Actor->GetController()) Controller->SetActorTickEnabled(bSavedControllerTick);
            }
        }
        bRampRoute = false;
        for (auto& Item : RouteGeometry) if (Item.IsValid()) Item->Destroy();
        RouteGeometry.Reset();
    }

    void StartRampRoute()
    {
        ALyraCharacter* Actor = Observed.Get();
        if (!Actor || bRampRoute) return;
        UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
        if (!Cube) return;
        // Temporary collision fixture, isolated above the stock map. No assets are saved.
        auto AddBlock = [&](FVector Position, FVector Scale, float Pitch)
        {
            FActorSpawnParameters Spawn; Spawn.ObjectFlags |= RF_Transient;
            AStaticMeshActor* Block = Actor->GetWorld()->SpawnActor<AStaticMeshActor>(Position,FRotator(Pitch,0,0),Spawn);
            if (!Block) return false;
            Block->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
            Block->GetStaticMeshComponent()->SetStaticMesh(Cube);
            Block->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
            Block->SetActorScale3D(Scale);
            RouteGeometry.Add(Block);
            return true;
        };
        if (!AddBlock(FVector(6000,6000,1980),FVector(40,8,.4),0)
            || !AddBlock(FVector(6000,6000,2300),FVector(18,6,.4),20))
        {
            StopRampRoute(); return;
        }
        // Keep the native diagnostic panel out of the continuous film.
        if (ObserverDraw.IsValid()) { UDebugDrawService::Unregister(ObserverDraw); ObserverDraw.Reset(); }
        SavedActorTransform = Actor->GetActorTransform();
        SavedWalkSpeed = Actor->GetCharacterMovement()->MaxWalkSpeed;
        if (AAIController* Controller = Cast<AAIController>(Actor->GetController()))
        {
            bSavedControllerTick = Controller->IsActorTickEnabled();
            Controller->StopMovement();
            Controller->ClearFocus(EAIFocusPriority::Gameplay);
            Controller->SetActorTickEnabled(false);
        }
        Actor->GetCharacterMovement()->StopMovementImmediately();
        Actor->GetCharacterMovement()->MaxWalkSpeed = 180.f;
        Actor->SetActorLocationAndRotation(FVector(5000,6000,2100),FRotator::ZeroRotator,false,nullptr,ETeleportType::TeleportPhysics);
        RouteGoal = 1;
        bRampRoute = true;
        UE_LOG(LogTemp, Display, TEXT("Observer ramp route: 20-degree collision ramp; CharacterMovement input; no pose manipulation"));
    }

    void StopObserver()
    {
        StopRampRoute();
        if (ObserverTicker.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(ObserverTicker);
        if (ObserverDraw.IsValid()) UDebugDrawService::Unregister(ObserverDraw);
        ObserverTicker.Reset(); ObserverDraw.Reset();
        if (ACameraActor* Camera = ObserverCamera.Get())
        {
            APlayerController* PC = Camera->GetWorld()->GetFirstPlayerController();
            if (PC && PC->GetViewTarget() == Camera && PreviousView.IsValid()) PC->SetViewTarget(PreviousView.Get());
            Camera->Destroy();
        }
        Observed.Reset(); PreviousView.Reset(); ObserverCamera.Reset();
    }

    void DrawObserver(UCanvas* Canvas, APlayerController*)
    {
        const ALyraCharacter* Target = Observed.Get();
        if (!Canvas || !Target || !ObserverCamera.IsValid()) return;
        const USkeletalMeshComponent* Mesh = Target->GetMesh();
        if (!Mesh) return;
        const float Distance = FVector::Distance(Target->GetActorLocation(), ObserverCamera->GetActorLocation());
        const FAnimUpdateRateParameters* Rates = Mesh->AnimUpdateRateParams;
        TArray<FString> Lines {
            TEXT("LIVE ANIMATION POLICY / DIAGNOSTIC VIEW"), Target->GetName(),
            FString::Printf(TEXT("Distance %.1f m   Speed %.1f cm/s"), Distance / 100.f, Target->GetVelocity().Size2D()),
            FString::Printf(TEXT("Full rate protected: %s   URO enabled: %s"), Target->IsAnimationFullRateProtected() ? TEXT("YES") : TEXT("NO"), Mesh->bEnableUpdateRateOptimizations ? TEXT("YES") : TEXT("NO")),
            Mesh->bEnableUpdateRateOptimizations ? FString::Printf(TEXT("URO update / evaluation interval: %d / %d frames"), Rates ? Rates->UpdateRate : 0, Rates ? Rates->EvaluationRate : 0) : FString(TEXT("URO bypassed: full-rate evaluation (cached intervals ignored)")),
            FString::Printf(TEXT("Post-process animation: %s   Mesh LOD: %d / %d"), Mesh->GetDisablePostProcessBlueprint() ? TEXT("OFF") : TEXT("ON"), Mesh->GetPredictedLODLevel(), Mesh->GetNumLODs()),
            TEXT("Camera follows one actor. This view does not measure frame time.")
        };
        FCanvasTileItem Panel(FVector2D(18,18), FVector2D(670,180), FLinearColor(0.015f,0.025f,0.04f,.9f));
        Panel.BlendMode = SE_BLEND_Translucent;
        Canvas->DrawItem(Panel);
        for (int32 Index=0; Index<Lines.Num(); ++Index)
        {
            FCanvasTextItem Text(FVector2D(30,28+Index*23), FText::FromString(Lines[Index]), GEngine->GetSmallFont(), Index==0 ? FLinearColor(.3f,1.f,.85f) : FLinearColor::White);
            Text.EnableShadow(FLinearColor::Black);
            Canvas->DrawItem(Text);
        }
    }

    void Observe(const TArray<FString>& Args, UWorld* World)
    {
        if (!World || Args.Num()<1) return;
        if (Args[0] == TEXT("off")) { StopObserver(); return; }
        if (Args[0] == TEXT("ramp")) { StartRampRoute(); return; }
        APlayerController* PC = World->GetFirstPlayerController();
        if (!PC) return;
        ALyraCharacter* Target = nullptr;
        double BestDistance = TNumericLimits<double>::Max();
        for (TActorIterator<ALyraCharacter> It(World); It; ++It)
        {
            if (!Cast<AAIController>(It->GetController())) continue;
            const double Distance = FVector::DistSquared(It->GetActorLocation(), PC->PlayerCameraManager->GetCameraLocation());
            if (It->GetName() == Args[0]) { Target=*It; break; }
            if (Args[0] == TEXT("nearest") && Distance < BestDistance) { Target=*It; BestDistance=Distance; }
        }
        if (!Target) { UE_LOG(LogTemp, Warning, TEXT("Observer: character not found")); return; }
        if (Observed.Get() != Target || !ObserverCamera.IsValid())
        {
            StopObserver();
            Observed=Target; PreviousView=PC->GetViewTarget();
            FActorSpawnParameters Spawn; Spawn.ObjectFlags |= RF_Transient;
            ObserverCamera=World->SpawnActor<ACameraActor>(Spawn);
            if (!ObserverCamera.IsValid()) { StopObserver(); return; }
            PC->SetViewTarget(ObserverCamera.Get());
            ObserverDraw=UDebugDrawService::Register(TEXT("Game"),FDebugDrawDelegate::CreateStatic(&DrawObserver));
            ObserverTicker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
            {
                ALyraCharacter* Actor=Observed.Get(); ACameraActor* Camera=ObserverCamera.Get();
                if (!Actor || !Camera) return true; // Safe across PIE teardown; next command/shutdown removes delegates.
                if (bRampRoute && !Actor->GetWorld()->IsPaused())
                {
                    const FVector Goal(RouteGoal ? 6800 : 5000,6000,0);
                    const FVector Direction=(Goal-Actor->GetActorLocation()).GetSafeNormal2D();
                    if (FVector::DistSquared2D(Goal,Actor->GetActorLocation()) < FMath::Square(50.f)) RouteGoal=1-RouteGoal;
                    if (AController* Controller=Actor->GetController()) Controller->SetControlRotation(Direction.Rotation());
                    Actor->AddMovementInput(Direction,1.f,true);
                }
                const FVector Aim=Actor->GetActorLocation()+FVector(0,0,10);
                const FVector Position=Aim+(bRampRoute ? FVector(0,ObserverDistance,80) : FVector(ObserverDistance,0,90));
                Camera->SetActorLocationAndRotation(Position,(Aim-Position).Rotation());
                // Telephoto keeps the actor readable while crossing actual significance distances.
                Camera->GetCameraComponent()->SetFieldOfView(FMath::RadiansToDegrees(2.f*FMath::Atan(300.f/ObserverDistance)));
                return true;
            }));
        }
        ObserverDistance=Args.Num()>1 ? FMath::Clamp(FCString::Atof(*Args[1]),300.f,4000.f):600.f;
        UE_LOG(LogTemp, Display, TEXT("Observer selected %s at %.0f cm; diagnostics only"),*Target->GetName(),ObserverDistance);
    }

    void LogAnimation(const TArray<FString>& Args, UWorld* World)
    {
        APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
        if (!PC) return;
        FVector Location; FRotator Rotation;
        PC->GetPlayerViewPoint(Location, Rotation);
        const auto Root = MakeShared<FJsonObject>();
        Root->SetNumberField(TEXT("time"), World->GetTimeSeconds());
        Root->SetBoolField(TEXT("rampRoute"), bRampRoute);
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
            Row->SetBoolField(TEXT("onGround"),It->GetCharacterMovement()->IsMovingOnGround());
            Row->SetStringField(TEXT("leftFoot"),Mesh->GetSocketLocation(TEXT("foot_l")).ToString());
            Row->SetStringField(TEXT("rightFoot"),Mesh->GetSocketLocation(TEXT("foot_r")).ToString());
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

    FAutoConsoleCommandWithWorldAndArgs ObserverCommand(TEXT("PerformanceLab.Debug.Observe"),
        TEXT("Observe one live AI: actor name|nearest [distance cm], or off. Exclude from timing runs."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Observe));
    FAutoConsoleCommandWithWorldAndArgs ViewCommand(TEXT("PerformanceLab.Debug.SetView"),
        TEXT("Development presentation camera: x y z pitch yaw roll fov. Does not move pawns."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetView));
    FAutoConsoleCommandWithWorldAndArgs SnapshotCommand(TEXT("PerformanceLab.Debug.LogAnimation"),
        TEXT("Development read-only animation policy snapshot. Do not run during timing samples."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&LogAnimation));
}
#endif

#if !UE_BUILD_SHIPPING
namespace ModularPoseLab { void Shutdown(); }
#endif

void FPerformanceLabModule::ShutdownModule()
{
#if !UE_BUILD_SHIPPING
    PerformanceLabPresentation::StopObserver();
    ModularPoseLab::Shutdown();
#endif
}

IMPLEMENT_MODULE(FPerformanceLabModule, PerformanceLab)
