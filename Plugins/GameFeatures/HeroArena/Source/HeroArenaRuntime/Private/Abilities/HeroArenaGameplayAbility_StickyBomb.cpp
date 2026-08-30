#include "Abilities/HeroArenaGameplayAbility_StickyBomb.h"

#include "AbilitySystemComponent.h"
#include "Actors/HeroArenaStickyBomb.h"
#include "Character/LyraCharacter.h"
#include "Effects/HeroArenaGameplayEffect_StickyBomb.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerState.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaGameplayAbility_StickyBomb)

UHeroArenaGameplayAbility_StickyBomb::UHeroArenaGameplayAbility_StickyBomb(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	ActivationPolicy = ELyraAbilityActivationPolicy::OnInputTriggered;
	CooldownGameplayEffectClass = UHeroArenaGameplayEffect_StickyBombCooldown::StaticClass();

	FGameplayTagContainer Tags;
	Tags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Ability.Type.Action.StickyBomb")));
	SetAssetTags(Tags);
}

void UHeroArenaGameplayAbility_StickyBomb::ActivateAbility(
	FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	ALyraCharacter* Character = GetLyraCharacterFromActorInfo();
	if (!Character || !CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (Character->HasAuthority())
	{
		AController* Controller = Character->GetController();
		const FRotator Aim = Controller ? Controller->GetControlRotation() : Character->GetActorRotation();
		const FVector Forward = Aim.Vector();
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = Character;
		SpawnParameters.Instigator = Character;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AHeroArenaStickyBomb* Bomb = GetWorld()->SpawnActor<AHeroArenaStickyBomb>(
			AHeroArenaStickyBomb::StaticClass(),
			Character->GetActorLocation() + Forward * 105.0f + FVector::UpVector * 58.0f,
			Aim, SpawnParameters);
		if (Bomb)
		{
			AActor* TeamAgent = Character->GetPlayerState() ? Cast<AActor>(Character->GetPlayerState()) : Character;
			Bomb->InitializeBomb(GetAbilitySystemComponentFromActorInfo(), TeamAgent,
				Forward * 2200.0f + FVector::UpVector * 110.0f);
		}
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
