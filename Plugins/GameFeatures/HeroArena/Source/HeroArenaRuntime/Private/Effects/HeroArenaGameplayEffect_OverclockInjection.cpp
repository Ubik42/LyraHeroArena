#include "Effects/HeroArenaGameplayEffect_OverclockInjection.h"

#include "GameplayEffectComponents/TargetTagRequirementsGameplayEffectComponent.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaGameplayEffect_OverclockInjection)

UHeroArenaGameplayEffect_OverclockInjection::UHeroArenaGameplayEffect_OverclockInjection(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FScalableFloat(8.0f);
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	StackingType = EGameplayEffectStackingType::AggregateByTarget;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	StackLimitCount = 1;
	StackDurationRefreshPolicy = EGameplayEffectStackingDurationPolicy::RefreshOnSuccessfulApplication;

	FInheritedTagContainer Tags;
	Tags.Added.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Status.HeroArena.Overclocked")));
	UTargetTagsGameplayEffectComponent* TagComponent =
		ObjectInitializer.CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(this, TEXT("OverclockTags"));
	GEComponents.Add(TagComponent);
	TagComponent->SetAndApplyTargetTagChanges(Tags);

	UTargetTagRequirementsGameplayEffectComponent* Removal =
		ObjectInitializer.CreateDefaultSubobject<UTargetTagRequirementsGameplayEffectComponent>(this, TEXT("OverclockRemoval"));
	Removal->RemovalTagRequirements.RequireTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Status.Death")));
	GEComponents.Add(Removal);
}

UHeroArenaGameplayEffect_OverclockInjectionCooldown::UHeroArenaGameplayEffect_OverclockInjectionCooldown(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FScalableFloat(1.0f);
	FInheritedTagContainer Tags;
	Tags.Added.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Cooldown.Ability.OverclockInjection")));
	UTargetTagsGameplayEffectComponent* Component =
		ObjectInitializer.CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(this, TEXT("OverclockCooldownTags"));
	GEComponents.Add(Component);
	Component->SetAndApplyTargetTagChanges(Tags);
}
