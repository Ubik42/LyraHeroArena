// Copyright Epic Games, Inc. All Rights Reserved.

#include "Abilities/HeroArenaGameplayAbility_RecoveryField.h"

#include "Actors/HeroArenaRecoveryField.h"
#include "Components/CapsuleComponent.h"
#include "Effects/HeroArenaGameplayEffect_RecoveryFieldCooldown.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaRecoveryAbility, Log, All);

UHeroArenaGameplayAbility_RecoveryField::UHeroArenaGameplayAbility_RecoveryField(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	ActivationPolicy = ELyraAbilityActivationPolicy::OnInputTriggered;
	CooldownGameplayEffectClass = UHeroArenaGameplayEffect_RecoveryFieldCooldown::StaticClass();
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Ability.Type.Action.RecoveryField")));
	SetAssetTags(AssetTags);
}

void UHeroArenaGameplayAbility_RecoveryField::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	AActor* AvatarActor = GetAvatarActorFromActorInfo();
	if (IsValid(AvatarActor) && AvatarActor->HasAuthority())
	{
		FVector FieldLocation = AvatarActor->GetActorLocation();
		if (const ACharacter* AvatarCharacter = Cast<ACharacter>(AvatarActor))
		{
			FieldLocation.Z -= AvatarCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		}

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = GetOwningActorFromActorInfo();
		SpawnParameters.Instigator = Cast<APawn>(AvatarActor);
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		AHeroArenaRecoveryField* Field = GetWorld()->SpawnActor<AHeroArenaRecoveryField>(
			AHeroArenaRecoveryField::StaticClass(), FieldLocation, FRotator::ZeroRotator, SpawnParameters);
		UE_LOG(LogHeroArenaRecoveryAbility, Log,
			TEXT("Recovery field ability committed for %s: field=%s authority=true"),
			*GetNameSafe(AvatarActor), *GetNameSafe(Field));
	}
	else
	{
		UE_LOG(LogHeroArenaRecoveryAbility, Log,
			TEXT("Recovery field predicted for %s authority=false"), *GetNameSafe(AvatarActor));
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
