#include "Abilities/HeroArenaGameplayAbility_Sprint.h"

#include "Abilities/Tasks/AbilityTask_WaitInputRelease.h"
#include "Character/LyraCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaGameplayAbility_Sprint)

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaSprint, Log, All);

UHeroArenaGameplayAbility_Sprint::UHeroArenaGameplayAbility_Sprint(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	ActivationPolicy = ELyraAbilityActivationPolicy::WhileInputActive;
	ActivationGroup = ELyraAbilityActivationGroup::Independent;
}

void UHeroArenaGameplayAbility_Sprint::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	ALyraCharacter* Character = GetLyraCharacterFromActorInfo();
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Movement || !CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ActiveMovementComponent = Movement;
	OriginalMaxWalkSpeed = Movement->MaxWalkSpeed;
	Movement->MaxWalkSpeed = OriginalMaxWalkSpeed * SprintSpeedMultiplier;
	bAppliedSprintSpeed = true;

	UE_LOG(LogHeroArenaSprint, Log, TEXT("Sprint started for %s: %.0f -> %.0f (authority=%s)"),
		*GetNameSafe(Character), OriginalMaxWalkSpeed, Movement->MaxWalkSpeed,
		Character->HasAuthority() ? TEXT("true") : TEXT("false"));

	// Lyra activates WhileInputActive abilities in the same input-processing pass.
	// Testing the pre-activation state here can see a stale "released" value and
	// immediately end/retrigger every frame, so wait only for the next release event.
	UAbilityTask_WaitInputRelease* WaitRelease = UAbilityTask_WaitInputRelease::WaitInputRelease(this, false);
	WaitRelease->OnRelease.AddDynamic(this, &ThisClass::HandleInputReleased);
	WaitRelease->ReadyForActivation();
}

void UHeroArenaGameplayAbility_Sprint::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	if (bAppliedSprintSpeed && ActiveMovementComponent)
	{
		ActiveMovementComponent->MaxWalkSpeed = OriginalMaxWalkSpeed;
		UE_LOG(LogHeroArenaSprint, Log, TEXT("Sprint ended for %s: restored %.0f (cancelled=%s)"),
			*GetNameSafe(GetAvatarActorFromActorInfo()), OriginalMaxWalkSpeed,
			bWasCancelled ? TEXT("true") : TEXT("false"));
	}

	bAppliedSprintSpeed = false;
	OriginalMaxWalkSpeed = 0.0f;
	ActiveMovementComponent = nullptr;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UHeroArenaGameplayAbility_Sprint::HandleInputReleased(float TimeHeld)
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}
