#include "Abilities/HeroArenaGameplayAbility_Barrier.h"

#include "Actors/HeroArenaBarrier.h"
#include "Engine/World.h"
#include "Effects/HeroArenaGameplayEffect_BarrierCooldown.h"
#include "GameFramework/Pawn.h"
#include "Teams/LyraTeamSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaGameplayAbility_Barrier)

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaBarrierAbility, Log, All);

UHeroArenaGameplayAbility_Barrier::UHeroArenaGameplayAbility_Barrier(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	ActivationPolicy = ELyraAbilityActivationPolicy::OnInputTriggered;
	CooldownGameplayEffectClass = UHeroArenaGameplayEffect_BarrierCooldown::StaticClass();
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Ability.Type.Action.Barrier")));
	SetAssetTags(AssetTags);
}

void UHeroArenaGameplayAbility_Barrier::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
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
		const FRotator SpawnRotation(0.0f, AvatarActor->GetActorRotation().Yaw, 0.0f);
		const FVector SpawnLocation = AvatarActor->GetActorLocation() + SpawnRotation.Vector() * 180.0f + FVector(0.0f, 0.0f, 65.0f);

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = GetOwningActorFromActorInfo();
		SpawnParameters.Instigator = Cast<APawn>(AvatarActor);
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		AHeroArenaBarrier* Barrier = GetWorld()->SpawnActorDeferred<AHeroArenaBarrier>(
			AHeroArenaBarrier::StaticClass(), FTransform(SpawnRotation, SpawnLocation), GetOwningActorFromActorInfo(),
			Cast<APawn>(AvatarActor), ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Barrier)
		{
			if (const ULyraTeamSubsystem* TeamSubsystem = GetWorld()->GetSubsystem<ULyraTeamSubsystem>())
			{
				Barrier->SetGenericTeamId(IntegerToGenericTeamId(TeamSubsystem->FindTeamFromObject(AvatarActor)));
			}
			Barrier->FinishSpawning(FTransform(SpawnRotation, SpawnLocation));
		}

		UE_LOG(LogHeroArenaBarrierAbility, Log, TEXT("Barrier ability committed for %s: barrier=%s authority=true"),
			*GetNameSafe(AvatarActor), *GetNameSafe(Barrier));
	}
	else
	{
		UE_LOG(LogHeroArenaBarrierAbility, Log, TEXT("Barrier ability predicted for %s authority=false"), *GetNameSafe(AvatarActor));
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
