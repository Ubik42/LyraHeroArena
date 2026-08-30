#include "Effects/HeroArenaGameplayEffect_MedicalFeedbackCooldown.h"

#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaGameplayEffect_MedicalFeedbackCooldown)

UHeroArenaGameplayEffect_MedicalFeedbackCooldown::UHeroArenaGameplayEffect_MedicalFeedbackCooldown(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FScalableFloat(0.5f);

	FInheritedTagContainer Tags;
	Tags.Added.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Cooldown.Passive.MedicalFeedback")));
	UTargetTagsGameplayEffectComponent* Component =
		ObjectInitializer.CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(this, TEXT("MedicalFeedbackCooldownTags"));
	GEComponents.Add(Component);
	Component->SetAndApplyTargetTagChanges(Tags);
}
