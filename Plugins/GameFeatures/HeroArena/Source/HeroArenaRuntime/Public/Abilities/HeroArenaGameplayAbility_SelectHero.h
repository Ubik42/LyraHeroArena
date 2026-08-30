#pragma once

#include "AbilitySystem/Abilities/LyraGameplayAbility.h"

#include "HeroArenaGameplayAbility_SelectHero.generated.h"

/** Shared, input-driven hero selection ability. The server always revalidates the requested ID against the roster. */
UCLASS(Abstract)
class HEROARENARUNTIME_API UHeroArenaGameplayAbility_SelectHero : public ULyraGameplayAbility
{
	GENERATED_BODY()

public:
	UHeroArenaGameplayAbility_SelectHero(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	FName RequestedHeroId;
};

UCLASS()
class HEROARENARUNTIME_API UHeroArenaGameplayAbility_SelectStriker : public UHeroArenaGameplayAbility_SelectHero
{
	GENERATED_BODY()
public:
	UHeroArenaGameplayAbility_SelectStriker(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};

UCLASS()
class HEROARENARUNTIME_API UHeroArenaGameplayAbility_SelectBulwark : public UHeroArenaGameplayAbility_SelectHero
{
	GENERATED_BODY()
public:
	UHeroArenaGameplayAbility_SelectBulwark(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
