#include "Effects/HeroArenaGameplayEffect_BarrierCooldown.h"

#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaGameplayEffect_BarrierCooldown)

UHeroArenaGameplayEffect_BarrierCooldown::UHeroArenaGameplayEffect_BarrierCooldown(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FScalableFloat(10.0f);

	FInheritedTagContainer CooldownTags;
	CooldownTags.Added.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Cooldown.Ability.Barrier")));
	UTargetTagsGameplayEffectComponent* TargetTags =
		ObjectInitializer.CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(this, TEXT("BarrierCooldownTags"));
	GEComponents.Add(TargetTags);
	TargetTags->SetAndApplyTargetTagChanges(CooldownTags);
}
