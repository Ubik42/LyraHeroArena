#include "Effects/HeroArenaGameplayEffect_RelayJumpPadCooldown.h"

#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaGameplayEffect_RelayJumpPadCooldown)

UHeroArenaGameplayEffect_RelayJumpPadCooldown::UHeroArenaGameplayEffect_RelayJumpPadCooldown(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FScalableFloat(10.0f);
	FInheritedTagContainer Tags;
	Tags.Added.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Cooldown.Ability.RelayJumpPad")));
	UTargetTagsGameplayEffectComponent* TargetTags =
		ObjectInitializer.CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(this, TEXT("RelayJumpPadCooldownTags"));
	GEComponents.Add(TargetTags);
	TargetTags->SetAndApplyTargetTagChanges(Tags);
}
