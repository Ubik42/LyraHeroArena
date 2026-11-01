// Original modular robot fixture. Diagnostic only; excluded from performance timing.
#if !UE_BUILD_SHIPPING
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Dom/JsonObject.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace ModularPoseLab
{
    TWeakObjectPtr<AActor> Owner, PreviousView;
    TWeakObjectPtr<ACameraActor> Camera;
    TWeakObjectPtr<AStaticMeshActor> Floor;
    TWeakObjectPtr<USkeletalMeshComponent> Leader;
    TArray<TWeakObjectPtr<USkeletalMeshComponent>> Parts;
    TWeakObjectPtr<UAnimSequence> Walk;
    FString Mode;

    void Shutdown()
    {
        if (Owner.IsValid())
        {
            if (APlayerController* PC=Owner->GetWorld()->GetFirstPlayerController())
                if (PreviousView.IsValid()) PC->SetViewTarget(PreviousView.Get());
            Owner->Destroy();
        }
        if (Camera.IsValid()) Camera->Destroy();
        if (Floor.IsValid()) Floor->Destroy();
        Owner.Reset(); Leader.Reset(); Camera.Reset(); Floor.Reset(); PreviousView.Reset(); Walk.Reset(); Parts.Reset(); Mode.Reset();
    }
    USkeletalMeshComponent* AddPart(AActor* Actor, USkeletalMesh* Mesh, FName Name)
    {
        auto* C=NewObject<USkeletalMeshComponent>(Actor,Name,RF_Transient);
        Actor->AddInstanceComponent(C); C->SetupAttachment(Actor->GetRootComponent());
        C->SetSkeletalMesh(Mesh); C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        C->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
        C->bEnableUpdateRateOptimizations=false; C->RegisterComponent(); return C;
    }
    bool Create(UWorld* World)
    {
        if (!World || !World->IsGameWorld() || !World->GetFirstPlayerController()) return false;
        UAnimSequence* Animation=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/ModularPoseLab/SK_RobotFull_Anim.SK_RobotFull_Anim"));
        TArray<USkeletalMesh*> Meshes;
        for (const TCHAR* Name : {TEXT("Full"),TEXT("Lower"),TEXT("Upper"),TEXT("Head")})
        {
            const FString Path=FString::Printf(TEXT("/Game/ModularPoseLab/SK_Robot%s.SK_Robot%s"),Name,Name);
            auto* Mesh=LoadObject<USkeletalMesh>(nullptr,*Path);
            if (!Mesh || !Animation) { UE_LOG(LogTemp,Error,TEXT("ModularPoseLab missing asset %s / animation"),*Path); return false; }
            Meshes.Add(Mesh);
        }
        for (auto* Mesh:Meshes) if(Mesh->GetSkeleton()!=Meshes[0]->GetSkeleton())
        { UE_LOG(LogTemp,Error,TEXT("ModularPoseLab requires shared authored skeleton")); return false; }
        FActorSpawnParameters Spawn;Spawn.ObjectFlags|=RF_Transient;
        auto* Actor=World->SpawnActor<AActor>(FVector(6000,6000,2200),FRotator::ZeroRotator,Spawn);
        auto* Root=NewObject<USceneComponent>(Actor,TEXT("FixtureRoot"),RF_Transient);Actor->AddInstanceComponent(Root);Actor->SetRootComponent(Root);Root->RegisterComponent();Actor->SetActorLocation(FVector(6000,6000,2200));
        Owner=Actor; Walk=Animation;
        Leader=AddPart(Actor,Meshes[0],TEXT("HiddenPoseLeader"));Leader->SetHiddenInGame(true,false);
        for(int32 I=1;I<Meshes.Num();++I) Parts.Add(AddPart(Actor,Meshes[I],*FString::Printf(TEXT("RobotPart%d"),I)));
        auto* Ground=World->SpawnActor<AStaticMeshActor>(FVector(6000,6000,2190),FRotator::ZeroRotator,Spawn);
        Ground->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
        Ground->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        Ground->SetActorScale3D(FVector(12,12,.2));Floor=Ground;
        const FVector Eye(5590,6520,2490),Target(6000,6000,2290);
        auto* View=World->SpawnActor<ACameraActor>(Eye,(Target-Eye).Rotation(),Spawn);
        View->GetCameraComponent()->SetFieldOfView(30);Camera=View;
        auto* PC=World->GetFirstPlayerController();PreviousView=PC->GetViewTarget();PC->SetViewTarget(View);
        return true;
    }
    void Snapshot(UWorld* World)
    {
        if(!Owner.IsValid() || Owner->GetWorld()!=World)return;
        auto Root=MakeShared<FJsonObject>();Root->SetStringField(TEXT("mode"),Mode);Root->SetNumberField(TEXT("worldTime"),World->GetTimeSeconds());
        int32 Instances=Leader->GetAnimInstance()?1:0,Visible=0,Sections=0;
        TArray<TSharedPtr<FJsonValue>> Rows;
        for(auto& Item:Parts) if(auto* C=Item.Get())
        {
            if(C->GetAnimInstance())++Instances;
            if(!C->bHiddenInGame)++Visible;
            auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("component"),C->GetName());Row->SetStringField(TEXT("mesh"),C->GetSkeletalMeshAsset()->GetPathName());
            Row->SetBoolField(TEXT("followsLeader"),C->LeaderPoseComponent.IsValid());Row->SetBoolField(TEXT("animationInstance"),C->GetAnimInstance()!=nullptr);Row->SetBoolField(TEXT("visible"),!C->bHiddenInGame);
            int32 Count=0;if(auto* Data=C->GetSkeletalMeshAsset()->GetResourceForRendering()) if(Data->LODRenderData.Num())Count=Data->LODRenderData[0].RenderSections.Num();
            Row->SetNumberField(TEXT("lod0Sections"),Count);if(!C->bHiddenInGame)Sections+=Count;
            TArray<TSharedPtr<FJsonValue>> Bones;
            for(FName Name:{FName(TEXT("head")),FName(TEXT("forearm_l")),FName(TEXT("foot_l"))})
            {
                const FVector P=C->GetSocketLocation(Name),Q=Leader->GetSocketLocation(Name);
                auto B=MakeShared<FJsonObject>();B->SetStringField(TEXT("bone"),Name.ToString());B->SetStringField(TEXT("position"),P.ToString());
                if(Mode==TEXT("leader")) B->SetNumberField(TEXT("leaderErrorCm"),FVector::Distance(P,Q));
                Bones.Add(MakeShared<FJsonValueObject>(B));
            }
            Row->SetArrayField(TEXT("bones"),Bones);Rows.Add(MakeShared<FJsonValueObject>(Row));
        }
        Root->SetArrayField(TEXT("parts"),Rows);Root->SetNumberField(TEXT("animationInstances"),Instances);Root->SetNumberField(TEXT("visibleComponents"),Visible);Root->SetNumberField(TEXT("visibleLod0Sections"),Sections);
        Root->SetNumberField(TEXT("leaderAnimationTime"),Leader->GetSingleNodeInstance()?Leader->GetSingleNodeInstance()->GetCurrentTime():-1);
        FString Text;const auto Writer=TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);FJsonSerializer::Serialize(Root,Writer);
        UE_LOG(LogTemp,Display,TEXT("ModularPoseLab snapshot: %s"),*Text);
    }
    void Command(const TArray<FString>& Args,UWorld* World)
    {
        if(!Args.Num())return;
        const FString Action=Args[0].ToLower();
        if(Action==TEXT("off")){Shutdown();return;}
        if(Action==TEXT("snapshot")){Snapshot(World);return;}
        if(Action!=TEXT("leader") && Action!=TEXT("independent"))return;
        if(Owner.IsValid() && Owner->GetWorld()!=World)Shutdown();
        if(!Owner.IsValid() && !Create(World))return;
        float Phase=0.f;
        if(Mode==TEXT("leader") && Leader->GetSingleNodeInstance()) Phase=Leader->GetSingleNodeInstance()->GetCurrentTime();
        else if(Parts.Num() && Parts[0].IsValid() && Parts[0]->GetSingleNodeInstance()) Phase=Parts[0]->GetSingleNodeInstance()->GetCurrentTime();
        Mode=Action;
        Leader->Stop();Leader->SetAnimationMode(EAnimationMode::AnimationBlueprint);Leader->SetAnimInstanceClass(nullptr);
        Leader->SetComponentTickEnabled(Action==TEXT("leader"));
        if(Action==TEXT("leader")){Leader->PlayAnimation(Walk.Get(),true);Leader->SetPosition(Phase,false);}
        for(auto& Item:Parts)if(auto* C=Item.Get())
        {
            C->Stop();C->SetLeaderPoseComponent(nullptr,true,false);C->SetAnimationMode(EAnimationMode::AnimationBlueprint);C->SetAnimInstanceClass(nullptr);
            if(Action==TEXT("leader"))C->SetLeaderPoseComponent(Leader.Get(),true,false);
            else {C->PlayAnimation(Walk.Get(),true);C->SetPosition(Phase,false);}
        }
        Snapshot(World);
    }
    FAutoConsoleCommandWithWorldAndArgs Console(TEXT("PerformanceLab.Debug.Modular"),TEXT("Original robot: independent|leader|snapshot|off. No performance claims."),FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Command));
}
#endif
