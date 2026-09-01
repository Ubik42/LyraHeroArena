#include "Effects/HeroArenaGameplayEffect_StickyBomb.h"

#include "GameplayEffectComponents/TargetTagRequirementsGameplayEffectComponent.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaGameplayEffect_StickyBomb)

UHeroArenaGameplayEffect_StickyBombAttached::UHeroArenaGameplayEffect_StickyBombAttached(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FScalableFloat(2.0f);

	FInheritedTagContainer Tags;
	Tags.Added.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Status.HeroArena.StickyBomb.Attached")));
	Tags.Added.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Status.HeroArena.Debuff.Purifiable")));
	UTargetTagsGameplayEffectComponent* TargetTags =
		ObjectInitializer.CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(this, TEXT("StickyBombTags"));
	GEComponents.Add(TargetTags);
	TargetTags->SetAndApplyTargetTagChanges(Tags);

	UTargetTagRequirementsGameplayEffectComponent* Requirements =
		ObjectInitializer.CreateDefaultSubobject<UTargetTagRequirementsGameplayEffectComponent>(this, TEXT("StickyBombDeathRemoval"));
	Requirements->RemovalTagRequirements.RequireTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Status.Death")));
	GEComponents.Add(Requirements);
}

UHeroArenaGameplayEffect_StickyBombCooldown::UHeroArenaGameplayEffect_StickyBombCooldown(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FScalableFloat(1.0f);

	FInheritedTagContainer Tags;
	Tags.Added.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Cooldown.Ability.StickyBomb")));
	UTargetTagsGameplayEffectComponent* TargetTags =
		ObjectInitializer.CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(this, TEXT("StickyBombCooldownTags"));
	GEComponents.Add(TargetTags);
	TargetTags->SetAndApplyTargetTagChanges(Tags);
}
