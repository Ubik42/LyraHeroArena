#include "Effects/HeroArenaGameplayEffect_RelayQuickMaintenanceCooldown.h"

#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaGameplayEffect_RelayQuickMaintenanceCooldown)

UHeroArenaGameplayEffect_RelayQuickMaintenanceCooldown::UHeroArenaGameplayEffect_RelayQuickMaintenanceCooldown(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FScalableFloat(0.75f);
	FInheritedTagContainer Tags;
	Tags.Added.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Cooldown.Ability.RelayQuickMaintenance")));
	UTargetTagsGameplayEffectComponent* TargetTags =
		ObjectInitializer.CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(this, TEXT("RelayQuickMaintenanceCooldownTags"));
	GEComponents.Add(TargetTags);
	TargetTags->SetAndApplyTargetTagChanges(Tags);
}
