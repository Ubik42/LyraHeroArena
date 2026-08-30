#pragma once

#include "AbilitySystemInterface.h"
#include "GameFramework/Actor.h"
#include "Teams/LyraTeamAgentInterface.h"
#include "HeroArenaRelayBeacon.generated.h"

class ULyraAbilitySystemComponent;
class ULyraHealthSet;
class UHeroArenaHeroDefinition;
class UNiagaraComponent;
class UNiagaraSystem;
class USceneComponent;
class USphereComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
struct FGameplayEffectSpec;

/** Destructible server-owned support beacon combining Lyra health, teams and canonical healing. */
UCLASS()
class HEROARENARUNTIME_API AHeroArenaRelayBeacon : public AActor,
	public IAbilitySystemInterface, public ILyraTeamAgentInterface
{
	GENERATED_BODY()

public:
	AHeroArenaRelayBeacon(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	virtual void SetGenericTeamId(const FGenericTeamId& NewTeamID) override;
	virtual FGenericTeamId GetGenericTeamId() const override { return MyTeamID; }
	virtual FOnLyraTeamIndexChangedDelegate* GetOnTeamIndexChangedDelegate() override { return &OnTeamChangedDelegate; }

	bool RecallBy(AActor* Requester);
	float GetBeaconHealth() const;
	float GetBeaconMaxHealth() const;

protected:
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Destroyed() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	void PulseSupport();
	void RefreshPresentation();
	void HandleHealthChanged(AActor* DamageInstigator, AActor* DamageCauser,
		const FGameplayEffectSpec* DamageEffectSpec, float DamageMagnitude, float OldValue, float NewValue);
	void HandleOutOfHealth(AActor* DamageInstigator, AActor* DamageCauser,
		const FGameplayEffectSpec* DamageEffectSpec, float DamageMagnitude, float OldValue, float NewValue);
	UFUNCTION()
	void HandleOwnerHeroChanged(const UHeroArenaHeroDefinition* OldHero, const UHeroArenaHeroDefinition* NewHero);

	UFUNCTION()
	void OnRep_Team(FGenericTeamId OldTeamID);
	UFUNCTION()
	void OnRep_EndTime();
	UFUNCTION()
	void OnRep_SupportPulse();
	UFUNCTION(NetMulticast, Reliable)
	void MulticastMarkRecalled();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> BeaconMesh;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USphereComponent> SupportVolume;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UTextRenderComponent> StatusText;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UNiagaraComponent> SupportFx;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<ULyraAbilitySystemComponent> AbilitySystemComponent;
	UPROPERTY()
	TObjectPtr<const ULyraHealthSet> HealthSet;

	UPROPERTY(ReplicatedUsing=OnRep_Team)
	FGenericTeamId MyTeamID = FGenericTeamId::NoTeam;
	UPROPERTY(ReplicatedUsing=OnRep_EndTime)
	float ServerEndTime = 0.0f;
	UPROPERTY(ReplicatedUsing=OnRep_SupportPulse)
	uint8 SupportPulseSequence = 0;
	UPROPERTY()
	FOnLyraTeamIndexChangedDelegate OnTeamChangedDelegate;

	UPROPERTY(EditDefaultsOnly, Category="Relay Beacon")
	float MaxBeaconHealth = 250.0f;
	UPROPERTY(EditDefaultsOnly, Category="Relay Beacon")
	float SupportRadius = 500.0f;
	UPROPERTY(EditDefaultsOnly, Category="Relay Beacon")
	float Duration = 15.0f;
	UPROPERTY(EditDefaultsOnly, Category="Relay Beacon")
	float PulseInterval = 1.0f;
	UPROPERTY(EditDefaultsOnly, Category="Relay Beacon")
	float HealPerPulse = 4.0f;

	TSoftObjectPtr<UNiagaraSystem> BreakEffectAsset;
	FTimerHandle SupportTimer;
	bool bRecalled = false;
};
