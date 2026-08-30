#include "Effects/HeroArenaGameplayEffect_KineticShieldCooldown.h"

#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaGameplayEffect_KineticShieldCooldown)

UHeroArenaGameplayEffect_KineticShieldCooldown::UHeroArenaGameplayEffect_KineticShieldCooldown(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FScalableFloat(12.0f);

	FInheritedTagContainer CooldownTags;
	CooldownTags.Added.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Cooldown.Ability.KineticShield")));
	UTargetTagsGameplayEffectComponent* TargetTags =
		ObjectInitializer.CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(this, TEXT("KineticShieldCooldownTags"));
	GEComponents.Add(TargetTags);
	TargetTags->SetAndApplyTargetTagChanges(CooldownTags);
}
