#pragma once

#include "ActiveGameplayEffectHandle.h"
#include "GameFramework/Actor.h"
#include "HeroArenaStickyBomb.generated.h"

class UAbilitySystemComponent;
class UNiagaraComponent;
class UProjectileMovementComponent;
class USphereComponent;
class UStaticMeshComponent;

/** Server-authoritative projectile that sticks to enemies or the world and detonates exactly once. */
UCLASS()
class HEROARENARUNTIME_API AHeroArenaStickyBomb : public AActor
{
	GENERATED_BODY()

public:
	AHeroArenaStickyBomb();
	void InitializeBomb(UAbilitySystemComponent* InSourceASC, AActor* InSourceTeamAgent, const FVector& Velocity);
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

private:
	UFUNCTION()
	void OnCollisionHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent,
		FVector NormalImpulse, const FHitResult& Hit);
	UFUNCTION()
	void OnRep_AttachmentState();
	UFUNCTION()
	void OnRep_Detonated();

	void StickToEnemy(AActor* Target, UAbilitySystemComponent* TargetASC);
	void StickToWorld(const FHitResult& Hit);
	void CheckAttachedState();
	void Detonate();
	void Disarm(const TCHAR* Reason);
	void ClearAttachedEffect();
	bool HasLineOfSightTo(AActor* Target) const;
	void ApplyDamage(AActor* Target, UAbilitySystemComponent* TargetASC);
	void ApplyReplicatedAttachment();
	void PlayDetonationFeedback();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USphereComponent> Collision;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> BombMesh;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UNiagaraComponent> TrailFx;

	UPROPERTY(ReplicatedUsing=OnRep_AttachmentState)
	TObjectPtr<AActor> StuckTarget;
	UPROPERTY(ReplicatedUsing=OnRep_AttachmentState)
	FVector_NetQuantize100 StuckRelativeLocation = FVector::ZeroVector;
	UPROPERTY(ReplicatedUsing=OnRep_AttachmentState)
	bool bStuck = false;
	UPROPERTY(ReplicatedUsing=OnRep_Detonated)
	bool bDetonated = false;
	UPROPERTY(Replicated)
	float DetonationServerTime = 0.0f;

	TWeakObjectPtr<UAbilitySystemComponent> SourceASC;
	TWeakObjectPtr<AActor> SourceTeamAgent;
	TWeakObjectPtr<UAbilitySystemComponent> AttachedTargetASC;
	FActiveGameplayEffectHandle AttachedEffectHandle;
	FTimerHandle FuseTimer;
	FTimerHandle AttachmentCheckTimer;
	float EnemyFuseSeconds = 1.5f;
	float WorldFuseSeconds = 1.2f;
	float ExplosionRadius = 325.0f;
	float ExplosionDamage = 90.0f;
};
