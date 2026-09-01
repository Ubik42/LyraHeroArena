#include "Components/HeroArenaBotTacticsComponent.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystem/Attributes/LyraHealthSet.h"
#include "AbilitySystem/LyraAbilitySystemComponent.h"
#include "Actors/HeroArenaBarrier.h"
#include "Actors/HeroArenaRelayBeacon.h"
#include "Actors/HeroArenaRelayJumpPad.h"
#include "Actors/HeroArenaTeamAmplifier.h"
#include "Components/HeroArenaRecallHistoryComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Effects/HeroArenaPurificationRules.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "Hero/HeroArenaHeroDefinition.h"
#include "Hero/HeroArenaHeroRoster.h"
#include "Hero/HeroArenaHeroSelectionComponent.h"
#include "Player/LyraPlayerState.h"
#include "Teams/LyraTeamSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaBotTacticsComponent)

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaBotTactics, Log, All);

UHeroArenaBotTacticsComponent::UHeroArenaBotTacticsComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UHeroArenaBotTacticsComponent::BeginPlay()
{
	Super::BeginPlay();
	const ALyraPlayerState* PlayerState = Cast<ALyraPlayerState>(GetOwner());
	if (!PlayerState || !PlayerState->HasAuthority() || !PlayerState->IsABot())
	{
		return;
	}
	GetWorld()->GetTimerManager().SetTimer(TacticsTimer, this, &ThisClass::EvaluateTactics, 1.25f, true, 0.4f);
}

void UHeroArenaBotTacticsComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(TacticsTimer);
	Super::EndPlay(EndPlayReason);
}

bool UHeroArenaBotTacticsComponent::EnsureBalancedHero(ALyraPlayerState* PlayerState)
{
	UHeroArenaHeroSelectionComponent* Selection = PlayerState
		? PlayerState->FindComponentByClass<UHeroArenaHeroSelectionComponent>() : nullptr;
	if (!Selection || !Selection->GetRoster()) return false;

	const UHeroArenaHeroDefinition* CurrentHero = Selection->GetSelectedHero();
	TArray<const ALyraPlayerState*> TeamBots;
	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (!GameState) return false;
	static const FName TeamComposition[] = { TEXT("Bulwark"), TEXT("Striker"), TEXT("Phase"), TEXT("Pulse"), TEXT("Relay") };
	TArray<FName> UnfilledRoles;
	UnfilledRoles.Append(TeamComposition, UE_ARRAY_COUNT(TeamComposition));
	for (const APlayerState* Candidate : GameState->PlayerArray)
	{
		const ALyraPlayerState* LyraCandidate = Cast<ALyraPlayerState>(Candidate);
		if (!LyraCandidate || LyraCandidate->GetTeamId() != PlayerState->GetTeamId()) continue;
		if (LyraCandidate->IsABot())
		{
			TeamBots.Add(LyraCandidate);
		}
		else if (const UHeroArenaHeroSelectionComponent* HumanSelection =
			LyraCandidate->FindComponentByClass<UHeroArenaHeroSelectionComponent>())
		{
			if (const UHeroArenaHeroDefinition* HumanHero = HumanSelection->GetSelectedHero())
			{
				UnfilledRoles.RemoveSingle(HumanHero->HeroId);
			}
		}
	}
	TeamBots.Sort([](const ALyraPlayerState& A, const ALyraPlayerState& B)
	{
		return A.GetPlayerId() < B.GetPlayerId();
	});
	const int32 Slot = TeamBots.IndexOfByKey(PlayerState);
	if (Slot == INDEX_NONE) return false;

	const FName DesiredHero = UnfilledRoles.IsValidIndex(Slot)
		? UnfilledRoles[Slot]
		: TeamComposition[(Slot - UnfilledRoles.Num()) % UE_ARRAY_COUNT(TeamComposition)];
	if (!Selection->GetRoster()->FindHeroById(DesiredHero)) return false;
	if (!CurrentHero || CurrentHero->HeroId != DesiredHero)
	{
		const bool bSelected = Selection->SelectHeroById(DesiredHero);
		if (bSelected)
		{
			UE_LOG(LogHeroArenaBotTactics, Log, TEXT("Bot role assigned: player=%s team=%d slot=%d hero=%s"),
				*GetNameSafe(PlayerState), PlayerState->GetTeamId(), Slot, *DesiredHero.ToString());
		}
		return bSelected;
	}
	return true;
}

float UHeroArenaBotTacticsComponent::GetHealthRatio(const ALyraPlayerState* PlayerState) const
{
	const ULyraAbilitySystemComponent* ASC = PlayerState ? PlayerState->GetLyraAbilitySystemComponent() : nullptr;
	const ULyraHealthSet* Health = ASC ? ASC->GetSet<ULyraHealthSet>() : nullptr;
	return Health && Health->GetMaxHealth() > 0.0f ? Health->GetHealth() / Health->GetMaxHealth() : 0.0f;
}

bool UHeroArenaBotTacticsComponent::IsViableTarget(const ALyraPlayerState* PlayerState) const
{
	return PlayerState && PlayerState->GetPawn() && GetHealthRatio(PlayerState) > 0.0f;
}

void UHeroArenaBotTacticsComponent::AimAt(AActor* Target) const
{
	const ALyraPlayerState* PlayerState = Cast<ALyraPlayerState>(GetOwner());
	APawn* Pawn = PlayerState ? PlayerState->GetPawn() : nullptr;
	AController* Controller = Pawn ? Pawn->GetController() : nullptr;
	if (Controller && Target)
	{
		const APawn* TargetPawn = Cast<APawn>(Target);
		const FVector AimPoint = TargetPawn ? TargetPawn->GetPawnViewLocation() : Target->GetActorLocation();
		Controller->SetControlRotation((AimPoint - Pawn->GetPawnViewLocation()).Rotation());
	}
}

void UHeroArenaBotTacticsComponent::AimForPhaseFlank(AActor* Target, const float SideSign) const
{
	const ALyraPlayerState* PlayerState = Cast<ALyraPlayerState>(GetOwner());
	APawn* Pawn = PlayerState ? PlayerState->GetPawn() : nullptr;
	AController* Controller = Pawn ? Pawn->GetController() : nullptr;
	if (!Controller || !Target)
	{
		return;
	}

	FVector Direct = Target->GetActorLocation() - Pawn->GetActorLocation();
	Direct.Z = 0.0f;
	Direct = Direct.GetSafeNormal();
	if (Direct.IsNearlyZero())
	{
		return;
	}

	const FVector Lateral = FVector::CrossProduct(FVector::UpVector, Direct).GetSafeNormal();
	const FVector FlankDirection = (Direct + Lateral * FMath::Sign(SideSign) * 0.75f).GetSafeNormal();
	Controller->SetControlRotation(FlankDirection.Rotation());
	const float OffsetDegrees = FMath::RadiansToDegrees(FMath::Acos(
		FMath::Clamp(FVector::DotProduct(Direct, FlankDirection), -1.0f, 1.0f)));
	UE_LOG(LogHeroArenaBotTactics, Verbose,
		TEXT("Phase flank aim: player=%s target=%s side=%s offset=%.1f degrees"),
		*GetNameSafe(PlayerState), *GetNameSafe(Target), SideSign >= 0.0f ? TEXT("left") : TEXT("right"),
		OffsetDegrees);
}

bool UHeroArenaBotTacticsComponent::TryActivate(ULyraAbilitySystemComponent* AbilitySystem,
	const TCHAR* AbilityTag, const TCHAR* Reason)
{
	if (!AbilitySystem) return false;
	FGameplayTagContainer Tags(FGameplayTag::RequestGameplayTag(AbilityTag));
	const bool bActivated = AbilitySystem->TryActivateAbilitiesByTag(Tags, true);
	if (bActivated)
	{
		UE_LOG(LogHeroArenaBotTactics, Verbose, TEXT("Bot tactic committed: player=%s ability=%s reason=%s"),
			*GetNameSafe(GetOwner()), AbilityTag, Reason);
	}
	return bActivated;
}

void UHeroArenaBotTacticsComponent::EvaluateTactics()
{
	ALyraPlayerState* PlayerState = Cast<ALyraPlayerState>(GetOwner());
	if (!PlayerState || !PlayerState->HasAuthority() || !PlayerState->IsABot()) return;
	const double Now = FPlatformTime::Seconds();
	if (Now >= NextHeroAssignmentAttempt)
	{
		NextHeroAssignmentAttempt = Now + 2.0;
		if (!EnsureBalancedHero(PlayerState)) return;
	}
	if (Now < NextDecisionTime || !PlayerState->GetPawn()) return;
	NextDecisionTime = Now + FMath::FRandRange(0.9, 1.5);

	UHeroArenaHeroSelectionComponent* Selection = PlayerState->FindComponentByClass<UHeroArenaHeroSelectionComponent>();
	const UHeroArenaHeroDefinition* Hero = Selection ? Selection->GetSelectedHero() : nullptr;
	const bool bUltimateReady = Selection && Selection->IsUltimateReady();
	ULyraAbilitySystemComponent* ASC = PlayerState->GetLyraAbilitySystemComponent();
	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (!Hero || !ASC || !GameState) return;

	ALyraPlayerState* NearestEnemy = nullptr;
	ALyraPlayerState* LowestAlly = nullptr;
	ALyraPlayerState* PurifiableAlly = nullptr;
	ALyraPlayerState* OverclockAlly = nullptr;
	float EnemyDistance = BIG_NUMBER;
	float LowestAllyHealth = 1.01f;
	float PurifiableAllyDistance = BIG_NUMBER;
	float OverclockAllyDistance = BIG_NUMBER;
	int32 OverclockAllyPriority = MAX_int32;
	int32 CloseEnemies = 0;
	int32 CloseAllies = 0;
	const FVector Origin = PlayerState->GetPawn()->GetActorLocation();
	for (APlayerState* CandidateBase : GameState->PlayerArray)
	{
		ALyraPlayerState* Candidate = Cast<ALyraPlayerState>(CandidateBase);
		if (!Candidate || Candidate == PlayerState || !IsViableTarget(Candidate)) continue;
		const float Distance = FVector::Dist(Origin, Candidate->GetPawn()->GetActorLocation());
		if (Candidate->GetTeamId() == PlayerState->GetTeamId())
		{
			if (Distance < 1000.0f) ++CloseAllies;
			if (Distance < 1800.0f
				&& !Candidate->GetLyraAbilitySystemComponent()->HasMatchingGameplayTag(
					FGameplayTag::RequestGameplayTag(TEXT("Status.HeroArena.Overclocked"))))
			{
				FCollisionQueryParams SightParams(
					SCENE_QUERY_STAT(HeroArenaBotOverclockSight), true, PlayerState->GetPawn());
				FHitResult SightHit;
				const bool bBlocked = GetWorld()->LineTraceSingleByChannel(
					SightHit, PlayerState->GetPawn()->GetPawnViewLocation(),
					Candidate->GetPawn()->GetPawnViewLocation(), ECC_Visibility, SightParams)
					&& SightHit.GetActor() != Candidate->GetPawn();
				if (!bBlocked)
				{
					const UHeroArenaHeroSelectionComponent* CandidateSelection =
						Candidate->FindComponentByClass<UHeroArenaHeroSelectionComponent>();
					const UHeroArenaHeroDefinition* CandidateHero = CandidateSelection
						? CandidateSelection->GetSelectedHero() : nullptr;
					const int32 Priority = CandidateHero && CandidateHero->Role == EHeroArenaRole::Vanguard ? 0
						: CandidateHero && CandidateHero->Role == EHeroArenaRole::Damage ? 1 : 2;
					if (Priority < OverclockAllyPriority
						|| (Priority == OverclockAllyPriority && Distance < OverclockAllyDistance))
					{
						OverclockAlly = Candidate;
						OverclockAllyDistance = Distance;
						OverclockAllyPriority = Priority;
					}
				}
			}
			if (Distance < 1800.0f && Distance < PurifiableAllyDistance
				&& UHeroArenaPurificationRules::HasPurifiableEffect(Candidate->GetLyraAbilitySystemComponent()))
			{
				FCollisionQueryParams SightParams(
					SCENE_QUERY_STAT(HeroArenaBotPurificationSight), true, PlayerState->GetPawn());
				FHitResult SightHit;
				const bool bBlocked = GetWorld()->LineTraceSingleByChannel(
					SightHit, PlayerState->GetPawn()->GetPawnViewLocation(),
					Candidate->GetPawn()->GetPawnViewLocation(), ECC_Visibility, SightParams)
					&& SightHit.GetActor() != Candidate->GetPawn();
				if (!bBlocked)
				{
					PurifiableAllyDistance = Distance;
					PurifiableAlly = Candidate;
				}
			}
			const float Health = GetHealthRatio(Candidate);
			if (Distance < 1800.0f && Health < LowestAllyHealth)
			{
				LowestAllyHealth = Health;
				LowestAlly = Candidate;
			}
		}
		else
		{
			if (Distance < 700.0f) ++CloseEnemies;
			if (Distance < EnemyDistance)
			{
				EnemyDistance = Distance;
				NearestEnemy = Candidate;
			}
		}
	}

	const float SelfHealth = GetHealthRatio(PlayerState);
	bool bNearestEnemyVisible = false;
	if (NearestEnemy)
	{
		FCollisionQueryParams EnemySightParams(
			SCENE_QUERY_STAT(HeroArenaBotEnemySight), true, PlayerState->GetPawn());
		FHitResult EnemySightHit;
		bNearestEnemyVisible = !GetWorld()->LineTraceSingleByChannel(
			EnemySightHit, PlayerState->GetPawn()->GetPawnViewLocation(),
			NearestEnemy->GetPawn()->GetPawnViewLocation(), ECC_Visibility, EnemySightParams)
			|| EnemySightHit.GetActor() == NearestEnemy->GetPawn();
	}
	if (Hero->HeroId == TEXT("Bulwark"))
	{
		if (NearestEnemy && EnemyDistance < 2600.0f
			&& (SelfHealth < 0.85f || (bNearestEnemyVisible && EnemyDistance > 1700.0f))
			&& TryActivate(ASC, TEXT("Ability.Type.Action.KineticShield"), TEXT("engage_with_overshield"))) return;
		if (NearestEnemy && (CloseEnemies >= 2 || EnemyDistance < 650.0f))
		{
			AimAt(NearestEnemy->GetPawn());
			if (TryActivate(ASC, TEXT("Ability.Type.Action.GroundSlam"), TEXT("close_pressure"))) return;
		}
		// Lyra's ranged bots typically settle outside the old 1500 uu trigger.
		// Charge from the measured engagement band so Bulwark can actually reach
		// its 650 uu ground-slam range instead of behaving like another rifle bot.
		if (NearestEnemy && bNearestEnemyVisible && EnemyDistance > 1700.0f && EnemyDistance < 2600.0f)
		{
			AimAt(NearestEnemy->GetPawn());
			if (TryActivate(ASC, TEXT("Ability.Type.Action.Charge"), TEXT("close_gap"))) return;
		}
		if (NearestEnemy && EnemyDistance < 2600.0f)
		{
			AimAt(NearestEnemy->GetPawn());
			TryActivate(ASC, TEXT("Ability.Type.Action.Barrier"), TEXT("enemy_front"));
		}
	}
	else if (Hero->HeroId == TEXT("Pulse"))
	{
		if (PurifiableAlly)
		{
			AimAt(PurifiableAlly->GetPawn());
			if (TryActivate(ASC, TEXT("Ability.Type.Action.PurificationNeedle"), TEXT("ally_debuff"))) return;
		}
		if (LowestAlly && LowestAllyHealth < 0.45f)
		{
			AimAt(LowestAlly->GetPawn());
			if (TryActivate(ASC, TEXT("Ability.Type.Action.BioticGrenade"), TEXT("emergency_heal"))) return;
		}
		if (OverclockAlly && NearestEnemy && EnemyDistance < (bUltimateReady ? 2600.0f : 2000.0f))
		{
			AimAt(OverclockAlly->GetPawn());
			if (TryActivate(ASC, TEXT("Ability.Type.Action.OverclockInjection"), TEXT("frontline_engaged"))) return;
		}
		if (LowestAlly && LowestAllyHealth < 0.75f)
		{
			AimAt(LowestAlly->GetPawn());
			if (TryActivate(ASC, TEXT("Ability.Type.Action.BioticGrenade"), TEXT("ally_low"))) return;
		}
		// Lyra's ranged bots usually exchange fire outside the old 1400 uu
		// threshold. Keep the throw inside the observed engagement band and
		// require line of sight so Pulse can pressure a visible group without
		// repeatedly spending the ability into cover.
		if (NearestEnemy && bNearestEnemyVisible && EnemyDistance < 2200.0f)
		{
			AimAt(NearestEnemy->GetPawn());
			TryActivate(ASC, TEXT("Ability.Type.Action.BioticGrenade"), TEXT("enemy_push"));
		}
	}
	else if (Hero->HeroId == TEXT("Phase"))
	{
		const UHeroArenaRecallHistoryComponent* RecallHistory =
			PlayerState->FindComponentByClass<UHeroArenaRecallHistoryComponent>();
		if (SelfHealth < 0.65f && RecallHistory && RecallHistory->HasUsefulRecallSample(15.0f)
			&& TryActivate(ASC, TEXT("Ability.Type.Action.Recall"), TEXT("health_recovery"))) return;
		if (NearestEnemy && EnemyDistance > 700.0f && EnemyDistance < 1700.0f && Now >= NextPhaseBlinkTime)
		{
			AimForPhaseFlank(NearestEnemy->GetPawn(), NextPhaseFlankSide);
			if (TryActivate(ASC, TEXT("Ability.Type.Action.Blink"), TEXT("flank")))
			{
				NextPhaseFlankSide *= -1.0f;
				NextPhaseBlinkTime = Now + 4.0;
				return;
			}
		}
		if (NearestEnemy && EnemyDistance < (bUltimateReady ? 2200.0f : 1000.0f))
		{
			AimAt(NearestEnemy->GetPawn());
			TryActivate(ASC, TEXT("Ability.Type.Action.StickyBomb"), bUltimateReady ? TEXT("ultimate_window") : TEXT("finish"));
		}
	}
	else if (Hero->HeroId == TEXT("Relay"))
	{
		AActor* DamagedDeployable = nullptr;
		float DamagedDeployableDistance = 700.0f;
		const ULyraTeamSubsystem* Teams = GetWorld()->GetSubsystem<ULyraTeamSubsystem>();
		auto ConsiderDamagedDeployable = [&](AActor* Candidate)
		{
			if (!IsValid(Candidate) || !Teams
				|| Teams->CompareTeams(PlayerState->GetPawn(), Candidate) != ELyraTeamComparison::OnSameTeam)
			{
				return;
			}
			const UAbilitySystemComponent* CandidateASC =
				UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Candidate);
			const float Health = CandidateASC
				? CandidateASC->GetNumericAttribute(ULyraHealthSet::GetHealthAttribute()) : 0.0f;
			const float MaxHealth = CandidateASC
				? CandidateASC->GetNumericAttribute(ULyraHealthSet::GetMaxHealthAttribute()) : 0.0f;
			const float Distance = FVector::Dist(Origin, Candidate->GetActorLocation());
			if (Health > 0.5f && MaxHealth > 0.0f && Health < MaxHealth - 0.5f
				&& Distance < DamagedDeployableDistance)
			{
				DamagedDeployable = Candidate;
				DamagedDeployableDistance = Distance;
			}
		};
		for (TActorIterator<AHeroArenaBarrier> It(GetWorld()); It; ++It) ConsiderDamagedDeployable(*It);
		for (TActorIterator<AHeroArenaRelayBeacon> It(GetWorld()); It; ++It) ConsiderDamagedDeployable(*It);
		for (TActorIterator<AHeroArenaTeamAmplifier> It(GetWorld()); It; ++It) ConsiderDamagedDeployable(*It);
		if (DamagedDeployable)
		{
			AimAt(DamagedDeployable);
			if (TryActivate(ASC, TEXT("Ability.Type.Action.RelayQuickMaintenance"), TEXT("repair_deployable"))) return;
		}

		bool bHasBeacon = false;
		for (TActorIterator<AHeroArenaRelayBeacon> It(GetWorld()); It; ++It)
		{
			if (It->GetOwner() == PlayerState)
			{
				bHasBeacon = true;
				break;
			}
		}
		if (!bHasBeacon && Now >= NextRelayBeaconAttemptTime
			&& ((LowestAlly && LowestAllyHealth < 0.85f) || CloseAllies >= 2))
		{
			NextRelayBeaconAttemptTime = Now + 4.0;
			if (TryActivate(ASC, TEXT("Ability.Type.Action.RelayBeacon"), TEXT("team_anchor"))) return;
		}
		if (NearestEnemy && EnemyDistance < (bUltimateReady ? 2600.0f : 1800.0f))
		{
			if (TryActivate(ASC, TEXT("Ability.Type.Action.RelayTeamAmplifier"),
				bUltimateReady ? TEXT("ultimate_window") : TEXT("contested_area"))) return;
		}
		else if (NearestEnemy && EnemyDistance > 1400.0f && CloseAllies > 0
			&& Now >= NextRelayJumpPadAttemptTime)
		{
			bool bHasJumpPad = false;
			for (TActorIterator<AHeroArenaRelayJumpPad> It(GetWorld()); It; ++It)
			{
				if (It->GetOwner() == PlayerState)
				{
					bHasJumpPad = true;
					break;
				}
			}
			if (!bHasJumpPad)
			{
				NextRelayJumpPadAttemptTime = Now + 4.0;
				TryActivate(ASC, TEXT("Ability.Type.Action.RelayJumpPad"), TEXT("rotate"));
			}
		}
	}
	else if (Hero->HeroId == TEXT("Striker"))
	{
		AActor* EnemyDeployable = nullptr;
		float EnemyDeployableDistance = 3500.0f;
		if (const ULyraTeamSubsystem* Teams = GetWorld()->GetSubsystem<ULyraTeamSubsystem>())
		{
			auto ConsiderEnemyDeployable = [&](AActor* Candidate)
			{
				if (!IsValid(Candidate)
					|| Teams->CompareTeams(PlayerState->GetPawn(), Candidate) != ELyraTeamComparison::DifferentTeams)
				{
					return;
				}
				const UAbilitySystemComponent* CandidateASC =
					UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Candidate);
				const float Health = CandidateASC
					? CandidateASC->GetNumericAttribute(ULyraHealthSet::GetHealthAttribute()) : 0.0f;
				const float Distance = FVector::Dist(Origin, Candidate->GetActorLocation());
				FCollisionQueryParams SightParams(
					SCENE_QUERY_STAT(HeroArenaBotDeployableSight), true, PlayerState->GetPawn());
				FHitResult SightHit;
				const bool bBlocked = GetWorld()->LineTraceSingleByChannel(
					SightHit, PlayerState->GetPawn()->GetPawnViewLocation(),
					Candidate->GetActorLocation() + FVector::UpVector * 45.0f,
					ECC_Visibility, SightParams) && SightHit.GetActor() != Candidate;
				if (!bBlocked && Health > 0.5f && Distance < EnemyDeployableDistance)
				{
					EnemyDeployable = Candidate;
					EnemyDeployableDistance = Distance;
				}
			};
			for (TActorIterator<AHeroArenaBarrier> It(GetWorld()); It; ++It) ConsiderEnemyDeployable(*It);
			for (TActorIterator<AHeroArenaRelayBeacon> It(GetWorld()); It; ++It) ConsiderEnemyDeployable(*It);
			for (TActorIterator<AHeroArenaTeamAmplifier> It(GetWorld()); It; ++It) ConsiderEnemyDeployable(*It);
		}

		// Weapon Overload now consumes the shared ultimate resource. Once it is
		// ready, commit it during a visible engagement before spending this
		// decision on the lower-impact recovery field.
		if (bUltimateReady && NearestEnemy && bNearestEnemyVisible && EnemyDistance < 3000.0f
			&& TryActivate(ASC, TEXT("Ability.Type.Action.WeaponOverload"), TEXT("ultimate_window")))
		{
			return;
		}

		if (SelfHealth < 0.80f || (LowestAlly && LowestAllyHealth < 0.85f))
		{
			if (TryActivate(ASC, TEXT("Ability.Type.Action.RecoveryField"), TEXT("sustain")))
			{
				return;
			}
		}

		// The adapter spawns the existing spiral projectile without running the
		// player-controller camera and haptics nodes in Lyra's grenade ability.
		if (EnemyDeployable)
		{
			AimAt(EnemyDeployable);
			if (TryActivate(ASC, TEXT("Ability.Type.Action.BotSpiralGrenade"), TEXT("enemy_deployable")))
			{
				return;
			}
		}
		else if (NearestEnemy && EnemyDistance > 500.0f && EnemyDistance < 2200.0f)
		{
			AimAt(NearestEnemy->GetPawn());
			if (TryActivate(ASC, TEXT("Ability.Type.Action.BotSpiralGrenade"), TEXT("ranged_pressure")))
			{
				return;
			}
		}
	}
}
