#pragma once

#include "Components/PlayerStateComponent.h"

#include "HeroArenaRecallHistoryComponent.generated.h"

class ACharacter;
class UHeroArenaHeroDefinition;
class ULyraInventoryItemInstance;

USTRUCT(BlueprintType)
struct FHeroArenaRecallResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Hero Arena|Recall")
	bool bSucceeded = false;

	UPROPERTY(BlueprintReadOnly, Category="Hero Arena|Recall")
	FName Reason = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="Hero Arena|Recall")
	float RecalledSeconds = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="Hero Arena|Recall")
	FVector StartLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category="Hero Arena|Recall")
	FVector EndLocation = FVector::ZeroVector;
};

/** Server-only, Pawn-lifetime-bounded history used by Phase Recall. */
UCLASS(Blueprintable, Meta=(BlueprintSpawnableComponent))
class HEROARENARUNTIME_API UHeroArenaRecallHistoryComponent : public UPlayerStateComponent
{
	GENERATED_BODY()

public:
	UHeroArenaRecallHistoryComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	bool TryRecallPosition(FHeroArenaRecallResult& OutResult);
	int32 GetSampleCount() const { return Samples.Num(); }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	struct FRecallSample
	{
		TWeakObjectPtr<ACharacter> Pawn;
		float ServerTime = 0.0f;
		FVector Location = FVector::ZeroVector;
		FRotator Rotation = FRotator::ZeroRotator;
		float Health = 0.0f;
		TWeakObjectPtr<ULyraInventoryItemInstance> PistolItem;
		int32 MagazineAmmo = 0;
		int32 MagazineCapacity = 0;
	};

	UFUNCTION()
	void HandleHeroChanged(const UHeroArenaHeroDefinition* OldHero, const UHeroArenaHeroDefinition* NewHero);

	UFUNCTION()
	void HandlePawnSet(APlayerState* PlayerState, APawn* NewPawn, APawn* OldPawn);

	void CaptureSample();
	void ResetHistory(const TCHAR* Reason);
	bool IsPhaseSelected() const;
	bool IsLocationLegal(ACharacter* Character, const FVector& Location, const FRotator& Rotation) const;
	bool GetCurrentPistolState(ACharacter* Character, ULyraInventoryItemInstance*& OutItem,
		int32& OutMagazineAmmo, int32& OutMagazineCapacity) const;

	UPROPERTY(EditDefaultsOnly, Category="Hero Arena|Recall", meta=(ClampMin="0.05", Units="s"))
	float SampleInterval = 0.1f;

	UPROPERTY(EditDefaultsOnly, Category="Hero Arena|Recall", meta=(ClampMin="1.0", Units="s"))
	float HistoryDuration = 4.0f;

	UPROPERTY(EditDefaultsOnly, Category="Hero Arena|Recall", meta=(ClampMin="0.5", Units="s"))
	float RecallDelay = 3.0f;

	UPROPERTY(EditDefaultsOnly, Category="Hero Arena|Recall", meta=(ClampMin="0.1", Units="s"))
	float MaximumSampleGap = 0.35f;

	UPROPERTY(EditDefaultsOnly, Category="Hero Arena|Recall", meta=(ClampMin="0.1", Units="s"))
	float RecallCooldown = 8.0f;

	TArray<FRecallSample> Samples;
	FTimerHandle SampleTimerHandle;
	float NextRecallAllowedServerTime = 0.0f;
};
