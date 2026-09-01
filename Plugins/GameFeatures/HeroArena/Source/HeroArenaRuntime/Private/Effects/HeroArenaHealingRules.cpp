#include "Effects/HeroArenaHealingRules.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/Attributes/LyraHealthSet.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Character/LyraHealthComponent.h"
#include "LyraGameplayTags.h"
#include "Messages/LyraVerbMessageHelpers.h"
#include "Scoring/HeroArenaContribution.h"
#include "System/LyraAssetManager.h"
#include "System/LyraGameData.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaHealingRules)

float UHeroArenaHealingRules::GetIncomingHealingMultiplier(const UAbilitySystemComponent* TargetASC)
{
	if (!TargetASC) return 1.0f;
	const FGameplayTag Suppressed = FGameplayTag::RequestGameplayTag(TEXT("Status.HeroArena.HealingSuppressed"));
	if (TargetASC->HasMatchingGameplayTag(Suppressed)) return 0.5f;
	const FGameplayTag Amplified = FGameplayTag::RequestGameplayTag(TEXT("Status.HeroArena.HealingAmplified"));
	return TargetASC->HasMatchingGameplayTag(Amplified) ? 1.5f : 1.0f;
}

float UHeroArenaHealingRules::ApplyStandardHealing(UAbilitySystemComponent* SourceASC, AActor* TargetActor,
	UObject* SourceObject, const float BaseHeal)
{
	ULyraHealthComponent* Health = ULyraHealthComponent::FindHealthComponent(TargetActor);
	UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(TargetActor);
	if (!SourceASC || !TargetASC || BaseHeal <= 0.0f || (Health && Health->IsDeadOrDying()))
	{
		return 0.0f;
	}

	const float OldHealth = Health ? Health->GetHealth()
		: TargetASC->GetNumericAttribute(ULyraHealthSet::GetHealthAttribute());
	const float MaxHealth = Health ? Health->GetMaxHealth()
		: TargetASC->GetNumericAttribute(ULyraHealthSet::GetMaxHealthAttribute());
	if (MaxHealth <= 0.0f || OldHealth >= MaxHealth)
	{
		return 0.0f;
	}

	const TSubclassOf<UGameplayEffect> HealEffect =
		ULyraAssetManager::GetSubclass(ULyraGameData::Get().HealGameplayEffect_SetByCaller);
	FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();
	Context.AddSourceObject(SourceObject);
	FGameplayEffectSpecHandle Spec = SourceASC->MakeOutgoingSpec(HealEffect, 1.0f, Context);
	if (!Spec.IsValid())
	{
		return 0.0f;
	}

	Spec.Data->SetSetByCallerMagnitude(
		LyraGameplayTags::SetByCaller_Heal, BaseHeal * GetIncomingHealingMultiplier(TargetASC));
	TargetASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
	const float NewHealth = Health ? Health->GetHealth()
		: TargetASC->GetNumericAttribute(ULyraHealthSet::GetHealthAttribute());
	const float ActualHealing = FMath::Max(0.0f, NewHealth - OldHealth);
	if (ULyraVerbMessageHelpers::GetPlayerStateFromObject(SourceASC->GetOwnerActor()) !=
		ULyraVerbMessageHelpers::GetPlayerStateFromObject(TargetActor))
	{
		HeroArenaContribution::Add(SourceASC->GetOwnerActor(), ActualHealing);
	}
	return ActualHealing;
}
