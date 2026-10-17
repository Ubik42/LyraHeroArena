#include "Hero/HeroArenaHeroSelectionComponent.h"
#include "Hero/HeroArenaHeroDefinition.h"
#include "Actors/HeroArenaRelayBeacon.h"
#include "Components/HeroArenaBlinkChargeComponent.h"
#include "Components/HeroArenaControlPointRotationComponent.h"
#include "Effects/HeroArenaPurificationRules.h"

#include "AbilitySystem/Attributes/LyraCombatSet.h"
#include "AbilitySystem/Attributes/LyraHealthSet.h"
#include "AbilitySystem/LyraAbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"

#include "Engine/World.h"
#include "Engine/NetDriver.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"
#include "Player/LyraPlayerState.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "LyraGameplayTags.h"
#include "System/LyraAssetManager.h"
#include "System/LyraGameData.h"
#include "Teams/LyraTeamSubsystem.h"

#if !UE_BUILD_SHIPPING
namespace HeroArenaSelectionDebug
{
	// Read-only recording aid. The external driver still uses normal Enhanced Input.
	void LogGameplayFrame(const TArray<FString>& Args, UWorld* World)
	{
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		ALyraPlayerState* PS = PC ? PC->GetPlayerState<ALyraPlayerState>() : nullptr;
		if (!Pawn || !PS) return;
		const ULyraAbilitySystemComponent* ASC = PS->GetLyraAbilitySystemComponent();
		const ULyraHealthSet* Health = ASC ? ASC->GetSet<ULyraHealthSet>() : nullptr;
		FVector Camera; FRotator CameraRotation;
		PC->GetPlayerViewPoint(Camera, CameraRotation);
		const FVector Position = Pawn->GetActorLocation();
		FVector Objective = Position;
		int32 Point = -1, Owner = -1;
		if (const AGameStateBase* GS = World->GetGameState())
		{
			if (const auto* Rotation = GS->FindComponentByClass<UHeroArenaControlPointRotationComponent>())
			{
				Objective = Rotation->GetCurrentPointLocation();
				Point = Rotation->GetActivePointIndex(); Owner = Rotation->GetActiveOwnerTeam();
			}
		}
		FVector Waypoint = Position;
		bool bPathValid = false;
		if (UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, Position, Objective, Pawn))
		{
			bPathValid = Path->IsValid() && !Path->IsPartial();
			if (bPathValid)
			{
				for (const FVector& Node : Path->PathPoints)
				{
					if (FVector::DistSquared2D(Position, Node) > FMath::Square(100.f)) { Waypoint = Node; break; }
				}
			}
		}
		FVector Target = FVector::ZeroVector;
		int32 TargetId = -1;
		float BestDistance = FMath::Square(3500.f);
		for (TActorIterator<ALyraPlayerState> It(World); It; ++It)
		{
			APawn* Enemy = It->GetPawn();
			const auto* EnemyASC = It->GetLyraAbilitySystemComponent();
			const auto* EnemyHealth = EnemyASC ? EnemyASC->GetSet<ULyraHealthSet>() : nullptr;
			if (!Enemy || It->GetTeamId() == PS->GetTeamId() || !EnemyHealth || EnemyHealth->GetHealth() <= 0.f) continue;
			const float Distance = FVector::DistSquared(Position, Enemy->GetActorLocation());
			if (Distance < BestDistance && PC->LineOfSightTo(Enemy, Camera))
			{
				BestDistance = Distance; Target = Enemy->GetActorLocation() + FVector(0,0,35); TargetId = It->GetPlayerId();
			}
		}
		const FRotator Control = PC->GetControlRotation();
		UE_LOG(LogTemp, Display, TEXT("HeroArena gameplay frame: {\"time\":%.3f,\"player\":%d,\"team\":%d,\"health\":%.2f,\"position\":[%.2f,%.2f,%.2f],\"control\":[%.2f,%.2f],\"camera\":[%.2f,%.2f,%.2f],\"targetId\":%d,\"target\":[%.2f,%.2f,%.2f],\"waypoint\":[%.2f,%.2f,%.2f],\"pathValid\":%s,\"objective\":[%.2f,%.2f,%.2f],\"point\":%d,\"owner\":%d}"),
			World->GetTimeSeconds(), PS->GetPlayerId(), PS->GetTeamId(), Health ? Health->GetHealth() : 0.f,
			Position.X,Position.Y,Position.Z,Control.Pitch,Control.Yaw,Camera.X,Camera.Y,Camera.Z,
			TargetId,Target.X,Target.Y,Target.Z,Waypoint.X,Waypoint.Y,Waypoint.Z,bPathValid?TEXT("true"):TEXT("false"),
			Objective.X,Objective.Y,Objective.Z,Point,Owner);
	}
	void LogBlinkNetwork(const TArray<FString>& Args, UWorld* World)
	{
		if (!World) return;
		UE_LOG(LogTemp, Display, TEXT("HeroArena network clock: stage=%s net=%d local=%.3f estimated_server=%.3f"),
			Args.Num() ? *Args[0] : TEXT("sample"), int32(World->GetNetMode()), World->GetTimeSeconds(),
			World->GetGameState() ? World->GetGameState()->GetServerWorldTimeSeconds() : -1.0);
#if DO_ENABLE_NET_TEST
		if (const UNetDriver* Driver = World->GetNetDriver())
		{
			UE_LOG(LogTemp, Display, TEXT("HeroArena network simulation: net=%d outgoing_lag_ms=%d outgoing_loss_pct=%d"),
				static_cast<int32>(World->GetNetMode()), Driver->PacketSimulationSettings.PktLag, Driver->PacketSimulationSettings.PktLoss);
		}
#endif
		for (TActorIterator<ALyraPlayerState> It(World); It; ++It)
		{
			ALyraPlayerState* PS = *It;
			if (PS->IsABot()) continue;
			const APawn* Pawn = PS->GetPawn();
			const UHeroArenaBlinkChargeComponent* Charges = PS->FindComponentByClass<UHeroArenaBlinkChargeComponent>();
			UE_LOG(LogTemp, Display, TEXT("HeroArena Blink network: stage=%s net=%d player=%d pawn=%s local=%d location=%s charges=%d next=%.2f pending=%d"),
				Args.Num() ? *Args[0] : TEXT("sample"), static_cast<int32>(World->GetNetMode()), PS->GetPlayerId(),
				*GetNameSafe(Pawn), Pawn && Pawn->IsLocallyControlled(), Pawn ? *Pawn->GetActorLocation().ToCompactString() : TEXT("None"),
				Charges ? Charges->GetCurrentCharges() : -1, Charges ? Charges->GetNextChargeRemaining() : -1.f,
				Charges ? Charges->GetPendingPredictionCount() : -1);
		}
	}

	void SetNetworkSimulation(const TArray<FString>& Args, UWorld* World)
	{
#if DO_ENABLE_NET_TEST
		UNetDriver* Driver = World ? World->GetNetDriver() : nullptr;
		if (!Driver || Args.Num() != 2) return;
		FPacketSimulationSettings Settings;
		Settings.PktLag = FMath::Clamp(FCString::Atoi(*Args[0]), 0, 500);
		Settings.PktLoss = FMath::Clamp(FCString::Atoi(*Args[1]), 0, 10);
		Driver->SetPacketSimulationSettings(Settings);
		LogBlinkNetwork({TEXT("network-configured")}, World);
#endif
	}

	// Deliberately server-only fault injection: reproduce a stale client activation decision.
	void SetBlinkBlocked(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || World->GetNetMode() == NM_Client || Args.Num() != 2) return;
		const int32 Index = FCString::Atoi(*Args[0]);
		const bool bBlock = FCString::Atoi(*Args[1]) != 0;
		int32 Current = 0;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It, ++Current)
		{
			if (Current != Index) continue;
			ALyraPlayerState* PS = It->Get() ? It->Get()->GetPlayerState<ALyraPlayerState>() : nullptr;
			ULyraAbilitySystemComponent* ASC = PS ? PS->GetLyraAbilitySystemComponent() : nullptr;
			if (!ASC) return;
			FGameplayTagContainer Tags(FGameplayTag::RequestGameplayTag(TEXT("Ability.Type.Action.Blink")));
			if (bBlock && !ASC->AreAbilityTagsBlocked(Tags)) ASC->BlockAbilitiesWithTags(Tags);
			else if (!bBlock && ASC->AreAbilityTagsBlocked(Tags)) ASC->UnBlockAbilitiesWithTags(Tags);
			UE_LOG(LogTemp, Display, TEXT("HeroArena Blink server block: player=%d blocked=%d"), PS->GetPlayerId(), bBlock);
			return;
		}
	}

	// Read-only diagnostics. On clients, GAS specs are available for the owning player only.
	void LogGAS(const TArray<FString>& Args, UWorld* World)
	{
		if (!World) return;
		for (TActorIterator<ALyraPlayerState> It(World); It; ++It)
		{
			ALyraPlayerState* PS = *It;
			if (PS->IsABot()) continue;
			ULyraAbilitySystemComponent* ASC = PS->GetLyraAbilitySystemComponent();
			const UHeroArenaHeroSelectionComponent* Selection = PS->FindComponentByClass<UHeroArenaHeroSelectionComponent>();
			const UHeroArenaHeroDefinition* Hero = Selection ? Selection->GetSelectedHero() : nullptr;
			TArray<FString> HeroSpecs;
			if (ASC)
			{
				for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
				{
					if (const UHeroArenaHeroDefinition* Source = Cast<UHeroArenaHeroDefinition>(Spec.SourceObject.Get()))
					{
						HeroSpecs.Add(FString::Printf(TEXT("%s:%s"), *Source->HeroId.ToString(), *GetNameSafe(Spec.Ability)));
					}
				}
			}
			HeroSpecs.Sort();
			UE_LOG(LogTemp, Display, TEXT("HeroArena GAS snapshot: world=%s net=%d player=%d ps=%s pawn=%s avatar=%s hero=%s hero_specs=%d [%s]"),
				*World->GetName(), static_cast<int32>(World->GetNetMode()), PS->GetPlayerId(), *GetNameSafe(PS),
				*GetNameSafe(PS->GetPawn()), *GetNameSafe(ASC ? ASC->GetAvatarActor() : nullptr),
				Hero ? *Hero->HeroId.ToString() : TEXT("None"), HeroSpecs.Num(), *FString::Join(HeroSpecs, TEXT(",")));
		}
	}

	void RequestHero(const TArray<FString>& Args, UWorld* World)
	{
		APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
		UHeroArenaHeroSelectionComponent* Selection = PlayerController && PlayerController->PlayerState
			? PlayerController->PlayerState->FindComponentByClass<UHeroArenaHeroSelectionComponent>()
			: nullptr;
		if (Selection && Args.Num() == 1)
		{
			Selection->RequestHeroSelection(FName(*Args[0]));
		}
	}

	void SetSelectionLocked(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || World->GetNetMode() == NM_Client || Args.Num() != 1)
		{
			return;
		}
		const bool bLocked = FCString::Atoi(*Args[0]) != 0;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* PlayerController = It->Get();
			if (UHeroArenaHeroSelectionComponent* Selection = PlayerController && PlayerController->PlayerState
				? PlayerController->PlayerState->FindComponentByClass<UHeroArenaHeroSelectionComponent>() : nullptr)
			{
				Selection->SetSelectionLocked(bLocked);
			}
		}
	}

	void SetPlayerTeam(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || World->GetNetMode() == NM_Client || Args.Num() != 2)
		{
			return;
		}

		const int32 ControllerIndex = FCString::Atoi(*Args[0]);
		const int32 TeamId = FCString::Atoi(*Args[1]);
		int32 CurrentIndex = 0;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It, ++CurrentIndex)
		{
			if (CurrentIndex != ControllerIndex)
			{
				continue;
			}

			APlayerController* PlayerController = It->Get();
			if (ULyraTeamSubsystem* Teams = World->GetSubsystem<ULyraTeamSubsystem>())
			{
				if (TeamId != INDEX_NONE && !Teams->DoesTeamExist(TeamId))
				{
					UE_LOG(LogTemp, Warning,
						TEXT("HeroArena debug team change rejected: controller=%d unknown_team=%d"),
						ControllerIndex, TeamId);
					return;
				}
				const int32 OldTeamId = Teams->FindTeamFromObject(PlayerController);
				const bool bChanged = Teams->ChangeTeamForActor(PlayerController, TeamId);
				UE_LOG(LogTemp, Display,
					TEXT("HeroArena debug team change: controller=%d old_team=%d requested_team=%d actual_team=%d changed=%s"),
					ControllerIndex, OldTeamId, TeamId, Teams->FindTeamFromObject(PlayerController),
					bChanged ? TEXT("true") : TEXT("false"));
			}
			return;
		}
	}

	void LogPlayerState(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || World->GetNetMode() == NM_Client || Args.Num() != 1)
		{
			return;
		}
		const int32 ControllerIndex = FCString::Atoi(*Args[0]);
		int32 CurrentIndex = 0;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It, ++CurrentIndex)
		{
			if (CurrentIndex != ControllerIndex)
			{
				continue;
			}
			ALyraPlayerState* PlayerState = It->Get() ? It->Get()->GetPlayerState<ALyraPlayerState>() : nullptr;
			ULyraAbilitySystemComponent* ASC = PlayerState ? PlayerState->GetLyraAbilitySystemComponent() : nullptr;
			const ULyraHealthSet* HealthSet = ASC ? ASC->GetSet<ULyraHealthSet>() : nullptr;
			const ULyraCombatSet* CombatSet = ASC ? ASC->GetSet<ULyraCombatSet>() : nullptr;
			const UHeroArenaHeroSelectionComponent* Selection =
				PlayerState ? PlayerState->FindComponentByClass<UHeroArenaHeroSelectionComponent>() : nullptr;
			const UHeroArenaHeroDefinition* Hero = Selection ? Selection->GetSelectedHero() : nullptr;
			const ACharacter* Character = Cast<ACharacter>(It->Get() ? It->Get()->GetPawn() : nullptr);
			UE_LOG(LogTemp, Display,
				TEXT("HeroArena player snapshot: controller=%d player_state=%s pawn=%s hero=%s team=%d health=%.2f max_health=%.2f base_damage=%.2f overclock=%d max_walk_speed=%.2f"),
				ControllerIndex, *GetNameSafe(PlayerState), *GetNameSafe(It->Get() ? It->Get()->GetPawn() : nullptr),
				Hero ? *Hero->HeroId.ToString() : TEXT("None"),
				PlayerState ? PlayerState->GetTeamId() : INDEX_NONE,
				HealthSet ? HealthSet->GetHealth() : -1.0f, HealthSet ? HealthSet->GetMaxHealth() : -1.0f,
				CombatSet ? CombatSet->GetBaseDamage() : -1.0f,
				ASC ? ASC->GetTagCount(FGameplayTag::RequestGameplayTag(TEXT("Status.HeroArena.Overclocked"))) : 0,
				Character && Character->GetCharacterMovement() ? Character->GetCharacterMovement()->MaxWalkSpeed : -1.0f);
			return;
		}
	}

	void LogBlinkCharges(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || World->GetNetMode() == NM_Client || Args.Num() != 1)
		{
			return;
		}
		const int32 ControllerIndex = FCString::Atoi(*Args[0]);
		int32 CurrentIndex = 0;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It, ++CurrentIndex)
		{
			if (CurrentIndex != ControllerIndex)
			{
				continue;
			}
			const APlayerState* PlayerState = It->Get() ? It->Get()->PlayerState : nullptr;
			const UHeroArenaBlinkChargeComponent* Charges =
				PlayerState ? PlayerState->FindComponentByClass<UHeroArenaBlinkChargeComponent>() : nullptr;
			UE_LOG(LogTemp, Display,
				TEXT("HeroArena blink charge snapshot: controller=%d player_state=%s charges=%d/%d next=%.2f"),
				ControllerIndex, *GetNameSafe(PlayerState), Charges ? Charges->GetCurrentCharges() : -1,
				Charges ? Charges->GetMaxCharges() : -1, Charges ? Charges->GetNextChargeRemaining() : -1.0f);
			return;
		}
	}

	void DamagePlayer(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || World->GetNetMode() == NM_Client || Args.Num() != 3)
		{
			return;
		}
		const int32 SourceIndex = FCString::Atoi(*Args[0]);
		const int32 TargetIndex = FCString::Atoi(*Args[1]);
		const float Damage = FMath::Max(0.0f, FCString::Atof(*Args[2]));
		TArray<APlayerController*> Controllers;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			Controllers.Add(It->Get());
		}
		if (!Controllers.IsValidIndex(SourceIndex) || !Controllers.IsValidIndex(TargetIndex) ||
			SourceIndex == TargetIndex || Damage <= 0.0f)
		{
			return;
		}
		ALyraPlayerState* SourcePS = Controllers[SourceIndex]
			? Controllers[SourceIndex]->GetPlayerState<ALyraPlayerState>() : nullptr;
		ALyraPlayerState* TargetPS = Controllers[TargetIndex]
			? Controllers[TargetIndex]->GetPlayerState<ALyraPlayerState>() : nullptr;
		ULyraAbilitySystemComponent* SourceASC = SourcePS ? SourcePS->GetLyraAbilitySystemComponent() : nullptr;
		ULyraAbilitySystemComponent* TargetASC = TargetPS ? TargetPS->GetLyraAbilitySystemComponent() : nullptr;
		const TSubclassOf<UGameplayEffect> DamageEffect =
			ULyraAssetManager::GetSubclass(ULyraGameData::Get().DamageGameplayEffect_SetByCaller);
		FGameplayEffectSpecHandle Spec = SourceASC
			? SourceASC->MakeOutgoingSpec(DamageEffect, 1.0f, SourceASC->MakeEffectContext())
			: FGameplayEffectSpecHandle();
		if (!TargetASC || !Spec.IsValid())
		{
			return;
		}
		Spec.Data->SetSetByCallerMagnitude(LyraGameplayTags::SetByCaller_Damage, Damage);
		TargetASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
		UE_LOG(LogTemp, Display,
			TEXT("HeroArena debug damage: source=%d target=%d damage=%.2f source_ps=%s target_ps=%s"),
			SourceIndex, TargetIndex, Damage, *GetNameSafe(SourcePS), *GetNameSafe(TargetPS));
	}

	void PurifyPlayer(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || World->GetNetMode() == NM_Client || Args.Num() != 1)
		{
			return;
		}
		const int32 TargetIndex = FCString::Atoi(*Args[0]);
		int32 CurrentIndex = 0;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It, ++CurrentIndex)
		{
			if (CurrentIndex != TargetIndex)
			{
				continue;
			}
			ALyraPlayerState* TargetPS = It->Get() ? It->Get()->GetPlayerState<ALyraPlayerState>() : nullptr;
			ULyraAbilitySystemComponent* TargetASC = TargetPS ? TargetPS->GetLyraAbilitySystemComponent() : nullptr;
			const int32 Removed = UHeroArenaPurificationRules::RemovePurifiableEffects(TargetASC);
			UE_LOG(LogTemp, Display,
				TEXT("HeroArena debug purification: target=%d target_ps=%s removed=%d"),
				TargetIndex, *GetNameSafe(TargetPS), Removed);
			return;
		}
	}

	void DamageRelayBeacon(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || World->GetNetMode() == NM_Client || Args.Num() != 2)
		{
			return;
		}
		const int32 SourceIndex = FCString::Atoi(*Args[0]);
		const float Damage = FMath::Max(0.0f, FCString::Atof(*Args[1]));
		int32 CurrentIndex = 0;
		ALyraPlayerState* SourcePS = nullptr;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It, ++CurrentIndex)
		{
			if (CurrentIndex == SourceIndex)
			{
				SourcePS = It->Get() ? It->Get()->GetPlayerState<ALyraPlayerState>() : nullptr;
				break;
			}
		}
		AHeroArenaRelayBeacon* Beacon = nullptr;
		for (TActorIterator<AHeroArenaRelayBeacon> It(World); It; ++It)
		{
			Beacon = *It;
			break;
		}
		ULyraAbilitySystemComponent* SourceASC = SourcePS ? SourcePS->GetLyraAbilitySystemComponent() : nullptr;
		UAbilitySystemComponent* TargetASC = Beacon ? Beacon->GetAbilitySystemComponent() : nullptr;
		const TSubclassOf<UGameplayEffect> DamageEffect =
			ULyraAssetManager::GetSubclass(ULyraGameData::Get().DamageGameplayEffect_SetByCaller);
		FGameplayEffectSpecHandle Spec = SourceASC
			? SourceASC->MakeOutgoingSpec(DamageEffect, 1.0f, SourceASC->MakeEffectContext())
			: FGameplayEffectSpecHandle();
		if (!TargetASC || !Spec.IsValid() || Damage <= 0.0f)
		{
			return;
		}
		Spec.Data->SetSetByCallerMagnitude(LyraGameplayTags::SetByCaller_Damage, Damage);
		TargetASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
		UE_LOG(LogTemp, Display,
			TEXT("HeroArena debug beacon damage: source=%d beacon=%s damage=%.2f health=%.2f"),
			SourceIndex, *GetNameSafe(Beacon), Damage, Beacon->GetBeaconHealth());
	}

	FAutoConsoleCommandWithWorldAndArgs LogGameplayFrameCommand(
		TEXT("HeroArena.Debug.LogGameplayFrame"), TEXT("Development read-only snapshot for input-driven recording; computes one objective path and visible enemy location."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&LogGameplayFrame));
	FAutoConsoleCommandWithWorldAndArgs LogBlinkNetworkCommand(
		TEXT("HeroArena.Debug.LogBlinkNetwork"), TEXT("Read player positions and Blink counts in the current world. Non-owner charge values are not authoritative."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&LogBlinkNetwork));
	FAutoConsoleCommandWithWorldAndArgs SetNetworkSimulationCommand(
		TEXT("HeroArena.Debug.SetNetworkSimulation"), TEXT("Development only: set this world's outgoing packet lag (0-500 ms) and loss (0-10 percent). Use 0 0 to clear."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetNetworkSimulation));
	FAutoConsoleCommandWithWorldAndArgs SetBlinkBlockedCommand(
		TEXT("HeroArena.Debug.SetBlinkBlocked"), TEXT("Development only: block Blink on one server controller without changing the client. Args: controller-index 0-or-1."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetBlinkBlocked));
	FAutoConsoleCommandWithWorldAndArgs RequestHeroCommand(
		TEXT("HeroArena.Debug.RequestHero"), TEXT("Request one hero ID through the owning client's normal server RPC."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RequestHero));
	FAutoConsoleCommandWithWorldAndArgs SetSelectionLockedCommand(
		TEXT("HeroArena.Debug.SetSelectionLocked"), TEXT("Set hero selection lock for all server PlayerStates: 0 or 1."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetSelectionLocked));
	FAutoConsoleCommandWithWorldAndArgs SetPlayerTeamCommand(
		TEXT("HeroArena.Debug.SetPlayerTeam"), TEXT("Set one server PlayerController team by iterator index; -1 clears its team."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetPlayerTeam));
	FAutoConsoleCommandWithWorldAndArgs LogPlayerStateCommand(
		TEXT("HeroArena.Debug.LogPlayerState"), TEXT("Log one server PlayerController's gameplay attribute snapshot."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&LogPlayerState));
	FAutoConsoleCommandWithWorldAndArgs LogGASCommand(
		TEXT("HeroArena.Debug.LogGAS"), TEXT("Read hero selection, ASC avatar and hero-owned ability specs in this world."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&LogGAS));
	FAutoConsoleCommandWithWorldAndArgs LogBlinkChargesCommand(
		TEXT("HeroArena.Debug.LogBlinkCharges"), TEXT("Log one server PlayerController's Phase Blink charge snapshot."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&LogBlinkCharges));
	FAutoConsoleCommandWithWorldAndArgs DamagePlayerCommand(
		TEXT("HeroArena.Debug.DamagePlayer"), TEXT("Apply standard Lyra damage from one server player index to another."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DamagePlayer));
	FAutoConsoleCommandWithWorldAndArgs PurifyPlayerCommand(
		TEXT("HeroArena.Debug.PurifyPlayer"), TEXT("Remove opt-in purifiable effects from one server player index."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&PurifyPlayer));
	FAutoConsoleCommandWithWorldAndArgs DamageRelayBeaconCommand(
		TEXT("HeroArena.Debug.DamageRelayBeacon"), TEXT("Apply standard Lyra damage from one player to the first Relay beacon."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DamageRelayBeacon));
}
#endif

void StartHeroArenaNetworkProbe();
void StopHeroArenaNetworkProbe();

class FHeroArenaRuntimeModule : public FDefaultModuleImpl
{
public:
	virtual void StartupModule() override { StartHeroArenaNetworkProbe(); }
	virtual void ShutdownModule() override { StopHeroArenaNetworkProbe(); }
};

IMPLEMENT_MODULE(FHeroArenaRuntimeModule, HeroArenaRuntime)
