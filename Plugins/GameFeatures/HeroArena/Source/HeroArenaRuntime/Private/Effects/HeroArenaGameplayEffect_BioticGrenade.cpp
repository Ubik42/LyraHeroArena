#include "Effects/HeroArenaGameplayEffect_BioticGrenade.h"

#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "GameplayEffectComponents/TargetTagRequirementsGameplayEffectComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaGameplayEffect_BioticGrenade)

UHeroArenaGameplayEffect_BioticGrenadeAmplify::UHeroArenaGameplayEffect_BioticGrenadeAmplify(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FScalableFloat(6.0f);
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	StackingType = EGameplayEffectStackingType::AggregateByTarget;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	StackLimitCount = 1;
	StackDurationRefreshPolicy = EGameplayEffectStackingDurationPolicy::RefreshOnSuccessfulApplication;
	FInheritedTagContainer Tags;
	Tags.Added.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Status.HeroArena.HealingAmplified")));
	UTargetTagsGameplayEffectComponent* Component = ObjectInitializer.CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(this, TEXT("AmplifyTags"));
	GEComponents.Add(Component);
	Component->SetAndApplyTargetTagChanges(Tags);
	UTargetTagRequirementsGameplayEffectComponent* Requirements = ObjectInitializer.CreateDefaultSubobject<UTargetTagRequirementsGameplayEffectComponent>(this, TEXT("AmplifyRemoval"));
	Requirements->RemovalTagRequirements.RequireTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Status.Death")));
	GEComponents.Add(Requirements);
}

UHeroArenaGameplayEffect_BioticGrenadeSuppress::UHeroArenaGameplayEffect_BioticGrenadeSuppress(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FScalableFloat(6.0f);
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	StackingType = EGameplayEffectStackingType::AggregateByTarget;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	StackLimitCount = 1;
	StackDurationRefreshPolicy = EGameplayEffectStackingDurationPolicy::RefreshOnSuccessfulApplication;
	FInheritedTagContainer Tags;
	Tags.Added.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Status.HeroArena.HealingSuppressed")));
	Tags.Added.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Status.HeroArena.Debuff.Purifiable")));
	UTargetTagsGameplayEffectComponent* Component = ObjectInitializer.CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(this, TEXT("SuppressTags"));
	GEComponents.Add(Component);
	Component->SetAndApplyTargetTagChanges(Tags);
	UTargetTagRequirementsGameplayEffectComponent* Requirements = ObjectInitializer.CreateDefaultSubobject<UTargetTagRequirementsGameplayEffectComponent>(this, TEXT("SuppressRemoval"));
	Requirements->RemovalTagRequirements.RequireTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Status.Death")));
	GEComponents.Add(Requirements);
}

UHeroArenaGameplayEffect_BioticGrenadeCooldown::UHeroArenaGameplayEffect_BioticGrenadeCooldown(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FScalableFloat(10.0f);
	FInheritedTagContainer Tags;
	Tags.Added.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Cooldown.Ability.BioticGrenade")));
	UTargetTagsGameplayEffectComponent* Component = ObjectInitializer.CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(this, TEXT("CooldownTags"));
	GEComponents.Add(Component);
	Component->SetAndApplyTargetTagChanges(Tags);
}
