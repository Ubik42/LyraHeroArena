#pragma once

#include "Components/GameStateComponent.h"
#include "GameFramework/GameplayMessageSubsystem.h"

#include "HeroArenaControlPointRotationComponent.generated.h"

struct FLyraControlPointStatusMessage;

/** Server-authoritative A/B/C objective rotation layered over ShooterCore control points. */
UCLASS(Meta=(BlueprintSpawnableComponent))
class HEROARENARUNTIME_API UHeroArenaControlPointRotationComponent : public UGameStateComponent
{
	GENERATED_BODY()

public:
	UHeroArenaControlPointRotationComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UFUNCTION(BlueprintPure, Category="Hero Arena|Objective")
	int32 GetActivePointIndex() const { return ActivePointIndex; }

	UFUNCTION(BlueprintPure, Category="Hero Arena|Objective")
	int32 GetActiveOwnerTeam() const { return ActiveOwnerTeam; }

	UFUNCTION(BlueprintPure, Category="Hero Arena|Objective")
	bool IsTransitioning() const { return bTransitioning; }

	UFUNCTION(BlueprintPure, Category="Hero Arena|Objective")
	float GetSecondsUntilChange() const;

	UFUNCTION(BlueprintPure, Category="Hero Arena|Objective")
	FText GetCurrentPointLabel() const;

	UFUNCTION(BlueprintPure, Category="Hero Arena|Objective")
	FVector GetCurrentPointLocation() const { return CurrentPointLocation; }

	UFUNCTION(BlueprintPure, Category="Hero Arena|Objective")
	int32 GetCapturingTeam() const { return CapturingTeam; }

	UFUNCTION(BlueprintPure, Category="Hero Arena|Objective")
	float GetCaptureProgress() const { return CaptureProgress; }

	UFUNCTION(BlueprintPure, Category="Hero Arena|Objective")
	bool IsActivePointContested() const { return bActivePointContested; }

	bool HasMatchEnded() const { return bMatchEnded; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	void DiscoverControlPoints();
	void TickRotation();
	void ActivatePoint(int32 PointIndex);
	void StartTransition();
	void SetPointActive(AActor* Point, bool bActive);
	void SetPointOwner(AActor* Point, int32 TeamId);
	void RefreshActiveCaptureState();
	void ClearInactiveScoringOwners();
	void HandlePointCaptured(FGameplayTag Channel, const FLyraControlPointStatusMessage& Message);
	float GetServerTime() const;

	UPROPERTY(Replicated)
	int32 ActivePointIndex = INDEX_NONE;

	UPROPERTY(Replicated)
	int32 PendingPointIndex = INDEX_NONE;

	UPROPERTY(Replicated)
	int32 ActiveOwnerTeam = 0;

	UPROPERTY(Replicated)
	bool bTransitioning = true;

	UPROPERTY(Replicated)
	float ChangeAtServerTime = 0.0f;

	UPROPERTY(Replicated)
	FVector_NetQuantize CurrentPointLocation = FVector::ZeroVector;

	UPROPERTY(Replicated)
	int32 CapturingTeam = 0;

	UPROPERTY(Replicated)
	float CaptureProgress = 0.0f;

	UPROPERTY(Replicated)
	bool bActivePointContested = false;

	// Local HUDs cannot query the server-only Lyra phase map.
	UPROPERTY(Replicated)
	bool bMatchEnded = false;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> ControlPoints;

	TMap<TWeakObjectPtr<AActor>, FTransform> OriginalTransforms;
	FGameplayMessageListenerHandle CaptureListener;
	FTimerHandle DiscoverTimer;
	FTimerHandle RotationTimer;

	UPROPERTY(EditDefaultsOnly, Category="Hero Arena|Objective", meta=(ClampMin="5.0", Units="s"))
	float HoldDuration = 35.0f;

	UPROPERTY(EditDefaultsOnly, Category="Hero Arena|Objective", meta=(ClampMin="1.0", Units="s"))
	float TransitionDuration = 6.0f;
};
