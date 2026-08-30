#include "Effects/HeroArenaGameplayEffect_PurificationNeedleCooldown.h"

#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaGameplayEffect_PurificationNeedleCooldown)

UHeroArenaGameplayEffect_PurificationNeedleCooldown::UHeroArenaGameplayEffect_PurificationNeedleCooldown(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FScalableFloat(8.0f);

	FInheritedTagContainer Tags;
	Tags.Added.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Cooldown.Ability.PurificationNeedle")));
	UTargetTagsGameplayEffectComponent* Component =
		ObjectInitializer.CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(this, TEXT("PurificationNeedleCooldownTags"));
	GEComponents.Add(Component);
	Component->SetAndApplyTargetTagChanges(Tags);
}
