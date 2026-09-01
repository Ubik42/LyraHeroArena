// Copyright Epic Games, Inc. All Rights Reserved.

#include "Effects/HeroArenaGameplayEffect_WeaponOverloadCooldown.h"

#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaGameplayEffect_WeaponOverloadCooldown)

UHeroArenaGameplayEffect_WeaponOverloadCooldown::UHeroArenaGameplayEffect_WeaponOverloadCooldown(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FScalableFloat(1.0f);

	FInheritedTagContainer CooldownTags;
	CooldownTags.Added.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Cooldown.Ability.WeaponOverload")));
	UTargetTagsGameplayEffectComponent* TargetTags =
		ObjectInitializer.CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(this, TEXT("WeaponOverloadCooldownTags"));
	GEComponents.Add(TargetTags);
	TargetTags->SetAndApplyTargetTagChanges(CooldownTags);
}
