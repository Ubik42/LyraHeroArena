#include "Hero/HeroArenaHeroDefinition.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaHeroDefinition)

#define LOCTEXT_NAMESPACE "HeroArenaHeroDefinition"

#if WITH_EDITOR
EDataValidationResult UHeroArenaHeroDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (HeroId.IsNone())
	{
		Context.AddError(LOCTEXT("MissingHeroId", "HeroId must be set."));
		Result = EDataValidationResult::Invalid;
	}

	if (DisplayName.IsEmpty())
	{
		Context.AddError(LOCTEXT("MissingDisplayName", "DisplayName must be set."));
		Result = EDataValidationResult::Invalid;
	}

	for (int32 Index = 0; Index < AbilitySets.Num(); ++Index)
	{
		if (!AbilitySets[Index])
		{
			Context.AddError(FText::Format(LOCTEXT("NullAbilitySet", "AbilitySets contains a null entry at index {0}."), FText::AsNumber(Index)));
			Result = EDataValidationResult::Invalid;
		}
	}

	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
