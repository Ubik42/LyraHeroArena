#include "Effects/HeroArenaPurificationRules.h"

#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"

namespace HeroArenaPurification
{
	FGameplayEffectQuery MakeQuery()
	{
		const FGameplayTag Marker = FGameplayTag::RequestGameplayTag(
			TEXT("Status.HeroArena.Debuff.Purifiable"));
		return FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(Marker));
	}
}

bool UHeroArenaPurificationRules::HasPurifiableEffect(const UAbilitySystemComponent* TargetASC)
{
	return TargetASC && TargetASC->GetActiveEffects(HeroArenaPurification::MakeQuery()).Num() > 0;
}

int32 UHeroArenaPurificationRules::RemovePurifiableEffects(UAbilitySystemComponent* TargetASC)
{
	return TargetASC ? TargetASC->RemoveActiveEffects(HeroArenaPurification::MakeQuery()) : 0;
}
