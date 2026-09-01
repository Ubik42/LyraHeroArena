#include "Abilities/HeroArenaRelayAbilitySet.h"

#include "Abilities/HeroArenaGameplayAbility_RelayBeacon.h"
#include "Abilities/HeroArenaGameplayAbility_RelayJumpPad.h"
#include "Abilities/HeroArenaGameplayAbility_RelayMaintenanceTuning.h"
#include "Abilities/HeroArenaGameplayAbility_RelayQuickMaintenance.h"
#include "Abilities/HeroArenaGameplayAbility_RelayTeamAmplifier.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaRelayAbilitySet)

UHeroArenaRelayAbilitySet::UHeroArenaRelayAbilitySet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	FLyraAbilitySet_GameplayAbility& MaintenanceTuning = GrantedGameplayAbilities.AddDefaulted_GetRef();
	MaintenanceTuning.Ability = UHeroArenaGameplayAbility_RelayMaintenanceTuning::StaticClass();
	MaintenanceTuning.AbilityLevel = 1;

	FLyraAbilitySet_GameplayAbility& Beacon = GrantedGameplayAbilities.AddDefaulted_GetRef();
	Beacon.Ability = UHeroArenaGameplayAbility_RelayBeacon::StaticClass();
	Beacon.AbilityLevel = 1;
	Beacon.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Ability.RelayBeacon"));

	FLyraAbilitySet_GameplayAbility& JumpPad = GrantedGameplayAbilities.AddDefaulted_GetRef();
	JumpPad.Ability = UHeroArenaGameplayAbility_RelayJumpPad::StaticClass();
	JumpPad.AbilityLevel = 1;
	JumpPad.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Ability.RelayJumpPad"));

	FLyraAbilitySet_GameplayAbility& QuickMaintenance = GrantedGameplayAbilities.AddDefaulted_GetRef();
	QuickMaintenance.Ability = UHeroArenaGameplayAbility_RelayQuickMaintenance::StaticClass();
	QuickMaintenance.AbilityLevel = 1;
	QuickMaintenance.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Ability.RelayQuickMaintenance"));

	FLyraAbilitySet_GameplayAbility& TeamAmplifier = GrantedGameplayAbilities.AddDefaulted_GetRef();
	TeamAmplifier.Ability = UHeroArenaGameplayAbility_RelayTeamAmplifier::StaticClass();
	TeamAmplifier.AbilityLevel = 1;
	TeamAmplifier.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Ability.RelayTeamAmplifier"));
}
