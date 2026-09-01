#include "Effects/HeroArenaGameplayEffect_BotSpiralGrenadeCooldown.h"

#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaGameplayEffect_BotSpiralGrenadeCooldown)

UHeroArenaGameplayEffect_BotSpiralGrenadeCooldown::UHeroArenaGameplayEffect_BotSpiralGrenadeCooldown(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FScalableFloat(6.0f);

	FInheritedTagContainer Tags;
	Tags.Added.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Cooldown.Ability.BotSpiralGrenade")));
	UTargetTagsGameplayEffectComponent* TargetTags =
		ObjectInitializer.CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(this, TEXT("BotSpiralGrenadeCooldownTags"));
	GEComponents.Add(TargetTags);
	TargetTags->SetAndApplyTargetTagChanges(Tags);
}
