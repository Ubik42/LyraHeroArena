#include "Abilities/HeroArenaGameplayAbility_Ultimate.h"

#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Hero/HeroArenaHeroSelectionComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaGameplayAbility_Ultimate)

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaUltimateAbility, Log, All);

namespace HeroArenaUltimateAbility
{
	APlayerState* ResolvePlayerState(const FGameplayAbilityActorInfo* ActorInfo)
	{
		if (!ActorInfo)
		{
			return nullptr;
		}
		if (APlayerState* PlayerState = Cast<APlayerState>(ActorInfo->OwnerActor.Get()))
		{
			return PlayerState;
		}
		const APawn* Pawn = Cast<APawn>(ActorInfo->AvatarActor.Get());
		return Pawn ? Pawn->GetPlayerState() : nullptr;
	}
}

bool UHeroArenaGameplayAbility_Ultimate::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	const APlayerState* PlayerState = HeroArenaUltimateAbility::ResolvePlayerState(ActorInfo);
	const UHeroArenaHeroSelectionComponent* Selection = PlayerState
		? PlayerState->FindComponentByClass<UHeroArenaHeroSelectionComponent>() : nullptr;
	const bool bChargeReady = Selection && Selection->IsUltimateReady();
	const bool bBaseCanActivate = Super::CanActivateAbility(
		Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags);
	if (bChargeReady)
	{
		UE_LOG(LogHeroArenaUltimateAbility, VeryVerbose,
			TEXT("Ultimate activation check: player=%s ability=%s gas_ready=%s"),
			*GetNameSafe(PlayerState), *GetName(), bBaseCanActivate ? TEXT("true") : TEXT("false"));
	}
	return bChargeReady && bBaseCanActivate;
}

bool UHeroArenaGameplayAbility_Ultimate::CommitUltimateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo)
{
	APlayerState* PlayerState = HeroArenaUltimateAbility::ResolvePlayerState(ActorInfo);
	UHeroArenaHeroSelectionComponent* Selection = PlayerState
		? PlayerState->FindComponentByClass<UHeroArenaHeroSelectionComponent>() : nullptr;
	if (!Selection || !Selection->IsUltimateReady() || !CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		return false;
	}
	return !PlayerState->HasAuthority() || Selection->TryConsumeUltimateCharge();
}
