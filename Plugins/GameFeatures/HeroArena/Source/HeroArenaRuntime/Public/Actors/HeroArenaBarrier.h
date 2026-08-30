#pragma once

#include "AbilitySystemInterface.h"
#include "GameFramework/Actor.h"
#include "Teams/LyraTeamAgentInterface.h"

#include "HeroArenaBarrier.generated.h"

class ULyraAbilitySystemComponent;
class ULyraHealthSet;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UNiagaraSystem;
class UPrimitiveComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
struct FGameplayEffectSpec;

/**
 * Server-owned, team-aware frontal barrier that participates in Lyra's normal
 * weapon trace, GameplayEffect damage and attribute replication pipeline.
 */
UCLASS()
class HEROARENARUNTIME_API AHeroArenaBarrier : public AActor, public IAbilitySystemInterface, public ILyraTeamAgentInterface
{
	GENERATED_BODY()

public:
	AHeroArenaBarrier(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	virtual void SetGenericTeamId(const FGenericTeamId& NewTeamID) override;
	virtual FGenericTeamId GetGenericTeamId() const override { return MyTeamID; }
	virtual FOnLyraTeamIndexChangedDelegate* GetOnTeamIndexChangedDelegate() override { return &OnTeamChangedDelegate; }
	virtual void NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp,
		bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit) override;

	UFUNCTION(BlueprintPure, Category="Hero Arena|Barrier")
	float GetBarrierHealth() const;

	UFUNCTION(BlueprintPure, Category="Hero Arena|Barrier")
	float GetBarrierMaxHealth() const;

protected:
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Destroyed() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	void HandleHealthChanged(AActor* DamageInstigator, AActor* DamageCauser, const FGameplayEffectSpec* DamageEffectSpec,
		float DamageMagnitude, float OldValue, float NewValue);
	void HandleOutOfHealth(AActor* DamageInstigator, AActor* DamageCauser, const FGameplayEffectSpec* DamageEffectSpec,
		float DamageMagnitude, float OldValue, float NewValue);
	void PlayDamageFeedback();
	void RestoreTeamColor();
	FLinearColor GetTeamColor() const;
	void RefreshPresentation();
	void RefreshLocalCameraOcclusion();

	UFUNCTION()
	void OnRep_MyTeamID(FGenericTeamId OldTeamID);

	UPROPERTY(VisibleAnywhere, Category="Barrier")
	TObjectPtr<UStaticMeshComponent> BarrierMesh;

	UPROPERTY(VisibleAnywhere, Category="Barrier")
	TObjectPtr<UTextRenderComponent> StatusText;

	UPROPERTY(VisibleAnywhere, Category="Barrier")
	TObjectPtr<ULyraAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY()
	TObjectPtr<const ULyraHealthSet> HealthSet;

	UPROPERTY(EditDefaultsOnly, Category="Barrier", meta=(ClampMin="1.0"))
	float MaxBarrierHealth = 350.0f;

	UPROPERTY(EditDefaultsOnly, Category="Barrier|Feedback")
	TSoftObjectPtr<UNiagaraSystem> BarrierImpactEffectAsset;

	UPROPERTY(EditDefaultsOnly, Category="Barrier|Feedback")
	TSoftObjectPtr<UNiagaraSystem> BarrierBreakEffectAsset;

	UPROPERTY(EditDefaultsOnly, Category="Barrier|Feedback")
	TSoftObjectPtr<UMaterialInterface> BarrierMaterialAsset;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraSystem> BarrierImpactEffect;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraSystem> BarrierBreakEffect;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BarrierMaterial;

	UPROPERTY(ReplicatedUsing=OnRep_MyTeamID)
	FGenericTeamId MyTeamID = FGenericTeamId::NoTeam;

	UPROPERTY()
	FOnLyraTeamIndexChangedDelegate OnTeamChangedDelegate;

	FTimerHandle DamageFlashTimer;
	FTimerHandle LocalCameraOcclusionTimer;
	double LastImpactFeedbackTime = -1.0;
	bool bHiddenForLocalCamera = false;
};
