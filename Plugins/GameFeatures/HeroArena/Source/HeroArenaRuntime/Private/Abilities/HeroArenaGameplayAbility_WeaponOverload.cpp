// Copyright Epic Games, Inc. All Rights Reserved.

#include "Abilities/HeroArenaGameplayAbility_WeaponOverload.h"

#include "AbilitySystemComponent.h"
#include "Actors/HeroArenaWeaponOverloadIndicator.h"
#include "Effects/HeroArenaGameplayEffect_WeaponOverloadCooldown.h"
#include "Equipment/LyraEquipmentManagerComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"
#include "Weapons/LyraRangedWeaponInstance.h"

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaWeaponOverload, Log, All);

UHeroArenaGameplayAbility_WeaponOverload::UHeroArenaGameplayAbility_WeaponOverload(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	ActivationPolicy = ELyraAbilityActivationPolicy::OnInputTriggered;
	CooldownGameplayEffectClass = UHeroArenaGameplayEffect_WeaponOverloadCooldown::StaticClass();

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Ability.Type.Action.WeaponOverload")));
	SetAssetTags(AssetTags);
	ActivationOwnedTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Status.HeroArena.WeaponOverload")));
}

void UHeroArenaGameplayAbility_WeaponOverload::ActivateAbility(
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

	RefreshWeaponOverrides();
	UE_LOG(LogHeroArenaWeaponOverload, Log,
		TEXT("Weapon overload binding snapshot: overrides=%d ranged_weapon=%s authority=%s"),
		FloatOverrides.Num(), *GetNameSafe(OverloadedWeapon.Get()),
		GetAvatarActorFromActorInfo() && GetAvatarActorFromActorInfo()->HasAuthority() ? TEXT("true") : TEXT("false"));
	GetWorld()->GetTimerManager().SetTimer(
		RefreshTimerHandle, this, &ThisClass::RefreshWeaponOverrides, 0.25f, true);
	GetWorld()->GetTimerManager().SetTimer(
		DurationTimerHandle, this, &ThisClass::FinishOverload, OverloadDuration, false);

	AActor* AvatarActor = GetAvatarActorFromActorInfo();
	if (IsValid(AvatarActor) && AvatarActor->HasAuthority())
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = GetOwningActorFromActorInfo();
		SpawnParameters.Instigator = Cast<APawn>(AvatarActor);
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AHeroArenaWeaponOverloadIndicator* SpawnedIndicator = GetWorld()->SpawnActor<AHeroArenaWeaponOverloadIndicator>(
			AHeroArenaWeaponOverloadIndicator::StaticClass(), AvatarActor->GetActorLocation(), FRotator::ZeroRotator, SpawnParameters);
		if (IsValid(SpawnedIndicator))
		{
			SpawnedIndicator->InitializeIndicator(AvatarActor, OverloadDuration);
			Indicator = SpawnedIndicator;
		}
	}

	UE_LOG(LogHeroArenaWeaponOverload, Log,
		TEXT("Weapon overload activated: avatar=%s duration=%.1f interval=%.2fx spread=%.2fx authority=%s"),
		*GetNameSafe(AvatarActor), OverloadDuration, FireIntervalMultiplier, SpreadMultiplier,
		AvatarActor && AvatarActor->HasAuthority() ? TEXT("true") : TEXT("false"));
}

void UHeroArenaGameplayAbility_WeaponOverload::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const bool bReplicateEndAbility,
	const bool bWasCancelled)
{
	GetWorld()->GetTimerManager().ClearTimer(DurationTimerHandle);
	GetWorld()->GetTimerManager().ClearTimer(RefreshTimerHandle);
	RestoreWeaponOverrides();

	if (AHeroArenaWeaponOverloadIndicator* ActiveIndicator = Indicator.Get();
		IsValid(ActiveIndicator) && ActiveIndicator->HasAuthority())
	{
		ActiveIndicator->Destroy();
	}
	Indicator.Reset();

	UE_LOG(LogHeroArenaWeaponOverload, Log,
		TEXT("Weapon overload ended: avatar=%s cancelled=%s authority=%s"),
		*GetNameSafe(GetAvatarActorFromActorInfo()), bWasCancelled ? TEXT("true") : TEXT("false"),
		GetAvatarActorFromActorInfo() && GetAvatarActorFromActorInfo()->HasAuthority() ? TEXT("true") : TEXT("false"));

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UHeroArenaGameplayAbility_WeaponOverload::RefreshWeaponOverrides()
{
	UAbilitySystemComponent* AbilitySystem = GetAbilitySystemComponentFromActorInfo();
	if (IsValid(AbilitySystem))
	{
		for (const FGameplayAbilitySpec& Spec : AbilitySystem->GetActivatableAbilities())
		{
			if (!IsValid(Spec.Ability))
			{
				continue;
			}

			for (UGameplayAbility* AbilityInstance : Spec.GetAbilityInstances())
			{
				// Lyra 5.7's Rifle Blueprint still serializes the legacy AbilityTags field,
				// so identify the runtime fire instance by its authored fire properties.
				ApplyFloatMultiplier(AbilityInstance, TEXT("FireDelayTimeSecs"), FireIntervalMultiplier);
				ApplyFloatMultiplier(AbilityInstance, TEXT("AutoRate"), 1.0 / FireIntervalMultiplier);
			}
		}
	}

	AActor* AvatarActor = GetAvatarActorFromActorInfo();
	ULyraEquipmentManagerComponent* EquipmentManager =
		IsValid(AvatarActor) ? AvatarActor->FindComponentByClass<ULyraEquipmentManagerComponent>() : nullptr;
	ULyraRangedWeaponInstance* CurrentWeapon = IsValid(EquipmentManager)
		? Cast<ULyraRangedWeaponInstance>(EquipmentManager->GetFirstInstanceOfType(ULyraRangedWeaponInstance::StaticClass()))
		: nullptr;

	const FGameplayTag OverloadTag = FGameplayTag::RequestGameplayTag(TEXT("Status.HeroArena.WeaponOverload"));
	if (OverloadedWeapon.Get() != CurrentWeapon)
	{
		if (ULyraRangedWeaponInstance* PreviousWeapon = OverloadedWeapon.Get())
		{
			PreviousWeapon->ClearExternalSpreadAngleMultiplier(OverloadTag);
		}
		OverloadedWeapon = CurrentWeapon;
	}
	if (IsValid(CurrentWeapon))
	{
		CurrentWeapon->SetExternalSpreadAngleMultiplier(OverloadTag, SpreadMultiplier);
	}
}

void UHeroArenaGameplayAbility_WeaponOverload::FinishOverload()
{
	EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, false);
}

void UHeroArenaGameplayAbility_WeaponOverload::RestoreWeaponOverrides()
{
	for (const FFloatOverride& Override : FloatOverrides)
	{
		UObject* Object = Override.Object.Get();
		FProperty* Property = IsValid(Object) ? Object->GetClass()->FindPropertyByName(Override.PropertyName) : nullptr;
		FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property);
		if (IsValid(Object) && NumericProperty && NumericProperty->IsFloatingPoint())
		{
			void* ValuePtr = NumericProperty->ContainerPtrToValuePtr<void>(Object);
			const double OverriddenValue = NumericProperty->GetFloatingPointPropertyValue(ValuePtr);
			NumericProperty->SetFloatingPointPropertyValue(ValuePtr, Override.OriginalValue);
			UE_LOG(LogHeroArenaWeaponOverload, Log,
				TEXT("Weapon overload restored: object=%s property=%s %.3f -> %.3f authority=%s"),
				*GetNameSafe(Object), *Override.PropertyName.ToString(), OverriddenValue, Override.OriginalValue,
				GetAvatarActorFromActorInfo() && GetAvatarActorFromActorInfo()->HasAuthority() ? TEXT("true") : TEXT("false"));
		}
	}
	FloatOverrides.Reset();

	if (ULyraRangedWeaponInstance* Weapon = OverloadedWeapon.Get())
	{
		Weapon->ClearExternalSpreadAngleMultiplier(
			FGameplayTag::RequestGameplayTag(TEXT("Status.HeroArena.WeaponOverload")));
	}
	OverloadedWeapon.Reset();
}

bool UHeroArenaGameplayAbility_WeaponOverload::ApplyFloatMultiplier(
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

	FNumericProperty* NumericProperty = CastField<FNumericProperty>(Object->GetClass()->FindPropertyByName(PropertyName));
	if (!NumericProperty || !NumericProperty->IsFloatingPoint())
	{
		return false;
	}

	void* ValuePtr = NumericProperty->ContainerPtrToValuePtr<void>(Object);
	const double OriginalValue = NumericProperty->GetFloatingPointPropertyValue(ValuePtr);
	NumericProperty->SetFloatingPointPropertyValue(ValuePtr, OriginalValue * Multiplier);
	FloatOverrides.Add({Object, PropertyName, OriginalValue});

	UE_LOG(LogHeroArenaWeaponOverload, Log,
		TEXT("Weapon overload override: object=%s property=%s %.3f -> %.3f authority=%s"),
		*GetNameSafe(Object), *PropertyName.ToString(), OriginalValue, OriginalValue * Multiplier,
		GetAvatarActorFromActorInfo() && GetAvatarActorFromActorInfo()->HasAuthority() ? TEXT("true") : TEXT("false"));
	return true;
}
