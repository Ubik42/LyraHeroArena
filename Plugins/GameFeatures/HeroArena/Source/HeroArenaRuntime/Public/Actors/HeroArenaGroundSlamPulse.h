#pragma once

#include "GameFramework/Actor.h"

#include "HeroArenaGroundSlamPulse.generated.h"

class UNiagaraComponent;
class USceneComponent;
class UTextRenderComponent;

/** Short replicated presentation actor for Bulwark's server-authoritative ground slam. */
UCLASS()
class HEROARENARUNTIME_API AHeroArenaGroundSlamPulse : public AActor
{
	GENERATED_BODY()

public:
	AHeroArenaGroundSlamPulse();
	void InitializePulse(int32 InTeamId, float InRadius);
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

private:
	UFUNCTION()
	void OnRep_State();

	void RefreshPresentation();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UNiagaraComponent> PulseFx;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UTextRenderComponent> StatusText;

	UPROPERTY(ReplicatedUsing=OnRep_State)
	int32 TeamId = INDEX_NONE;

	UPROPERTY(ReplicatedUsing=OnRep_State)
	float PulseRadius = 650.0f;
};
