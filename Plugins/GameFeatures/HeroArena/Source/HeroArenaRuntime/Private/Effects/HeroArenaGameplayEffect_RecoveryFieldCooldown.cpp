// Copyright Epic Games, Inc. All Rights Reserved.

#include "Effects/HeroArenaGameplayEffect_RecoveryFieldCooldown.h"

#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "GameplayTagContainer.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaGameplayEffect_RecoveryFieldCooldown)


UHeroArenaGameplayEffect_RecoveryFieldCooldown::UHeroArenaGameplayEffect_RecoveryFieldCooldown(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FScalableFloat(12.0f);

	FInheritedTagContainer CooldownTags;
	CooldownTags.Added.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Cooldown.Ability.RecoveryField")));

	UTargetTagsGameplayEffectComponent* TargetTags =
		ObjectInitializer.CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(this, TEXT("RecoveryFieldCooldownTags"));
	GEComponents.Add(TargetTags);
	TargetTags->SetAndApplyTargetTagChanges(CooldownTags);
}
