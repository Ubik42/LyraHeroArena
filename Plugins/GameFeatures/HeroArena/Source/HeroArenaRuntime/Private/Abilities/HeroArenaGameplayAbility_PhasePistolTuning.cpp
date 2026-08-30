// Copyright Epic Games, Inc. All Rights Reserved.

#include "Abilities/HeroArenaGameplayAbility_PhasePistolTuning.h"

#include "AbilitySystemComponent.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaGameplayAbility_PhasePistolTuning)

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaPhasePistol, Log, All);

UHeroArenaGameplayAbility_PhasePistolTuning::UHeroArenaGameplayAbility_PhasePistolTuning(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	ActivationPolicy = ELyraAbilityActivationPolicy::OnSpawn;
}

void UHeroArenaGameplayAbility_PhasePistolTuning::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	RefreshPistolOverride();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			RefreshTimerHandle, this, &ThisClass::RefreshPistolOverride, RefreshInterval, true);
	}

	UE_LOG(LogHeroArenaPhasePistol, Log,
		TEXT("Phase pistol tuning activated: avatar=%s interval_multiplier=%.2f overrides=%d authority=%s"),
		*GetNameSafe(GetAvatarActorFromActorInfo()), FireIntervalMultiplier, FloatOverrides.Num(),
		GetAvatarActorFromActorInfo() && GetAvatarActorFromActorInfo()->HasAuthority() ? TEXT("true") : TEXT("false"));
}

void UHeroArenaGameplayAbility_PhasePistolTuning::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const bool bReplicateEndAbility,
	const bool bWasCancelled)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshTimerHandle);
	}
	RestorePistolOverride();

	UE_LOG(LogHeroArenaPhasePistol, Log,
		TEXT("Phase pistol tuning ended: avatar=%s cancelled=%s authority=%s"),
		*GetNameSafe(GetAvatarActorFromActorInfo()), bWasCancelled ? TEXT("true") : TEXT("false"),
		GetAvatarActorFromActorInfo() && GetAvatarActorFromActorInfo()->HasAuthority() ? TEXT("true") : TEXT("false"));

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UHeroArenaGameplayAbility_PhasePistolTuning::RefreshPistolOverride()
{
	UAbilitySystemComponent* AbilitySystem = GetAbilitySystemComponentFromActorInfo();
	if (!IsValid(AbilitySystem))
	{
		return;
	}

	FloatOverrides.RemoveAll([](const FFloatOverride& Override)
	{
		return !Override.Object.IsValid();
	});

	for (const FGameplayAbilitySpec& Spec : AbilitySystem->GetActivatableAbilities())
	{
		for (UGameplayAbility* AbilityInstance : Spec.GetAbilityInstances())
		{
			if (IsPistolFireAbility(AbilityInstance))
			{
				ApplyFloatMultiplier(AbilityInstance, TEXT("FireDelayTimeSecs"), FireIntervalMultiplier);
			}
		}
	}
}

void UHeroArenaGameplayAbility_PhasePistolTuning::RestorePistolOverride()
{
	for (const FFloatOverride& Override : FloatOverrides)
	{
		UObject* Object = Override.Object.Get();
		FNumericProperty* Property = IsValid(Object)
			? CastField<FNumericProperty>(Object->GetClass()->FindPropertyByName(Override.PropertyName)) : nullptr;
		if (!Property || !Property->IsFloatingPoint())
		{
			continue;
		}

		void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);
		const double CurrentValue = Property->GetFloatingPointPropertyValue(ValuePtr);
		if (FMath::IsNearlyEqual(CurrentValue, Override.AppliedValue, 0.0001))
		{
			Property->SetFloatingPointPropertyValue(ValuePtr, Override.OriginalValue);
			UE_LOG(LogHeroArenaPhasePistol, Log,
				TEXT("Phase pistol tuning restored: object=%s property=%s %.3f -> %.3f authority=%s"),
				*GetNameSafe(Object), *Override.PropertyName.ToString(), CurrentValue, Override.OriginalValue,
				GetAvatarActorFromActorInfo() && GetAvatarActorFromActorInfo()->HasAuthority() ? TEXT("true") : TEXT("false"));
		}
		else
		{
			UE_LOG(LogHeroArenaPhasePistol, Verbose,
				TEXT("Phase pistol restore skipped because another system changed the value: object=%s property=%s expected=%.3f current=%.3f"),
				*GetNameSafe(Object), *Override.PropertyName.ToString(), Override.AppliedValue, CurrentValue);
		}
	}
	FloatOverrides.Reset();
}

bool UHeroArenaGameplayAbility_PhasePistolTuning::ApplyFloatMultiplier(
	UObject* Object, const FName PropertyName, const double Multiplier)
{
	if (!IsValid(Object) || FloatOverrides.ContainsByPredicate(
		[Object, PropertyName](const FFloatOverride& Existing)
		{
			return Existing.Object == Object && Existing.PropertyName == PropertyName;
		}))
	{
		return false;
	}

	FNumericProperty* Property = CastField<FNumericProperty>(Object->GetClass()->FindPropertyByName(PropertyName));
	if (!Property || !Property->IsFloatingPoint())
	{
		return false;
	}

	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);
	const double OriginalValue = Property->GetFloatingPointPropertyValue(ValuePtr);
	const double AppliedValue = OriginalValue * Multiplier;
	Property->SetFloatingPointPropertyValue(ValuePtr, AppliedValue);
	FloatOverrides.Add({Object, PropertyName, OriginalValue, AppliedValue});

	UE_LOG(LogHeroArenaPhasePistol, Log,
		TEXT("Phase pistol tuning applied: object=%s class=%s property=%s %.3f -> %.3f authority=%s"),
		*GetNameSafe(Object), *GetNameSafe(Object->GetClass()), *PropertyName.ToString(), OriginalValue, AppliedValue,
		GetAvatarActorFromActorInfo() && GetAvatarActorFromActorInfo()->HasAuthority() ? TEXT("true") : TEXT("false"));
	return true;
}

bool UHeroArenaGameplayAbility_PhasePistolTuning::IsPistolFireAbility(const UGameplayAbility* Ability) const
{
	return IsValid(Ability) && Ability->GetClass()->GetPathName().Contains(TEXT("GA_Weapon_Fire_Pistol"));
}
