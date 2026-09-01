#include "Abilities/HeroArenaGameplayAbility_RelayQuickMaintenance.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystem/Abilities/LyraAbilitySimpleFailureMessage.h"
#include "AbilitySystem/Attributes/LyraHealthSet.h"
#include "Actors/HeroArenaBarrier.h"
#include "Actors/HeroArenaRelayBeacon.h"
#include "Actors/HeroArenaTeamAmplifier.h"
#include "Character/LyraCharacter.h"
#include "Effects/HeroArenaGameplayEffect_RelayQuickMaintenanceCooldown.h"
#include "Effects/HeroArenaHealingRules.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "Teams/LyraTeamSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaGameplayAbility_RelayQuickMaintenance)

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaRelayQuickMaintenance, Log, All);

UHeroArenaGameplayAbility_RelayQuickMaintenance::UHeroArenaGameplayAbility_RelayQuickMaintenance(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	ActivationPolicy = ELyraAbilityActivationPolicy::OnInputTriggered;
	CooldownGameplayEffectClass = UHeroArenaGameplayEffect_RelayQuickMaintenanceCooldown::StaticClass();
	FGameplayTagContainer Tags;
	Tags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Ability.Type.Action.RelayQuickMaintenance")));
	SetAssetTags(Tags);
}

void UHeroArenaGameplayAbility_RelayQuickMaintenance::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	ALyraCharacter* Character = GetLyraCharacterFromActorInfo();
	UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo();
	if (!Character || !SourceASC)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	FVector ViewOrigin = Character->GetPawnViewLocation();
	FRotator ViewRotation = Character->GetActorRotation();
	if (AController* Controller = Character->GetController())
	{
		ViewRotation = Controller->GetControlRotation();
	}
	AActor* Target = FindMaintenanceTarget(Character, ViewOrigin, ViewRotation.Vector());
	if (!Target)
	{
		if (ActorInfo && ActorInfo->IsLocallyControlled())
		{
			FLyraAbilitySimpleFailureMessage Message;
			Message.PlayerController = Cast<APlayerController>(Character->GetController());
			Message.UserFacingReason = NSLOCTEXT("HeroArenaAbilityFeedback", "NoMaintenanceTarget",
				"准星附近没有受损的友方部署物");
			UGameplayMessageSubsystem::Get(this).BroadcastMessage(
				FGameplayTag::RequestGameplayTag(TEXT("Ability.UserFacingSimpleActivateFail.Message")), Message);
		}
		if (Character->HasAuthority())
		{
			UE_LOG(LogHeroArenaRelayQuickMaintenance, Log,
				TEXT("Relay quick maintenance rejected: source=%s reason=no_target"), *GetNameSafe(Character));
		}
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (Character->HasAuthority())
	{
		const float ActualRepair = UHeroArenaHealingRules::ApplyStandardHealing(
			SourceASC, Target, Character, RepairAmount);
		if (ActualRepair > 0.0f)
		{
			if (UAbilitySystemComponent* TargetASC =
				UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target))
			{
				FGameplayCueParameters CueParameters;
				CueParameters.Instigator = Character;
				CueParameters.EffectCauser = Character;
				CueParameters.Location = Target->GetActorLocation();
				TargetASC->ExecuteGameplayCue(
					FGameplayTag::RequestGameplayTag(TEXT("GameplayCue.Character.Heal")), CueParameters);
			}
		}
		UE_LOG(LogHeroArenaRelayQuickMaintenance, Log,
			TEXT("Relay quick maintenance resolved: source=%s target=%s requested=%.1f actual=%.1f"),
			*GetNameSafe(Character), *GetNameSafe(Target), RepairAmount, ActualRepair);
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

AActor* UHeroArenaGameplayAbility_RelayQuickMaintenance::FindMaintenanceTarget(
	const AActor* Source, const FVector& ViewOrigin, const FVector& ViewDirection) const
{
	UWorld* World = GetWorld();
	ULyraTeamSubsystem* Teams = World ? World->GetSubsystem<ULyraTeamSubsystem>() : nullptr;
	if (!World || !Teams)
	{
		return nullptr;
	}

	AActor* BestTarget = nullptr;
	float BestScore = TNumericLimits<float>::Max();
	auto ConsiderTarget = [&](AActor* Candidate)
	{
		if (!IsValid(Candidate) || Teams->CompareTeams(Source, Candidate) != ELyraTeamComparison::OnSameTeam)
		{
			return;
		}
		const UAbilitySystemComponent* CandidateASC =
			UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Candidate);
		const float CurrentHealth = CandidateASC
			? CandidateASC->GetNumericAttribute(ULyraHealthSet::GetHealthAttribute()) : 0.0f;
		const float MaxHealth = CandidateASC
			? CandidateASC->GetNumericAttribute(ULyraHealthSet::GetMaxHealthAttribute()) : 0.0f;
		if (MaxHealth <= 0.0f || CurrentHealth >= MaxHealth - 0.5f)
		{
			return;
		}
		const FVector InteractionPoint = Candidate->GetActorLocation() + FVector::UpVector * 55.0f;
		const FVector ToTarget = InteractionPoint - ViewOrigin;
		const float AlongAim = FVector::DotProduct(ToTarget, ViewDirection);
		if (AlongAim <= 0.0f || AlongAim > MaintenanceRange)
		{
			return;
		}
		const float OffAxis = (ToTarget - ViewDirection * AlongAim).Size();
		if (OffAxis > AimAssistRadius)
		{
			return;
		}

		FCollisionQueryParams Params(SCENE_QUERY_STAT(HeroArenaRelayMaintenanceSight), true, Source);
		FHitResult Hit;
		const bool bBlocked = World->LineTraceSingleByChannel(Hit, ViewOrigin, InteractionPoint, ECC_Visibility, Params)
			&& Hit.GetActor() != Candidate;
		if (bBlocked)
		{
			return;
		}
		const float Score = AlongAim + OffAxis * 2.0f;
		if (Score < BestScore)
		{
			BestScore = Score;
			BestTarget = Candidate;
		}
	};

	for (TActorIterator<AHeroArenaBarrier> It(World); It; ++It)
	{
		ConsiderTarget(*It);
	}
	for (TActorIterator<AHeroArenaRelayBeacon> It(World); It; ++It)
	{
		ConsiderTarget(*It);
	}
	for (TActorIterator<AHeroArenaTeamAmplifier> It(World); It; ++It)
	{
		ConsiderTarget(*It);
	}
	return BestTarget;
}
