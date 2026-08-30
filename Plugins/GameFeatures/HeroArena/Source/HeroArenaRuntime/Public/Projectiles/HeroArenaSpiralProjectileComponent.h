// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"

#include "HeroArenaSpiralProjectileComponent.generated.h"

class UProjectileMovementComponent;
class AHeroArenaBarrier;

/**
 * Adds a server-authoritative helical velocity offset to an existing projectile.
 *
 * The component leaves spawning, normal collision, bouncing, replication,
 * detonation and Pawn damage to the owning projectile.  In addition to the
 * spiral-flight difference, it adapts direct enemy impacts against HeroArena's
 * damageable barrier because Lyra's grenade overlap intentionally targets Pawns.
 */
UCLASS(ClassGroup=(HeroArena), BlueprintType, Blueprintable, meta=(BlueprintSpawnableComponent))
class HEROARENARUNTIME_API UHeroArenaSpiralProjectileComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHeroArenaSpiralProjectileComponent();

	/** Apply the HeroArena-only direct-hit adapter from the barrier's proven Actor hit callback. */
	bool TryApplyBarrierImpact(AHeroArenaBarrier* Barrier, const FHitResult& Hit);

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void RebuildFrame(const FVector& ForwardDirection);

	/** Approximate radius of the helix in Unreal units. */
	UPROPERTY(EditAnywhere, Category="Spiral", meta=(ClampMin="0.0", Units="cm"))
	float SpiralRadius = 32.0f;

	/** Number of complete turns per second. */
	UPROPERTY(EditAnywhere, Category="Spiral", meta=(ClampMin="0.0"))
	float TurnsPerSecond = 2.25f;

	/** Rebuild the spiral frame after a strong direction change such as a bounce. */
	UPROPERTY(EditAnywhere, Category="Spiral", meta=(ClampMin="-1.0", ClampMax="1.0"))
	float DirectionResetDotThreshold = 0.45f;

	/** Direct damage dealt when this projectile physically hits an enemy HeroArena barrier. */
	UPROPERTY(EditAnywhere, Category="Spiral", meta=(ClampMin="0.0"))
	float BarrierImpactDamage = 100.0f;

	UPROPERTY(Transient)
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

	FVector ForwardAxis = FVector::ForwardVector;
	FVector SpiralAxisX = FVector::RightVector;
	FVector SpiralAxisY = FVector::UpVector;
	FVector LastInjectedVelocity = FVector::ZeroVector;
	float ElapsedTime = 0.0f;
	bool bAppliedBarrierImpactDamage = false;
};
