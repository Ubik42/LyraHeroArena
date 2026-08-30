#include "Abilities/HeroArenaGameplayAbility_SelectHero.h"

#include "GameFramework/PlayerState.h"
#include "Hero/HeroArenaHeroSelectionComponent.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaGameplayAbility_SelectHero)

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaSelectHeroAbility, Log, All);

UHeroArenaGameplayAbility_SelectHero::UHeroArenaGameplayAbility_SelectHero(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	ActivationPolicy = ELyraAbilityActivationPolicy::OnInputTriggered;
}

void UHeroArenaGameplayAbility_SelectHero::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	AActor* OwnerActor = GetOwningActorFromActorInfo();
	if (IsValid(OwnerActor) && ActorInfo && ActorInfo->IsLocallyControlled())
	{
		if (UHeroArenaHeroSelectionComponent* Selection = OwnerActor->FindComponentByClass<UHeroArenaHeroSelectionComponent>())
		{
			const FName HeroId = RequestedHeroId;
			FTimerDelegate DeferredSelection;
			DeferredSelection.BindWeakLambda(Selection, [Selection, HeroId]()
			{
				Selection->RequestHeroSelection(HeroId);
				UE_LOG(LogHeroArenaSelectHeroAbility, Log, TEXT("Input hero selection '%s' requested by %s"),
					*HeroId.ToString(), *GetNameSafe(Selection->GetOwner()));
			});
			GetWorld()->GetTimerManager().SetTimerForNextTick(DeferredSelection);
		}
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

UHeroArenaGameplayAbility_SelectStriker::UHeroArenaGameplayAbility_SelectStriker(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	RequestedHeroId = TEXT("Striker");
}

UHeroArenaGameplayAbility_SelectBulwark::UHeroArenaGameplayAbility_SelectBulwark(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	RequestedHeroId = TEXT("Bulwark");
}
