#include "Hero/HeroArenaHeroSelectionComponent.h"
#include "Hero/HeroArenaHeroDefinition.h"
#include "Actors/HeroArenaRelayBeacon.h"
#include "Components/HeroArenaBlinkChargeComponent.h"
#include "Effects/HeroArenaPurificationRules.h"

#include "AbilitySystem/Attributes/LyraCombatSet.h"
#include "AbilitySystem/Attributes/LyraHealthSet.h"
#include "AbilitySystem/LyraAbilitySystemComponent.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"
#include "Player/LyraPlayerState.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "LyraGameplayTags.h"
#include "System/LyraAssetManager.h"
#include "System/LyraGameData.h"
#include "Teams/LyraTeamSubsystem.h"

#if !UE_BUILD_SHIPPING
namespace HeroArenaSelectionDebug
{
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

IMPLEMENT_MODULE(FDefaultModuleImpl, HeroArenaRuntime)
