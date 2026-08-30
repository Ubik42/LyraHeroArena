#include "Abilities/HeroArenaGameplayAbility_Charge.h"

#include "Abilities/Tasks/AbilityTask_ApplyRootMotionConstantForce.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimMontage.h"
#include "Character/LyraCharacter.h"
#include "Character/LyraHealthComponent.h"
#include "Effects/HeroArenaGameplayEffect_ChargeCooldown.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "System/LyraAssetManager.h"
#include "System/LyraGameData.h"
#include "LyraGameplayTags.h"
#include "Teams/LyraTeamSubsystem.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaGameplayAbility_Charge)

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaCharge, Log, All);

UHeroArenaGameplayAbility_Charge::UHeroArenaGameplayAbility_Charge(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	ActivationPolicy = ELyraAbilityActivationPolicy::OnInputTriggered;
	ActivationGroup = ELyraAbilityActivationGroup::Exclusive_Replaceable;
	CooldownGameplayEffectClass = UHeroArenaGameplayEffect_ChargeCooldown::StaticClass();

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Ability.Type.Action.Charge")));
	SetAssetTags(AssetTags);

	ConstructorHelpers::FObjectFinder<UAnimMontage> ForwardDashMontage(
		TEXT("/Game/Characters/Heroes/Mannequin/Animations/Actions/AM_MM_Dash_Forward.AM_MM_Dash_Forward"));
	ChargeMontage = ForwardDashMontage.Object;
}

void UHeroArenaGameplayAbility_Charge::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	ALyraCharacter* Character = GetLyraCharacterFromActorInfo();
	if (!IsValid(Character) || !IsValid(Character->GetCharacterMovement()) ||
		!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	const FRotator FacingRotation(0.0f,
		Character->GetController() ? Character->GetController()->GetControlRotation().Yaw : Character->GetActorRotation().Yaw,
		0.0f);
	ChargeDirection = FacingRotation.Vector().GetSafeNormal2D();
	if (ChargeDirection.IsNearlyZero())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (Character->bIsCrouched)
	{
		Character->UnCrouch();
	}

	ActiveRootMotionTask = UAbilityTask_ApplyRootMotionConstantForce::ApplyRootMotionConstantForce(
		this, TEXT("HeroArenaBulwarkCharge"), ChargeDirection, ChargeStrength, ChargeDuration,
		false, nullptr, ERootMotionFinishVelocityMode::SetVelocity, FVector::ZeroVector, 0.0f, true);
	ActiveRootMotionTask->OnFinish.AddDynamic(this, &ThisClass::HandleChargeFinished);
	ActiveRootMotionTask->ReadyForActivation();

	if (IsValid(ChargeMontage))
	{
		UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this, TEXT("HeroArenaBulwarkChargeMontage"), ChargeMontage, 1.0f, NAME_None, true);
		MontageTask->ReadyForActivation();
	}

	if (Character->HasAuthority())
	{
		FGameplayCueParameters CueParameters;
		CueParameters.Instigator = Character;
		CueParameters.EffectCauser = Character;
		CueParameters.Location = Character->GetActorLocation();
		CueParameters.Normal = ChargeDirection;
		// GCNL_Dash treats NormalizedMagnitude as its timeline duration and
		// RawMagnitude as effect strength. Supplying both is part of that cue's
		// actual Blueprint contract; a zero duration makes its play-rate divide by zero.
		CueParameters.NormalizedMagnitude = ChargeDuration;
		CueParameters.RawMagnitude = ChargeStrength;
		GetAbilitySystemComponentFromActorInfo()->ExecuteGameplayCue(
			FGameplayTag::RequestGameplayTag(TEXT("GameplayCue.Character.Dash")), CueParameters);

		PreviousServerLocation = Character->GetActorLocation();
		ServerChargeStartTime = GetWorld()->GetTimeSeconds();
		ConsecutiveStalledChecks = 0;
		GetWorld()->GetTimerManager().SetTimer(ServerContactTimer, this,
			&ThisClass::CheckServerChargeContact, 0.03f, true);
	}

	UE_LOG(LogHeroArenaCharge, Log, TEXT("Charge started for %s direction=%s authority=%s"),
		*GetNameSafe(Character), *ChargeDirection.ToCompactString(),
		Character->HasAuthority() ? TEXT("true") : TEXT("false"));
}

void UHeroArenaGameplayAbility_Charge::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ServerContactTimer);
	}

	if (IsValid(ActiveRootMotionTask))
	{
		ActiveRootMotionTask->EndTask();
		ActiveRootMotionTask = nullptr;
	}

	UE_LOG(LogHeroArenaCharge, Log, TEXT("Charge ended for %s cancelled=%s authority=%s"),
		*GetNameSafe(GetAvatarActorFromActorInfo()), bWasCancelled ? TEXT("true") : TEXT("false"),
		GetAvatarActorFromActorInfo() && GetAvatarActorFromActorInfo()->HasAuthority() ? TEXT("true") : TEXT("false"));

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UHeroArenaGameplayAbility_Charge::HandleChargeFinished()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UHeroArenaGameplayAbility_Charge::CheckServerChargeContact()
{
	ALyraCharacter* Character = GetLyraCharacterFromActorInfo();
	UWorld* World = GetWorld();
	if (!IsValid(Character) || !Character->HasAuthority() || !IsValid(World))
	{
		return;
	}

	const FVector CurrentLocation = Character->GetActorLocation();
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(HeroArenaBulwarkCharge), false, Character);
	TArray<FHitResult> Hits;
	World->SweepMultiByObjectType(Hits, PreviousServerLocation, CurrentLocation + ChargeDirection * ContactRadius,
		FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeSphere(ContactRadius), QueryParams);

	TSet<TObjectPtr<AActor>> CheckedActors;
	for (const FHitResult& Hit : Hits)
	{
		AActor* HitActor = Hit.GetActor();
		if (IsValid(HitActor) && !CheckedActors.Contains(HitActor))
		{
			CheckedActors.Add(HitActor);
			if (TryHitEnemy(HitActor, Hit))
			{
				EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
				return;
			}
		}
	}

	const double Elapsed = World->GetTimeSeconds() - ServerChargeStartTime;
	const float DistanceMoved2D = FVector::Dist2D(CurrentLocation, PreviousServerLocation);
	PreviousServerLocation = CurrentLocation;
	ConsecutiveStalledChecks = Elapsed > 0.12 && DistanceMoved2D < 3.0f ? ConsecutiveStalledChecks + 1 : 0;
	if (ConsecutiveStalledChecks >= 2)
	{
		UE_LOG(LogHeroArenaCharge, Log, TEXT("Charge stopped by world collision for %s after %.2fs"),
			*GetNameSafe(Character), Elapsed);
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
	}
}

bool UHeroArenaGameplayAbility_Charge::TryHitEnemy(AActor* TargetActor, const FHitResult& HitResult)
{
	AActor* SourceActor = GetAvatarActorFromActorInfo();
	const ULyraTeamSubsystem* TeamSubsystem = GetWorld()->GetSubsystem<ULyraTeamSubsystem>();
	if (!IsValid(SourceActor) || !IsValid(TargetActor) || !IsValid(TeamSubsystem) ||
		!TeamSubsystem->CanCauseDamage(SourceActor, TargetActor, false))
	{
		return false;
	}

	ULyraHealthComponent* HealthComponent = ULyraHealthComponent::FindHealthComponent(TargetActor);
	UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo();
	UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(TargetActor);
	if (!IsValid(HealthComponent) || HealthComponent->IsDeadOrDying() || !IsValid(SourceASC) || !IsValid(TargetASC))
	{
		return false;
	}

	const TSubclassOf<UGameplayEffect> DamageEffectClass =
		ULyraAssetManager::GetSubclass(ULyraGameData::Get().DamageGameplayEffect_SetByCaller);
	if (!DamageEffectClass)
	{
		return false;
	}

	FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();
	Context.AddSourceObject(this);
	Context.AddHitResult(HitResult);
	FGameplayEffectSpecHandle SpecHandle = SourceASC->MakeOutgoingSpec(DamageEffectClass, 1.0f, Context);
	if (!SpecHandle.IsValid())
	{
		return false;
	}

	const float OldHealth = HealthComponent->GetHealth();
	SpecHandle.Data->SetSetByCallerMagnitude(LyraGameplayTags::SetByCaller_Damage, Damage);
	TargetASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());

	if (ACharacter* TargetCharacter = Cast<ACharacter>(TargetActor))
	{
		TargetCharacter->LaunchCharacter(ChargeDirection * KnockbackStrength + FVector::UpVector * KnockbackLift,
			true, true);
	}

	UE_LOG(LogHeroArenaCharge, Log, TEXT("Charge hit %s: health %.0f -> %.0f knockback=%.0f authority=true"),
		*GetNameSafe(TargetActor), OldHealth, HealthComponent->GetHealth(), KnockbackStrength);
	return true;
}
