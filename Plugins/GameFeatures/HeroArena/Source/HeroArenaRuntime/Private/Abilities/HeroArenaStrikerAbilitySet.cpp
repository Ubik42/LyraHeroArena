#include "Abilities/HeroArenaStrikerAbilitySet.h"

#include "Abilities/HeroArenaGameplayAbility_Sprint.h"
#include "Abilities/HeroArenaGameplayAbility_RecoveryField.h"
#include "Abilities/HeroArenaGameplayAbility_WeaponOverload.h"
#include "Abilities/HeroArenaGameplayAbility_SelectHero.h"
#include "AbilitySystem/Abilities/LyraGameplayAbility.h"
#include "GameplayTagContainer.h"
#include "UObject/ConstructorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaStrikerAbilitySet)

UHeroArenaStrikerAbilitySet::UHeroArenaStrikerAbilitySet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	FLyraAbilitySet_GameplayAbility& Sprint = GrantedGameplayAbilities.AddDefaulted_GetRef();
	Sprint.Ability = UHeroArenaGameplayAbility_Sprint::StaticClass();
	Sprint.AbilityLevel = 1;
	Sprint.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Ability.Sprint"));

	ConstructorHelpers::FClassFinder<ULyraGameplayAbility> SpiralGrenadeAbility(
		TEXT("/HeroArena/Heroes/Abilities/GA_Striker_SpiralGrenade"));
	if (SpiralGrenadeAbility.Succeeded())
	{
		FLyraAbilitySet_GameplayAbility& SpiralGrenade = GrantedGameplayAbilities.AddDefaulted_GetRef();
		SpiralGrenade.Ability = SpiralGrenadeAbility.Class;
		SpiralGrenade.AbilityLevel = 1;
		SpiralGrenade.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Ability.SpiralGrenade"));
	}

	FLyraAbilitySet_GameplayAbility& RecoveryField = GrantedGameplayAbilities.AddDefaulted_GetRef();
	RecoveryField.Ability = UHeroArenaGameplayAbility_RecoveryField::StaticClass();
	RecoveryField.AbilityLevel = 1;
	RecoveryField.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Ability.RecoveryField"));

	FLyraAbilitySet_GameplayAbility& WeaponOverload = GrantedGameplayAbilities.AddDefaulted_GetRef();
	WeaponOverload.Ability = UHeroArenaGameplayAbility_WeaponOverload::StaticClass();
	WeaponOverload.AbilityLevel = 1;
	WeaponOverload.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Ability.WeaponOverload"));

	FLyraAbilitySet_GameplayAbility& SelectStriker = GrantedGameplayAbilities.AddDefaulted_GetRef();
	SelectStriker.Ability = UHeroArenaGameplayAbility_SelectStriker::StaticClass();
	SelectStriker.AbilityLevel = 1;
	SelectStriker.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.HeroSelect.Striker"));

	FLyraAbilitySet_GameplayAbility& SelectBulwark = GrantedGameplayAbilities.AddDefaulted_GetRef();
	SelectBulwark.Ability = UHeroArenaGameplayAbility_SelectBulwark::StaticClass();
	SelectBulwark.AbilityLevel = 1;
	SelectBulwark.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.HeroSelect.Bulwark"));
}
