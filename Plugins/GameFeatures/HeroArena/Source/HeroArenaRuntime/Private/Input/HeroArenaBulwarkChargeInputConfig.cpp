#include "Input/HeroArenaBulwarkChargeInputConfig.h"

#include "InputAction.h"
#include "UObject/ConstructorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaBulwarkChargeInputConfig)

UHeroArenaBulwarkChargeInputConfig::UHeroArenaBulwarkChargeInputConfig(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ConstructorHelpers::FObjectFinder<UInputAction> ChargeAction(
		TEXT("/HeroArena/Input/IA_Bulwark_Charge.IA_Bulwark_Charge"));
	if (ChargeAction.Succeeded())
	{
		FLyraInputAction& Binding = AbilityInputActions.AddDefaulted_GetRef();
		Binding.InputAction = ChargeAction.Object;
		Binding.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Ability.Charge"));
	}
}
