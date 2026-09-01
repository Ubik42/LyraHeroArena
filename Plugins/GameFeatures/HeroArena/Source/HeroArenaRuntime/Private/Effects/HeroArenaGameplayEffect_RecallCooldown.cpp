#include "Effects/HeroArenaGameplayEffect_RecallCooldown.h"

#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaGameplayEffect_RecallCooldown)

UHeroArenaGameplayEffect_RecallCooldown::UHeroArenaGameplayEffect_RecallCooldown(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FScalableFloat(8.0f);

	FInheritedTagContainer CooldownTags;
	CooldownTags.Added.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Cooldown.Ability.Recall")));
	UTargetTagsGameplayEffectComponent* TargetTags =
		ObjectInitializer.CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(this, TEXT("RecallCooldownTags"));
	GEComponents.Add(TargetTags);
	TargetTags->SetAndApplyTargetTagChanges(CooldownTags);
}
