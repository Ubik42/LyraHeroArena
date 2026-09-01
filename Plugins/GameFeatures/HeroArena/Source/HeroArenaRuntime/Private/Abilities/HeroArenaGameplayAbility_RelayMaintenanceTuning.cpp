#include "Abilities/HeroArenaGameplayAbility_RelayMaintenanceTuning.h"

#include "AbilitySystemComponent.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaGameplayAbility_RelayMaintenanceTuning)

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaRelayWeapon, Log, All);

UHeroArenaGameplayAbility_RelayMaintenanceTuning::UHeroArenaGameplayAbility_RelayMaintenanceTuning(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	ActivationPolicy = ELyraAbilityActivationPolicy::OnSpawn;

	static ConstructorHelpers::FClassFinder<UGameplayAbility> MaintenanceFire(
		TEXT("/HeroArena/Heroes/Pulse/Weapons/GA_Pulse_BioticPistol"));
	if (MaintenanceFire.Succeeded())
	{
		MaintenanceFireAbilityClass = MaintenanceFire.Class;
	}
}

void UHeroArenaGameplayAbility_RelayMaintenanceTuning::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	RefreshWeaponAbility();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			RefreshTimerHandle, this, &ThisClass::RefreshWeaponAbility, RefreshInterval, true);
	}
}

void UHeroArenaGameplayAbility_RelayMaintenanceTuning::EndAbility(
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
	RemoveReplacementAbility();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UHeroArenaGameplayAbility_RelayMaintenanceTuning::RefreshWeaponAbility()
{
	UAbilitySystemComponent* AbilitySystem = GetAbilitySystemComponentFromActorInfo();
	if (!IsValid(AbilitySystem) || !AbilitySystem->IsOwnerActorAuthoritative()
		|| !MaintenanceFireAbilityClass)
	{
		return;
	}

	if (ReplacementAbilityHandle.IsValid())
	{
		const FGameplayAbilitySpec* ReplacementSpec = AbilitySystem->FindAbilitySpecFromHandle(ReplacementAbilityHandle);
		if (ReplacementSpec && ReplacementSourceObject.IsValid())
		{
			return;
		}
		RemoveReplacementAbility();
	}

	FGameplayAbilitySpecHandle OriginalHandle;
	FGameplayTagContainer InputTags;
	TWeakObjectPtr<UObject> SourceObject;
	int32 AbilityLevel = 1;
	int32 InputId = INDEX_NONE;
	for (const FGameplayAbilitySpec& Spec : AbilitySystem->GetActivatableAbilities())
	{
		if (!Spec.Ability || Spec.IsActive()
			|| !Spec.Ability->GetClass()->GetPathName().Contains(TEXT("GA_Weapon_Fire_Pistol")))
		{
			continue;
		}

		OriginalHandle = Spec.Handle;
		InputTags = Spec.GetDynamicSpecSourceTags();
		SourceObject = Spec.SourceObject;
		AbilityLevel = Spec.Level;
		InputId = Spec.InputID;
		break;
	}

	if (!OriginalHandle.IsValid() || !SourceObject.IsValid())
	{
		return;
	}

	AbilitySystem->ClearAbility(OriginalHandle);
	FGameplayAbilitySpec ReplacementSpec(
		MaintenanceFireAbilityClass, AbilityLevel, InputId, SourceObject.Get());
	ReplacementSpec.GetDynamicSpecSourceTags() = InputTags;
	ReplacementAbilityHandle = AbilitySystem->GiveAbility(ReplacementSpec);
	ReplacementSourceObject = SourceObject;

	UE_LOG(LogHeroArenaRelayWeapon, Log,
		TEXT("Relay maintenance pistol installed: avatar=%s source=%s input_tags=%s handle=%s"),
		*GetNameSafe(GetAvatarActorFromActorInfo()), *GetNameSafe(SourceObject.Get()),
		*InputTags.ToStringSimple(), *ReplacementAbilityHandle.ToString());
}

void UHeroArenaGameplayAbility_RelayMaintenanceTuning::RemoveReplacementAbility()
{
	if (ReplacementAbilityHandle.IsValid())
	{
		if (UAbilitySystemComponent* AbilitySystem = GetAbilitySystemComponentFromActorInfo())
		{
			if (AbilitySystem->IsOwnerActorAuthoritative())
			{
				AbilitySystem->ClearAbility(ReplacementAbilityHandle);
			}
		}
	}
	ReplacementAbilityHandle = FGameplayAbilitySpecHandle();
	ReplacementSourceObject.Reset();
}
