#include "Abilities/HeroArenaBulwarkAbilitySet.h"

#include "Abilities/HeroArenaGameplayAbility_Barrier.h"
#include "Abilities/HeroArenaGameplayAbility_Charge.h"
#include "Abilities/HeroArenaGameplayAbility_GroundSlam.h"
#include "Abilities/HeroArenaGameplayAbility_KineticShield.h"
#include "Abilities/HeroArenaGameplayAbility_SelectHero.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaBulwarkAbilitySet)

UHeroArenaBulwarkAbilitySet::UHeroArenaBulwarkAbilitySet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	FLyraAbilitySet_GameplayAbility& Barrier = GrantedGameplayAbilities.AddDefaulted_GetRef();
	Barrier.Ability = UHeroArenaGameplayAbility_Barrier::StaticClass();
	Barrier.AbilityLevel = 1;
	Barrier.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Ability.Barrier"));

	FLyraAbilitySet_GameplayAbility& Charge = GrantedGameplayAbilities.AddDefaulted_GetRef();
	Charge.Ability = UHeroArenaGameplayAbility_Charge::StaticClass();
	Charge.AbilityLevel = 1;
	Charge.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Ability.Charge"));

	FLyraAbilitySet_GameplayAbility& KineticShield = GrantedGameplayAbilities.AddDefaulted_GetRef();
	KineticShield.Ability = UHeroArenaGameplayAbility_KineticShield::StaticClass();
	KineticShield.AbilityLevel = 1;
	KineticShield.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Ability.KineticShield"));

	FLyraAbilitySet_GameplayAbility& GroundSlam = GrantedGameplayAbilities.AddDefaulted_GetRef();
	GroundSlam.Ability = UHeroArenaGameplayAbility_GroundSlam::StaticClass();
	GroundSlam.AbilityLevel = 1;
	GroundSlam.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Ability.GroundSlam"));

	FLyraAbilitySet_GameplayAbility& SelectStriker = GrantedGameplayAbilities.AddDefaulted_GetRef();
	SelectStriker.Ability = UHeroArenaGameplayAbility_SelectStriker::StaticClass();
	SelectStriker.AbilityLevel = 1;
	SelectStriker.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.HeroSelect.Striker"));

	FLyraAbilitySet_GameplayAbility& SelectBulwark = GrantedGameplayAbilities.AddDefaulted_GetRef();
	SelectBulwark.Ability = UHeroArenaGameplayAbility_SelectBulwark::StaticClass();
	SelectBulwark.AbilityLevel = 1;
	SelectBulwark.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.HeroSelect.Bulwark"));
}
