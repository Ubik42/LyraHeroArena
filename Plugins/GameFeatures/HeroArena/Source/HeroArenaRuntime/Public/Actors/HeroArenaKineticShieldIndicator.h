#pragma once

#include "GameFramework/Actor.h"

#include "HeroArenaKineticShieldIndicator.generated.h"

class UNiagaraComponent;
class USceneComponent;
class UTextRenderComponent;

/** Replicated, non-colliding presentation and readable state for Bulwark's temporary shield. */
UCLASS()
class HEROARENARUNTIME_API AHeroArenaKineticShieldIndicator : public AActor
{
	GENERATED_BODY()

public:
	AHeroArenaKineticShieldIndicator();

	void InitializeIndicator(AActor* InTargetActor, float InShieldAmount, float InDuration);
	void SetShieldAmount(float InShieldAmount);

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

private:
	UFUNCTION()
	void OnRep_State();

	void RefreshPresentation();
	float GetRemainingTime() const;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UNiagaraComponent> ShieldFx;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UTextRenderComponent> StatusText;

	UPROPERTY(ReplicatedUsing=OnRep_State)
	TObjectPtr<AActor> TargetActor;

	UPROPERTY(ReplicatedUsing=OnRep_State)
	float CurrentShield = 0.0f;

	UPROPERTY(ReplicatedUsing=OnRep_State)
	float MaxShield = 0.0f;

	UPROPERTY(ReplicatedUsing=OnRep_State)
	float ServerEndTime = 0.0f;
};
