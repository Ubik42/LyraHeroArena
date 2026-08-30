#include "Abilities/HeroArenaPhaseAbilitySet.h"

#include "Abilities/HeroArenaGameplayAbility_Blink.h"
#include "Abilities/HeroArenaGameplayAbility_PhasePistolTuning.h"
#include "Abilities/HeroArenaGameplayAbility_Recall.h"
#include "Abilities/HeroArenaGameplayAbility_StickyBomb.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaPhaseAbilitySet)

UHeroArenaPhaseAbilitySet::UHeroArenaPhaseAbilitySet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	FLyraAbilitySet_GameplayAbility& PistolTuning = GrantedGameplayAbilities.AddDefaulted_GetRef();
	PistolTuning.Ability = UHeroArenaGameplayAbility_PhasePistolTuning::StaticClass();
	PistolTuning.AbilityLevel = 1;

	FLyraAbilitySet_GameplayAbility& Blink = GrantedGameplayAbilities.AddDefaulted_GetRef();
	Blink.Ability = UHeroArenaGameplayAbility_Blink::StaticClass();
	Blink.AbilityLevel = 1;
	Blink.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Ability.Blink"));

	FLyraAbilitySet_GameplayAbility& Recall = GrantedGameplayAbilities.AddDefaulted_GetRef();
	Recall.Ability = UHeroArenaGameplayAbility_Recall::StaticClass();
	Recall.AbilityLevel = 1;
	Recall.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Ability.Recall"));

	FLyraAbilitySet_GameplayAbility& StickyBomb = GrantedGameplayAbilities.AddDefaulted_GetRef();
	StickyBomb.Ability = UHeroArenaGameplayAbility_StickyBomb::StaticClass();
	StickyBomb.AbilityLevel = 1;
	StickyBomb.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Ability.StickyBomb"));
}
