// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"

#include "HeroArenaRecoveryField.generated.h"

class UNiagaraComponent;
class USceneComponent;
class USphereComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

/** Server-authoritative, replicated healing area deployed by Striker. */
UCLASS()
class HEROARENARUNTIME_API AHeroArenaRecoveryField : public AActor
{
	GENERATED_BODY()

public:
	AHeroArenaRecoveryField();

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category="Recovery Field")
	float GetRemainingTime() const;

	UFUNCTION(BlueprintPure, Category="Recovery Field")
	int32 GetFieldTeamId() const { return FieldTeamId; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void PulseHealing();
	void RefreshPresentation();

	UFUNCTION()
	void OnRep_FieldState();

	UFUNCTION()
	void OnRep_HealingPulse();

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<USphereComponent> HealingVolume;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UStaticMeshComponent> PadMesh;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UTextRenderComponent> StatusText;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UNiagaraComponent> HealingPulseFx;

	UPROPERTY(EditDefaultsOnly, Category="Recovery Field", meta=(ClampMin="50.0", Units="cm"))
	float HealingRadius = 450.0f;

	UPROPERTY(EditDefaultsOnly, Category="Recovery Field", meta=(ClampMin="0.1", Units="s"))
	float FieldDuration = 6.0f;

	UPROPERTY(EditDefaultsOnly, Category="Recovery Field", meta=(ClampMin="0.1", Units="s"))
	float HealingInterval = 1.0f;

	UPROPERTY(EditDefaultsOnly, Category="Recovery Field", meta=(ClampMin="0.0"))
	float HealPerPulse = 8.0f;

	UPROPERTY(ReplicatedUsing=OnRep_FieldState)
	int32 FieldTeamId = INDEX_NONE;

	UPROPERTY(ReplicatedUsing=OnRep_FieldState)
	float ServerEndTime = 0.0f;

	/** Changes only when a pulse actually heals someone, so remote clients receive a compact feedback event. */
	UPROPERTY(ReplicatedUsing=OnRep_HealingPulse)
	uint8 HealingPulseSequence = 0;

	FTimerHandle HealingTimerHandle;
};
