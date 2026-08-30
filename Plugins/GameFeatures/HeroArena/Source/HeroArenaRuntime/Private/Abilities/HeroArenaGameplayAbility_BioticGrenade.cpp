#include "Abilities/HeroArenaGameplayAbility_BioticGrenade.h"

#include "Actors/HeroArenaBioticGrenade.h"
#include "Character/LyraCharacter.h"
#include "Effects/HeroArenaGameplayEffect_BioticGrenade.h"
#include "GameFramework/Controller.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaGameplayAbility_BioticGrenade)

UHeroArenaGameplayAbility_BioticGrenade::UHeroArenaGameplayAbility_BioticGrenade(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	ActivationPolicy = ELyraAbilityActivationPolicy::OnInputTriggered;
	CooldownGameplayEffectClass = UHeroArenaGameplayEffect_BioticGrenadeCooldown::StaticClass();
	FGameplayTagContainer Tags;
	Tags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Ability.Type.Action.BioticGrenade")));
	SetAssetTags(Tags);
}

void UHeroArenaGameplayAbility_BioticGrenade::ActivateAbility(FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
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
		const FRotator Aim = Character->GetController() ? Character->GetController()->GetControlRotation() : Character->GetActorRotation();
		const FVector Forward = Aim.Vector();
		FActorSpawnParameters Params;
		Params.Owner = Character;
		Params.Instigator = Character;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AHeroArenaBioticGrenade* Grenade = GetWorld()->SpawnActor<AHeroArenaBioticGrenade>(AHeroArenaBioticGrenade::StaticClass(),
			Character->GetActorLocation() + Forward * 100.0f + FVector::UpVector * 60.0f, Aim, Params);
		if (Grenade) Grenade->InitializeVelocity(Forward * 1800.0f + FVector::UpVector * 180.0f);
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
