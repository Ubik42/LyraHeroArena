#include "Abilities/HeroArenaRelayAbilitySet.h"

#include "Abilities/HeroArenaGameplayAbility_RelayBeacon.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaRelayAbilitySet)

UHeroArenaRelayAbilitySet::UHeroArenaRelayAbilitySet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	FLyraAbilitySet_GameplayAbility& Beacon = GrantedGameplayAbilities.AddDefaulted_GetRef();
	Beacon.Ability = UHeroArenaGameplayAbility_RelayBeacon::StaticClass();
	Beacon.AbilityLevel = 1;
	Beacon.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Ability.RelayBeacon"));
}
