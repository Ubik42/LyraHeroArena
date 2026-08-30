#include "Input/HeroArenaHeroSelectionInputConfig.h"

#include "InputAction.h"
#include "UObject/ConstructorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaHeroSelectionInputConfig)

UHeroArenaHeroSelectionInputConfig::UHeroArenaHeroSelectionInputConfig(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	struct FSelectionBinding
	{
		const TCHAR* ActionPath;
		const TCHAR* InputTag;
	};
	const FSelectionBinding Bindings[] = {
		{ TEXT("/HeroArena/Input/IA_HeroSelect_Striker.IA_HeroSelect_Striker"), TEXT("InputTag.HeroSelect.Striker") },
		{ TEXT("/HeroArena/Input/IA_HeroSelect_Bulwark.IA_HeroSelect_Bulwark"), TEXT("InputTag.HeroSelect.Bulwark") }
	};

	for (const FSelectionBinding& Source : Bindings)
	{
		ConstructorHelpers::FObjectFinder<UInputAction> Action(Source.ActionPath);
		if (Action.Succeeded())
		{
			FLyraInputAction& Binding = AbilityInputActions.AddDefaulted_GetRef();
			Binding.InputAction = Action.Object;
			Binding.InputTag = FGameplayTag::RequestGameplayTag(Source.InputTag);
		}
	}
}
