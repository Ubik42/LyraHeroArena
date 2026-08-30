#pragma once

#include "GameFramework/Actor.h"

#include "HeroArenaGroundSlamSlow.generated.h"

class UNiagaraComponent;
class USceneComponent;
class UTextRenderComponent;

/** Narrow replicated movement modifier used only by Ground Slam; not a general status framework. */
UCLASS()
class HEROARENARUNTIME_API AHeroArenaGroundSlamSlow : public AActor
{
	GENERATED_BODY()

public:
	AHeroArenaGroundSlamSlow();
	void InitializeSlow(AActor* InTargetActor, float InMultiplier, float InDuration);
	AActor* GetTargetActor() const { return TargetActor; }
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void OnRep_State();

	void ApplySlowLocally();
	void RestoreSpeedLocally();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UNiagaraComponent> SlowFx;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UTextRenderComponent> StatusText;

	UPROPERTY(ReplicatedUsing=OnRep_State)
	TObjectPtr<AActor> TargetActor;

	UPROPERTY(ReplicatedUsing=OnRep_State)
	float SpeedMultiplier = 0.55f;

	UPROPERTY(ReplicatedUsing=OnRep_State)
	float ServerEndTime = 0.0f;

	float OriginalMaxWalkSpeed = 0.0f;
	float AppliedMaxWalkSpeed = 0.0f;
	bool bAppliedLocally = false;
};
