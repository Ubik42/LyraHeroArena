#include "Abilities/HeroArenaGameplayAbility_PurificationNeedle.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Abilities/LyraAbilitySimpleFailureMessage.h"
#include "Character/LyraCharacter.h"
#include "Character/LyraHealthComponent.h"
#include "Effects/HeroArenaGameplayEffect_PurificationNeedleCooldown.h"
#include "Effects/HeroArenaPurificationRules.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "Teams/LyraTeamSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaGameplayAbility_PurificationNeedle)

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaPurificationNeedle, Log, All);

UHeroArenaGameplayAbility_PurificationNeedle::UHeroArenaGameplayAbility_PurificationNeedle(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	ActivationPolicy = ELyraAbilityActivationPolicy::OnInputTriggered;
	CooldownGameplayEffectClass = UHeroArenaGameplayEffect_PurificationNeedleCooldown::StaticClass();

	FGameplayTagContainer Tags;
	Tags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Ability.Type.Action.PurificationNeedle")));
	SetAssetTags(Tags);
}

void UHeroArenaGameplayAbility_PurificationNeedle::ActivateAbility(
	FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	ALyraCharacter* Character = GetLyraCharacterFromActorInfo();
	AController* Controller = Character ? Character->GetController() : nullptr;
	if (!Character || !Controller)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(HeroArenaPurificationNeedle), true, Character);
	FHitResult Hit;
	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		Hit, ViewLocation, ViewLocation + ViewRotation.Vector() * TraceRange, ECC_Visibility, QueryParams);
	AActor* TargetActor = bHit ? Hit.GetActor() : nullptr;
	ULyraHealthComponent* TargetHealth = ULyraHealthComponent::FindHealthComponent(TargetActor);
	UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(TargetActor);
	ULyraTeamSubsystem* Teams = GetWorld()->GetSubsystem<ULyraTeamSubsystem>();
	int32 SourceTeamId = INDEX_NONE;
	int32 TargetTeamId = INDEX_NONE;
	const ELyraTeamComparison Relationship = Teams && TargetActor
		? Teams->CompareTeams(Character, TargetActor, SourceTeamId, TargetTeamId)
		: ELyraTeamComparison::InvalidArgument;

	auto Reject = [&](const TCHAR* Reason)
	{
		if (ActorInfo && ActorInfo->IsLocallyControlled())
		{
			FText UserFacingReason = NSLOCTEXT(
				"HeroArenaAbilityFeedback", "AimPurificationNeedle", "净化针需要瞄准一名队友");
			if (FCString::Strcmp(Reason, TEXT("nothing_purifiable")) == 0)
			{
				UserFacingReason = NSLOCTEXT(
					"HeroArenaAbilityFeedback", "NothingToPurify", "目标当前没有可净化状态");
			}
			else if (FCString::Strcmp(Reason, TEXT("not_living_ally")) == 0 ||
				FCString::Strcmp(Reason, TEXT("target_dead")) == 0)
			{
				UserFacingReason = NSLOCTEXT(
					"HeroArenaAbilityFeedback", "PurifyLivingAlly", "净化针只能作用于存活队友");
			}
			FLyraAbilitySimpleFailureMessage Message;
			Message.PlayerController = Cast<APlayerController>(Controller);
			Message.UserFacingReason = UserFacingReason;
			UGameplayMessageSubsystem::Get(this).BroadcastMessage(
				FGameplayTag::RequestGameplayTag(TEXT("Ability.UserFacingSimpleActivateFail.Message")), Message);
		}
		if (Character->HasAuthority())
		{
			UE_LOG(LogHeroArenaPurificationNeedle, Log,
				TEXT("Purification needle rejected: reason=%s source=%s team=%d target=%s team=%d"),
				Reason, *GetNameSafe(Character), SourceTeamId, *GetNameSafe(TargetActor), TargetTeamId);
		}
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
	};

	if (!TargetActor || !TargetHealth || !TargetASC)
	{
		Reject(TEXT("no_valid_target"));
		return;
	}
	if (TargetActor == Character)
	{
		Reject(TEXT("self_target"));
		return;
	}
	if (Relationship != ELyraTeamComparison::OnSameTeam)
	{
		Reject(TEXT("not_living_ally"));
		return;
	}
	if (TargetHealth->IsDeadOrDying())
	{
		Reject(TEXT("target_dead"));
		return;
	}
	if (!UHeroArenaPurificationRules::HasPurifiableEffect(TargetASC))
	{
		Reject(TEXT("nothing_purifiable"));
		return;
	}
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		Reject(TEXT("commit_failed"));
		return;
	}

	if (Character->HasAuthority())
	{
		const FGameplayTag SuppressedTag = FGameplayTag::RequestGameplayTag(TEXT("Status.HeroArena.HealingSuppressed"));
		const FGameplayTag AmplifiedTag = FGameplayTag::RequestGameplayTag(TEXT("Status.HeroArena.HealingAmplified"));
		const int32 SuppressedBefore = TargetASC->GetTagCount(SuppressedTag);
		const int32 AmplifiedBefore = TargetASC->GetTagCount(AmplifiedTag);
		const int32 Removed = UHeroArenaPurificationRules::RemovePurifiableEffects(TargetASC);

		FGameplayCueParameters CueParameters;
		CueParameters.Instigator = Character;
		CueParameters.EffectCauser = Character;
		CueParameters.Location = Hit.ImpactPoint;
		TargetASC->ExecuteGameplayCue(
			FGameplayTag::RequestGameplayTag(TEXT("GameplayCue.Character.Heal")), CueParameters);

		UE_LOG(LogHeroArenaPurificationNeedle, Log,
			TEXT("Purification needle resolved: source=%s target=%s removed=%d suppressed=%d->%d amplified=%d->%d"),
			*GetNameSafe(Character), *GetNameSafe(TargetActor), Removed,
			SuppressedBefore, TargetASC->GetTagCount(SuppressedTag),
			AmplifiedBefore, TargetASC->GetTagCount(AmplifiedTag));
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
