#pragma once

#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"

#include "HeroArenaHeroDefinition.generated.h"

class UTexture2D;
class ULyraAbilitySet;
class ULyraInventoryItemDefinition;

UENUM(BlueprintType)
enum class EHeroArenaRole : uint8
{
	Vanguard UMETA(DisplayName = "前排"),
	Damage UMETA(DisplayName = "输出"),
	Support UMETA(DisplayName = "支援")
};

/**
 * Describes only the identity and hero-specific gameplay layer.
 * Core pawn, input, camera and baseline abilities remain owned by Lyra PawnData.
 */
UCLASS(BlueprintType, Const, Meta = (DisplayName = "Hero Arena Hero Definition"))
class HEROARENARUNTIME_API UHeroArenaHeroDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hero")
	FName HeroId;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hero")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hero", meta = (MultiLine = true))
	FText ShortDescription;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hero")
	EHeroArenaRole Role = EHeroArenaRole::Damage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hero")
	FGameplayTagContainer HeroTags;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation", meta = (AssetBundles = "UI"))
	TSoftObjectPtr<UTexture2D> Portrait;

	/** Ability sets layered on top of the shared Lyra PawnData grants. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gameplay")
	TArray<TObjectPtr<const ULyraAbilitySet>> AbilitySets;

	/** Optional weapon loadout applied by the server to quick-bar slot zero on spawn/hero change. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gameplay")
	TSoftClassPtr<ULyraInventoryItemDefinition> DefaultWeaponItem;
};
