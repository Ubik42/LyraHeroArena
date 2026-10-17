#include "Abilities/HeroArenaGameplayAbility_Blink.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "AbilitySystem/Abilities/LyraAbilitySimpleFailureMessage.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimMontage.h"
#include "Character/LyraCharacter.h"
#include "Components/HeroArenaBlinkChargeComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "LyraGameplayTags.h"
#include "UObject/ConstructorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaGameplayAbility_Blink)

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaBlink, Log, All);

UHeroArenaGameplayAbility_Blink::UHeroArenaGameplayAbility_Blink(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	ActivationPolicy = ELyraAbilityActivationPolicy::OnInputTriggered;
	ActivationGroup = ELyraAbilityActivationGroup::Exclusive_Replaceable;

	FGameplayTagContainer Tags;
	Tags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Ability.Type.Action.Blink")));
	SetAssetTags(Tags);

	ConstructorHelpers::FObjectFinder<UAnimMontage> ForwardDashMontage(
		TEXT("/Game/Characters/Heroes/Mannequin/Animations/Actions/AM_MM_Dash_Forward.AM_MM_Dash_Forward"));
	BlinkMontage = ForwardDashMontage.Object;
	FailureTagToUserFacingMessages.Add(LyraGameplayTags::Ability_ActivateFail_Cost,
		NSLOCTEXT("HeroArenaAbilityFeedback", "BlinkNoCharges", "闪现充能不足，请等待恢复"));
}

bool UHeroArenaGameplayAbility_Blink::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}
	const UHeroArenaBlinkChargeComponent* Charges = GetChargeComponent(ActorInfo);
	if (!Charges)
	{
		return false;
	}
	if (!Charges->HasAvailableCharge())
	{
		// Let GAS deliver failure feedback on the owning client; queries stay side-effect free.
		if (OptionalRelevantTags)
		{
			OptionalRelevantTags->AddTag(LyraGameplayTags::Ability_ActivateFail_Cost);
		}
		return false;
	}
	return true;
}

void UHeroArenaGameplayAbility_Blink::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	ALyraCharacter* Character = GetLyraCharacterFromActorInfo();
	UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
	UWorld* World = GetWorld();
	if (!Character || !Capsule || !World || !Character->GetCharacterMovement())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	const float FacingYaw = Character->GetController()
		? Character->GetController()->GetControlRotation().Yaw : Character->GetActorRotation().Yaw;
	const FVector Direction = FRotator(0.0f, FacingYaw, 0.0f).Vector().GetSafeNormal2D();
	const FVector Start = Character->GetActorLocation();
	const FVector DesiredEnd = Start + Direction * BlinkDistance;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(HeroArenaPhaseBlink), false, Character);
	FHitResult Hit;
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(
		Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight());
	const bool bBlocked = World->SweepSingleByChannel(
		Hit, Start, DesiredEnd, FQuat::Identity, ECC_Pawn, Shape, QueryParams);
	const FVector End = bBlocked
		? Hit.Location - Direction * FMath::Min(WallClearance, Hit.Distance)
		: DesiredEnd;
	const float TravelDistance = FVector::Dist2D(Start, End);

	UHeroArenaBlinkChargeComponent* Charges = GetChargeComponent(ActorInfo);
	if (Direction.IsNearlyZero() || TravelDistance < MinimumTravelDistance)
	{
		if (ActorInfo && ActorInfo->IsLocallyControlled())
		{
			FLyraAbilitySimpleFailureMessage Message;
			Message.PlayerController = Cast<APlayerController>(Character->GetController());
			Message.UserFacingReason = NSLOCTEXT(
				"HeroArenaAbilityFeedback", "BlinkNoRoom", "前方空间不足，无法闪现");
			UGameplayMessageSubsystem::Get(this).BroadcastMessage(
				FGameplayTag::RequestGameplayTag(TEXT("Ability.UserFacingSimpleActivateFail.Message")), Message);
		}
		if (Character->HasAuthority())
		{
			UE_LOG(LogHeroArenaBlink, Log,
				TEXT("Blink rejected: pawn=%s blocked=%s travel=%.2f start=%s desired=%s"),
				*GetNameSafe(Character), bBlocked ? TEXT("true") : TEXT("false"), TravelDistance,
				*Start.ToCompactString(), *DesiredEnd.ToCompactString());
		}
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo) || !Charges || !Charges->TryConsumeCharge())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (Character->bIsCrouched)
	{
		Character->UnCrouch();
	}
	Character->SetActorLocation(End, false, nullptr, ETeleportType::TeleportPhysics);
	Character->GetCharacterMovement()->StopMovementImmediately();

	if (IsValid(BlinkMontage))
	{
		UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this, TEXT("HeroArenaPhaseBlinkMontage"), BlinkMontage, 1.35f, NAME_None, false);
		MontageTask->ReadyForActivation();
	}

	FGameplayCueParameters CueParameters;
	CueParameters.Instigator = Character;
	CueParameters.EffectCauser = Character;
	CueParameters.Location = End;
	CueParameters.Normal = Direction;
	CueParameters.NormalizedMagnitude = 0.18f;
	CueParameters.RawMagnitude = BlinkDistance;
	GetAbilitySystemComponentFromActorInfo()->ExecuteGameplayCue(
		FGameplayTag::RequestGameplayTag(TEXT("GameplayCue.Character.Dash")), CueParameters);

	UE_LOG(LogHeroArenaBlink, Log,
		TEXT("Blink resolved: pawn=%s start=%s end=%s distance=%.2f blocked=%s authority=%s"),
		*GetNameSafe(Character), *Start.ToCompactString(), *Character->GetActorLocation().ToCompactString(),
		TravelDistance, bBlocked ? TEXT("true") : TEXT("false"),
		Character->HasAuthority() ? TEXT("true") : TEXT("false"));
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

UHeroArenaBlinkChargeComponent* UHeroArenaGameplayAbility_Blink::GetChargeComponent(
	const FGameplayAbilityActorInfo* ActorInfo) const
{
	return ActorInfo && ActorInfo->OwnerActor.IsValid()
		? ActorInfo->OwnerActor->FindComponentByClass<UHeroArenaBlinkChargeComponent>() : nullptr;
}
