// Opt-in, non-Shipping local test driver for separate server/client processes.
// Commands are files under Saved/PortfolioNetwork/<run-role>, not a network API.
#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameStateBase.h"
#include "UObject/UnrealType.h"
#include "UObject/StructOnScope.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "InputAction.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "UnrealClient.h"
#include "Player/LyraPlayerState.h"
#include "AbilitySystem/LyraAbilitySystemComponent.h"
#include "UI/HeroArenaHeroSelectWidget.h"
#include "UObject/UObjectIterator.h"
#include "Character/LyraHealthComponent.h"

#if !UE_BUILD_SHIPPING
namespace HeroArenaNetworkProbe
{
    FTSTicker::FDelegateHandle Handle;
    FString Folder;
    int32 Next = 1;
    bool bReady = false;

    bool Tick(float)
    {
        if (!GEngine) return true;
        UWorld* World = nullptr;
        for (const FWorldContext& Context : GEngine->GetWorldContexts())
        {
            if (Context.WorldType == EWorldType::Game && Context.World()) { World = Context.World(); break; }
        }
        if (!World) return true;
        APlayerController* PC = World->GetFirstPlayerController();
        ALyraPlayerState* PS = PC ? PC->GetPlayerState<ALyraPlayerState>() : nullptr;
        ULyraAbilitySystemComponent* ASC = PS ? PS->GetLyraAbilitySystemComponent() : nullptr;
        if (!bReady)
        {
            if (!PC || !PC->GetPawn() || !ASC || ASC->GetActivatableAbilities().Num() == 0) return true;
            bReady = true;
            if (PC->IsLocalController() && GEngine->GameViewport)
            {
                GEngine->GameViewport->SetIgnoreInput(true);
                PC->FlushPressedKeys();
            }
            FFileHelper::SaveStringToFile(FString::Printf(TEXT("net=%d player=%d"), int32(World->GetNetMode()), PS->GetPlayerId()),
                *FPaths::Combine(Folder, TEXT("ready.txt")));
        }
        const FString Stem = FString::Printf(TEXT("%04d"), Next);
        FString Command;
        if (!FFileHelper::LoadFileToString(Command, *FPaths::Combine(Folder, Stem + TEXT(".txt")))) return true;
        Command.TrimStartAndEndInline();
        bool bAccepted = false;
        FString Operation = Command, Args;
        Command.Split(TEXT(" "), &Operation, &Args);
        if (Operation == TEXT("Console"))
        {
            FString Name, Unused;
            Args.Split(TEXT(" "), &Name, &Unused);
            if (Name.IsEmpty()) Name = Args;
            const TArray<FString> Allowed = { TEXT("HeroArena.Debug.RequestHero"), TEXT("HeroArena.Debug.SetSelectionLocked"),
                TEXT("HeroArena.Debug.SetNetworkSimulation"), TEXT("HeroArena.Debug.SetBlinkBlocked"),
                TEXT("HeroArena.Debug.LogBlinkNetwork"), TEXT("HeroArena.Debug.LogGAS"),
                TEXT("HeroArena.Debug.LogMatchFrame"),
                TEXT("HeroArena.Debug.DamagePlayer"), TEXT("HeroArena.Debug.LogPlayerState") };
            if (Allowed.Contains(Name) && !Args.Contains(TEXT("\n")) && !Args.Contains(TEXT("|")))
                bAccepted = GEngine->Exec(World, *Args);
        }
        else if (Operation == TEXT("Blink") && PC && PC->IsLocalController())
        {
            const UInputAction* Action = LoadObject<UInputAction>(nullptr, TEXT("/HeroArena/Input/IA_Phase_Blink.IA_Phase_Blink"));
            UEnhancedInputLocalPlayerSubsystem* Input = PC->GetLocalPlayer() ? PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
            if (Input && Action && Input->GetPlayerInput())
            {
                Input->GetPlayerInput()->InjectInputForAction(Action, FInputActionValue(true));
                bAccepted = true;
            }
        }
        else if (Operation == TEXT("FinishRound") && World->GetNetMode() == NM_DedicatedServer)
        {
            // Staged terminal-state test. Invoke the host's real victory flow without editing assets.
            const int32 Team = FCString::Atoi(*Args);
            if ((Team == 1 || Team == 2) && World->GetGameState())
            {
                for (UActorComponent* Component : World->GetGameState()->GetComponents())
                {
                    if (!Component || !Component->GetClass()->GetName().Contains(TEXT("ControlPointScoring"))) continue;
                    UFunction* Victory = Component->FindFunction(TEXT("HandleVictory"));
                    FIntProperty* TeamParam = nullptr;
                    int32 Inputs = 0;
                    for (TFieldIterator<FProperty> It(Victory); Victory && It; ++It)
                    {
                        if (It->HasAnyPropertyFlags(CPF_Parm) && !It->HasAnyPropertyFlags(CPF_ReturnParm))
                        {
                            ++Inputs;
                            TeamParam = CastField<FIntProperty>(*It);
                        }
                    }
                    if (Victory && Inputs == 1 && TeamParam)
                    {
                        FStructOnScope Parameters(Victory);
                        TeamParam->SetPropertyValue_InContainer(Parameters.GetStructMemory(), Team);
                        Component->ProcessEvent(Victory, Parameters.GetStructMemory());
                        bAccepted = true;
                    }
                    break;
                }
            }
        }
        else if (Operation == TEXT("ClockBias") && World->GetNetMode() == NM_Client)
        {
            // One-shot fault injection only; normal GameState synchronization continues afterward.
            // The command exists solely behind the explicit non-Shipping process-probe opt-in.
            const float Bias = FCString::Atof(*Args);
            AGameStateBase* State = World->GetGameState();
            FFloatProperty* Offset = State ? FindFProperty<FFloatProperty>(State->GetClass(), TEXT("ServerWorldTimeSecondsDelta")) : nullptr;
            if (Offset && FMath::IsFinite(Bias) && FMath::Abs(Bias) <= 10.0f)
            {
                Offset->SetPropertyValue_InContainer(State, Offset->GetPropertyValue_InContainer(State) + Bias);
                UE_LOG(LogTemp, Display, TEXT("Blink clock fault injection: bias_seconds=%.3f"), Bias);
                bAccepted = true;
            }
        }
        else if (Operation == TEXT("Place") && PC && PC->GetPawn())
        {
            // Explicit setup only. Tests never use this to fake a replicated skill result.
            TArray<FString> Values; Args.ParseIntoArrayWS(Values);
            if (Values.Num() == 4)
            {
                const FVector Position(FCString::Atof(*Values[0]), FCString::Atof(*Values[1]), FCString::Atof(*Values[2]));
                const FRotator Rotation(0, FCString::Atof(*Values[3]), 0);
                if (World->GetNetMode() != NM_Client) PC->GetPawn()->TeleportTo(Position, Rotation);
                PC->SetControlRotation(Rotation);
                bAccepted = true;
            }
        }
        else if (Operation == TEXT("Shot") && World->GetNetMode() == NM_Client)
        {
            FScreenshotRequest::RequestScreenshot(FPaths::Combine(Folder, Stem + TEXT(".png")), true, false);
            bAccepted = true;
        }
        else if (Operation == TEXT("Continue") && PC && PC->IsLocalController())
        {
            for (TObjectIterator<UHeroArenaHeroSelectWidget> It; It; ++It)
                if (It->GetWorld() == World && It->IsActivated()) It->DeactivateWidget();
            bAccepted = true;
        }
        else if (Operation == TEXT("Eliminate") && PC && PC->GetPawn() && World->GetNetMode() == NM_DedicatedServer)
        {
            if (auto* Health = PC->GetPawn()->FindComponentByClass<ULyraHealthComponent>())
            {
                Health->DamageSelfDestruct();
                bAccepted = true;
            }
        }
        else if (Operation == TEXT("Quit"))
        {
            bAccepted = true;
            FPlatformMisc::RequestExit(false);
        }
        UE_LOG(LogTemp, Display, TEXT("HeroArena process probe: id=%d net=%d world_time=%.3f accepted=%d command=%s"),
            Next, int32(World->GetNetMode()), World->GetTimeSeconds(), bAccepted, *Command);
        const FString AckPath = FPaths::Combine(Folder, Stem + TEXT(".ack"));
        const FString TempPath = AckPath + TEXT(".tmp");
        if (FFileHelper::SaveStringToFile(bAccepted ? TEXT("ok") : TEXT("rejected"), *TempPath))
            IFileManager::Get().Move(*AckPath, *TempPath);
        ++Next;
        return true;
    }
}
#endif

void StartHeroArenaNetworkProbe()
{
#if !UE_BUILD_SHIPPING
    FString Run;
    if (!FParse::Value(FCommandLine::Get(), TEXT("HeroArenaNetworkProbe="), Run) || Run.IsEmpty()) return;
    for (TCHAR Char : Run) if (!FChar::IsAlnum(Char) && Char != TCHAR('-') && Char != TCHAR('_')) return;
    HeroArenaNetworkProbe::Folder = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PortfolioNetwork"), Run);
    IFileManager::Get().MakeDirectory(*HeroArenaNetworkProbe::Folder, true);
    HeroArenaNetworkProbe::Next = 1;
    HeroArenaNetworkProbe::bReady = false;
    HeroArenaNetworkProbe::Handle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&HeroArenaNetworkProbe::Tick), .05f);
#endif
}

void StopHeroArenaNetworkProbe()
{
#if !UE_BUILD_SHIPPING
    FTSTicker::GetCoreTicker().RemoveTicker(HeroArenaNetworkProbe::Handle);
#endif
}
