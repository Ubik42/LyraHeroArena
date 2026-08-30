#pragma once

#include "Engine/DataAsset.h"

#include "HeroArenaHeroRoster.generated.h"

class UHeroArenaHeroDefinition;

/** Ordered set of heroes available to one Hero Arena experience. */
UCLASS(BlueprintType, Const, Meta = (DisplayName = "Hero Arena Hero Roster"))
class HEROARENARUNTIME_API UHeroArenaHeroRoster : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Hero Arena|Roster")
	const UHeroArenaHeroDefinition* FindHeroById(FName HeroId) const;

	UFUNCTION(BlueprintPure, Category = "Hero Arena|Roster")
	bool ContainsHero(const UHeroArenaHeroDefinition* Hero) const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Roster")
	TArray<TObjectPtr<const UHeroArenaHeroDefinition>> Heroes;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Roster")
	TObjectPtr<const UHeroArenaHeroDefinition> DefaultHero;
};
