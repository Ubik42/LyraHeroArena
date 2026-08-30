#include "Hero/HeroArenaHeroRoster.h"

#include "Hero/HeroArenaHeroDefinition.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaHeroRoster)

#define LOCTEXT_NAMESPACE "HeroArenaHeroRoster"

const UHeroArenaHeroDefinition* UHeroArenaHeroRoster::FindHeroById(const FName HeroId) const
{
	if (HeroId.IsNone())
	{
		return nullptr;
	}

	for (const UHeroArenaHeroDefinition* Hero : Heroes)
	{
		if (Hero && Hero->HeroId == HeroId)
		{
			return Hero;
		}
	}

	return nullptr;
}

bool UHeroArenaHeroRoster::ContainsHero(const UHeroArenaHeroDefinition* Hero) const
{
	return Hero && Heroes.Contains(Hero);
}

#if WITH_EDITOR
EDataValidationResult UHeroArenaHeroRoster::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	TSet<FName> SeenHeroIds;

	if (Heroes.IsEmpty())
	{
		Context.AddError(LOCTEXT("EmptyRoster", "Heroes must contain at least one hero."));
		Result = EDataValidationResult::Invalid;
	}

	for (int32 Index = 0; Index < Heroes.Num(); ++Index)
	{
		const UHeroArenaHeroDefinition* Hero = Heroes[Index];
		if (!Hero)
		{
			Context.AddError(FText::Format(LOCTEXT("NullHero", "Heroes contains a null entry at index {0}."), FText::AsNumber(Index)));
			Result = EDataValidationResult::Invalid;
			continue;
		}

		if (SeenHeroIds.Contains(Hero->HeroId))
		{
			Context.AddError(FText::Format(LOCTEXT("DuplicateHeroId", "HeroId '{0}' appears more than once."), FText::FromName(Hero->HeroId)));
			Result = EDataValidationResult::Invalid;
		}
		SeenHeroIds.Add(Hero->HeroId);
	}

	if (!DefaultHero)
	{
		Context.AddError(LOCTEXT("MissingDefaultHero", "DefaultHero must be set."));
		Result = EDataValidationResult::Invalid;
	}
	else if (!Heroes.Contains(DefaultHero))
	{
		Context.AddError(LOCTEXT("DefaultHeroOutsideRoster", "DefaultHero must also be present in Heroes."));
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
