#include "Input/HeroArenaBulwarkInputConfig.h"

#include "InputAction.h"
#include "UObject/ConstructorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaBulwarkInputConfig)

UHeroArenaBulwarkInputConfig::UHeroArenaBulwarkInputConfig(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ConstructorHelpers::FObjectFinder<UInputAction> BarrierAction(
		TEXT("/HeroArena/Input/IA_Bulwark_Barrier.IA_Bulwark_Barrier"));
	if (BarrierAction.Succeeded())
	{
		FLyraInputAction& Binding = AbilityInputActions.AddDefaulted_GetRef();
		Binding.InputAction = BarrierAction.Object;
		Binding.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Ability.Barrier"));
	}

	ConstructorHelpers::FObjectFinder<UInputAction> KineticShieldAction(
		TEXT("/HeroArena/Input/IA_Bulwark_KineticShield.IA_Bulwark_KineticShield"));
	if (KineticShieldAction.Succeeded())
	{
		FLyraInputAction& Binding = AbilityInputActions.AddDefaulted_GetRef();
		Binding.InputAction = KineticShieldAction.Object;
		Binding.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Ability.KineticShield"));
	}

	ConstructorHelpers::FObjectFinder<UInputAction> GroundSlamAction(
		TEXT("/HeroArena/Input/IA_Bulwark_GroundSlam.IA_Bulwark_GroundSlam"));
	if (GroundSlamAction.Succeeded())
	{
		FLyraInputAction& Binding = AbilityInputActions.AddDefaulted_GetRef();
		Binding.InputAction = GroundSlamAction.Object;
		Binding.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Ability.GroundSlam"));
	}
}
