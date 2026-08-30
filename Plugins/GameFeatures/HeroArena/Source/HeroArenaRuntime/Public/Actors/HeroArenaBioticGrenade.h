#pragma once

#include "GameFramework/Actor.h"
#include "HeroArenaBioticGrenade.generated.h"

class UNiagaraComponent;
class UProjectileMovementComponent;
class USphereComponent;
class UStaticMeshComponent;

/** Replicated Pulse grenade; only the server assigns team-dependent status effects. */
UCLASS()
class HEROARENARUNTIME_API AHeroArenaBioticGrenade : public AActor
{
	GENERATED_BODY()
public:
	AHeroArenaBioticGrenade();
	void InitializeVelocity(const FVector& Velocity);
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
protected:
	virtual void BeginPlay() override;
private:
	UFUNCTION() void OnProjectileBounce(const FHitResult& ImpactResult, const FVector& ImpactVelocity);
	UFUNCTION() void OnRep_Detonated();
	void Detonate();
	void PlayDetonationFeedback();

	UPROPERTY(VisibleAnywhere) TObjectPtr<USphereComponent> Collision;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> GrenadeMesh;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UProjectileMovementComponent> ProjectileMovement;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UNiagaraComponent> TrailFx;
	UPROPERTY(ReplicatedUsing=OnRep_Detonated) bool bDetonated = false;
	FTimerHandle FuseTimer;
	float Radius = 500.0f;
};
