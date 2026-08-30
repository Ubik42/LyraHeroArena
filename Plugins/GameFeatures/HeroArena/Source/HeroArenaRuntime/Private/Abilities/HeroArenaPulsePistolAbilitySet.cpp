#include "Abilities/HeroArenaPulsePistolAbilitySet.h"

#include "Abilities/HeroArenaGameplayAbility_BioticGrenade.h"
#include "Abilities/HeroArenaGameplayAbility_PurificationNeedle.h"
#include "Abilities/HeroArenaGameplayAbility_OverclockInjection.h"
#include "AbilitySystem/Abilities/LyraGameplayAbility.h"
#include "UObject/ConstructorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaPulsePistolAbilitySet)

UHeroArenaPulsePistolAbilitySet::UHeroArenaPulsePistolAbilitySet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	static ConstructorHelpers::FClassFinder<ULyraGameplayAbility> PulseFire(
		TEXT("/HeroArena/Heroes/Pulse/Weapons/GA_Pulse_BioticPistol"));
	static ConstructorHelpers::FClassFinder<ULyraGameplayAbility> PistolReload(
		TEXT("/Game/Weapons/Pistol/GA_Weapon_Reload_Pistol"));
	static ConstructorHelpers::FClassFinder<ULyraGameplayAbility> AutoReload(
		TEXT("/Game/Weapons/GA_Weapon_AutoReload"));

	if (PulseFire.Succeeded())
	{
		FLyraAbilitySet_GameplayAbility& Entry = GrantedGameplayAbilities.AddDefaulted_GetRef();
		Entry.Ability = PulseFire.Class;
		Entry.AbilityLevel = 1;
		Entry.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Weapon.Fire"));
	}
	if (PistolReload.Succeeded())
	{
		FLyraAbilitySet_GameplayAbility& Entry = GrantedGameplayAbilities.AddDefaulted_GetRef();
		Entry.Ability = PistolReload.Class;
		Entry.AbilityLevel = 1;
		Entry.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Weapon.Reload"));
	}
	if (AutoReload.Succeeded())
	{
		FLyraAbilitySet_GameplayAbility& Entry = GrantedGameplayAbilities.AddDefaulted_GetRef();
		Entry.Ability = AutoReload.Class;
		Entry.AbilityLevel = 1;
	}

	FLyraAbilitySet_GameplayAbility& BioticGrenade = GrantedGameplayAbilities.AddDefaulted_GetRef();
	BioticGrenade.Ability = UHeroArenaGameplayAbility_BioticGrenade::StaticClass();
	BioticGrenade.AbilityLevel = 1;
	BioticGrenade.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Ability.BioticGrenade"));

	FLyraAbilitySet_GameplayAbility& PurificationNeedle = GrantedGameplayAbilities.AddDefaulted_GetRef();
	PurificationNeedle.Ability = UHeroArenaGameplayAbility_PurificationNeedle::StaticClass();
	PurificationNeedle.AbilityLevel = 1;
	PurificationNeedle.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Ability.PurificationNeedle"));

	FLyraAbilitySet_GameplayAbility& OverclockInjection = GrantedGameplayAbilities.AddDefaulted_GetRef();
	OverclockInjection.Ability = UHeroArenaGameplayAbility_OverclockInjection::StaticClass();
	OverclockInjection.AbilityLevel = 1;
	OverclockInjection.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Ability.OverclockInjection"));
}
