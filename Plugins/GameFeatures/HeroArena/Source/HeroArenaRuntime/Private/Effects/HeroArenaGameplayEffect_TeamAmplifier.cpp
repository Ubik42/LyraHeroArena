#include "Effects/HeroArenaGameplayEffect_TeamAmplifier.h"

#include "GameplayEffectComponents/TargetTagRequirementsGameplayEffectComponent.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaGameplayEffect_TeamAmplifier)

UHeroArenaGameplayEffect_TeamAmplified::UHeroArenaGameplayEffect_TeamAmplified(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FScalableFloat(1.1f);
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	StackingType = EGameplayEffectStackingType::AggregateByTarget;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	StackLimitCount = 1;
	StackDurationRefreshPolicy = EGameplayEffectStackingDurationPolicy::RefreshOnSuccessfulApplication;

	FInheritedTagContainer Tags;
	Tags.Added.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Status.HeroArena.TeamAmplified")));
	UTargetTagsGameplayEffectComponent* TagComponent =
		ObjectInitializer.CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(this, TEXT("TeamAmplifiedTags"));
	GEComponents.Add(TagComponent);
	TagComponent->SetAndApplyTargetTagChanges(Tags);

	UTargetTagRequirementsGameplayEffectComponent* Removal =
		ObjectInitializer.CreateDefaultSubobject<UTargetTagRequirementsGameplayEffectComponent>(this, TEXT("TeamAmplifiedRemoval"));
	Removal->RemovalTagRequirements.RequireTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Status.Death")));
	GEComponents.Add(Removal);
}

UHeroArenaGameplayEffect_TeamAmplifierCooldown::UHeroArenaGameplayEffect_TeamAmplifierCooldown(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FScalableFloat(1.0f);
	FInheritedTagContainer Tags;
	Tags.Added.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Cooldown.Ability.RelayTeamAmplifier")));
	UTargetTagsGameplayEffectComponent* TagComponent =
		ObjectInitializer.CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(this, TEXT("TeamAmplifierCooldownTags"));
	GEComponents.Add(TagComponent);
	TagComponent->SetAndApplyTargetTagChanges(Tags);
}
