#include "Abilities/HeroArenaGameplayAbility_Recall.h"

#include "Components/HeroArenaRecallHistoryComponent.h"
#include "Effects/HeroArenaGameplayEffect_RecallCooldown.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaGameplayAbility_Recall)

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaRecall, Log, All);

UHeroArenaGameplayAbility_Recall::UHeroArenaGameplayAbility_Recall(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	ActivationPolicy = ELyraAbilityActivationPolicy::OnInputTriggered;
	ActivationGroup = ELyraAbilityActivationGroup::Exclusive_Blocking;
	CooldownGameplayEffectClass = UHeroArenaGameplayEffect_RecallCooldown::StaticClass();

	FGameplayTagContainer Tags;
	Tags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Ability.Type.Action.Recall")));
	SetAssetTags(Tags);
}

void UHeroArenaGameplayAbility_Recall::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	UHeroArenaRecallHistoryComponent* History = ActorInfo && ActorInfo->OwnerActor.IsValid()
		? ActorInfo->OwnerActor->FindComponentByClass<UHeroArenaRecallHistoryComponent>() : nullptr;
	FHeroArenaRecallResult Result;
	const bool bSucceeded = History && History->TryRecallPosition(Result);
	const bool bCooldownApplied = bSucceeded
		&& CommitAbilityCooldown(Handle, ActorInfo, ActivationInfo, true);
	if (bSucceeded && !bCooldownApplied)
	{
		UE_LOG(LogHeroArenaRecall, Warning,
			TEXT("Recall succeeded but GAS cooldown could not be applied: owner=%s"),
			*GetNameSafe(ActorInfo ? ActorInfo->OwnerActor.Get() : nullptr));
	}
	if (bSucceeded || Result.Reason != TEXT("cooldown"))
	{
		UE_LOG(LogHeroArenaRecall, Log,
			TEXT("Recall ability completed: owner=%s success=%s reason=%s seconds=%.2f"),
			*GetNameSafe(ActorInfo ? ActorInfo->OwnerActor.Get() : nullptr),
			bSucceeded ? TEXT("true") : TEXT("false"), *Result.Reason.ToString(), Result.RecalledSeconds);
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, !bSucceeded);
}
