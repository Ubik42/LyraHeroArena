#include "Input/HeroArenaStrikerInputConfig.h"

#include "InputAction.h"
#include "UObject/ConstructorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaStrikerInputConfig)

UHeroArenaStrikerInputConfig::UHeroArenaStrikerInputConfig(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ConstructorHelpers::FObjectFinder<UInputAction> SprintAction(
		TEXT("/HeroArena/Input/IA_Striker_Sprint.IA_Striker_Sprint"));
	if (SprintAction.Succeeded())
	{
		FLyraInputAction& Binding = AbilityInputActions.AddDefaulted_GetRef();
		Binding.InputAction = SprintAction.Object;
		Binding.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Ability.Sprint"));
	}

	ConstructorHelpers::FObjectFinder<UInputAction> SpiralGrenadeAction(
		TEXT("/HeroArena/Input/IA_Striker_SpiralGrenade.IA_Striker_SpiralGrenade"));
	if (SpiralGrenadeAction.Succeeded())
	{
		FLyraInputAction& Binding = AbilityInputActions.AddDefaulted_GetRef();
		Binding.InputAction = SpiralGrenadeAction.Object;
		Binding.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Ability.SpiralGrenade"));

		FLyraInputAction& PulseBinding = AbilityInputActions.AddDefaulted_GetRef();
		PulseBinding.InputAction = SpiralGrenadeAction.Object;
		PulseBinding.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Ability.BioticGrenade"));
	}

	ConstructorHelpers::FObjectFinder<UInputAction> RecoveryFieldAction(
		TEXT("/HeroArena/Input/IA_Striker_RecoveryField.IA_Striker_RecoveryField"));
	if (RecoveryFieldAction.Succeeded())
	{
		FLyraInputAction& Binding = AbilityInputActions.AddDefaulted_GetRef();
		Binding.InputAction = RecoveryFieldAction.Object;
		Binding.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Ability.RecoveryField"));
	}

	ConstructorHelpers::FObjectFinder<UInputAction> WeaponOverloadAction(
		TEXT("/HeroArena/Input/IA_Striker_WeaponOverload.IA_Striker_WeaponOverload"));
	if (WeaponOverloadAction.Succeeded())
	{
		FLyraInputAction& Binding = AbilityInputActions.AddDefaulted_GetRef();
		Binding.InputAction = WeaponOverloadAction.Object;
		Binding.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Ability.WeaponOverload"));

		FLyraInputAction& RelayBinding = AbilityInputActions.AddDefaulted_GetRef();
		RelayBinding.InputAction = WeaponOverloadAction.Object;
		RelayBinding.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Ability.RelayTeamAmplifier"));
	}

	ConstructorHelpers::FObjectFinder<UInputAction> PurificationNeedleAction(
		TEXT("/HeroArena/Input/IA_Pulse_PurificationNeedle.IA_Pulse_PurificationNeedle"));
	if (PurificationNeedleAction.Succeeded())
	{
		FLyraInputAction& Binding = AbilityInputActions.AddDefaulted_GetRef();
		Binding.InputAction = PurificationNeedleAction.Object;
		Binding.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Ability.PurificationNeedle"));

		FLyraInputAction& RelayBinding = AbilityInputActions.AddDefaulted_GetRef();
		RelayBinding.InputAction = PurificationNeedleAction.Object;
		RelayBinding.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Ability.RelayQuickMaintenance"));
	}

	ConstructorHelpers::FObjectFinder<UInputAction> OverclockInjectionAction(
		TEXT("/HeroArena/Input/IA_Pulse_OverclockInjection.IA_Pulse_OverclockInjection"));
	if (OverclockInjectionAction.Succeeded())
	{
		FLyraInputAction& Binding = AbilityInputActions.AddDefaulted_GetRef();
		Binding.InputAction = OverclockInjectionAction.Object;
		Binding.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Ability.OverclockInjection"));
	}

	ConstructorHelpers::FObjectFinder<UInputAction> BlinkAction(
		TEXT("/HeroArena/Input/IA_Phase_Blink.IA_Phase_Blink"));
	if (BlinkAction.Succeeded())
	{
		FLyraInputAction& Binding = AbilityInputActions.AddDefaulted_GetRef();
		Binding.InputAction = BlinkAction.Object;
		Binding.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Ability.Blink"));
	}

	ConstructorHelpers::FObjectFinder<UInputAction> RecallAction(
		TEXT("/HeroArena/Input/IA_Phase_Recall.IA_Phase_Recall"));
	if (RecallAction.Succeeded())
	{
		FLyraInputAction& Binding = AbilityInputActions.AddDefaulted_GetRef();
		Binding.InputAction = RecallAction.Object;
		Binding.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Ability.Recall"));
	}

	ConstructorHelpers::FObjectFinder<UInputAction> StickyBombAction(
		TEXT("/HeroArena/Input/IA_Phase_StickyBomb.IA_Phase_StickyBomb"));
	if (StickyBombAction.Succeeded())
	{
		FLyraInputAction& Binding = AbilityInputActions.AddDefaulted_GetRef();
		Binding.InputAction = StickyBombAction.Object;
		Binding.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Ability.StickyBomb"));
	}

	ConstructorHelpers::FObjectFinder<UInputAction> RelayBeaconAction(
		TEXT("/HeroArena/Input/IA_Relay_Beacon.IA_Relay_Beacon"));
	if (RelayBeaconAction.Succeeded())
	{
		FLyraInputAction& Binding = AbilityInputActions.AddDefaulted_GetRef();
		Binding.InputAction = RelayBeaconAction.Object;
		Binding.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Ability.RelayBeacon"));
	}

	// Relay reuses the established recovery-field action/key as its second ability slot.
	if (RecoveryFieldAction.Succeeded())
	{
		FLyraInputAction& Binding = AbilityInputActions.AddDefaulted_GetRef();
		Binding.InputAction = RecoveryFieldAction.Object;
		Binding.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Ability.RelayJumpPad"));
	}
}
