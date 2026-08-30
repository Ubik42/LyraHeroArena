// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"

#include "HeroArenaWeaponOverloadIndicator.generated.h"

class UNiagaraComponent;
class USceneComponent;
class UTextRenderComponent;

/** Short-lived replicated presentation owned by the authoritative overload ability. */
UCLASS()
class HEROARENARUNTIME_API AHeroArenaWeaponOverloadIndicator : public AActor
{
	GENERATED_BODY()

public:
	AHeroArenaWeaponOverloadIndicator();

	void InitializeIndicator(AActor* InTargetActor, float InDuration);

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void OnRep_IndicatorState();

	void RefreshAttachment();
	float GetRemainingTime() const;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UNiagaraComponent> ElectricFx;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UTextRenderComponent> StatusText;

	UPROPERTY(ReplicatedUsing=OnRep_IndicatorState)
	TObjectPtr<AActor> TargetActor;

	UPROPERTY(ReplicatedUsing=OnRep_IndicatorState)
	float ServerEndTime = 0.0f;
};
