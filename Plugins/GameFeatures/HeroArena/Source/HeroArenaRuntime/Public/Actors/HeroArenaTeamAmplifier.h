#pragma once

#include "AbilitySystemInterface.h"
#include "GameFramework/Actor.h"
#include "Teams/LyraTeamAgentInterface.h"

#include "HeroArenaTeamAmplifier.generated.h"

class ULyraAbilitySystemComponent;
class ULyraHealthSet;
class UHeroArenaHeroDefinition;
class UNiagaraComponent;
class USceneComponent;
class USphereComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
struct FGameplayEffectSpec;

/** Destructible Relay ultimate that refreshes a team damage aura. */
UCLASS()
class HEROARENARUNTIME_API AHeroArenaTeamAmplifier : public AActor,
	public IAbilitySystemInterface, public ILyraTeamAgentInterface
{
	GENERATED_BODY()
public:
	AHeroArenaTeamAmplifier(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	virtual void SetGenericTeamId(const FGenericTeamId& NewTeamID) override;
	virtual FGenericTeamId GetGenericTeamId() const override { return MyTeamID; }
	virtual FOnLyraTeamIndexChangedDelegate* GetOnTeamIndexChangedDelegate() override { return &OnTeamChangedDelegate; }

protected:
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	void RefreshAura();
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
	void RefreshPresentation();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> AmplifierMesh;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USphereComponent> AuraVolume;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UNiagaraComponent> AuraFx;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UTextRenderComponent> StatusText;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<ULyraAbilitySystemComponent> AbilitySystemComponent;
	UPROPERTY()
	TObjectPtr<const ULyraHealthSet> HealthSet;

	UPROPERTY(ReplicatedUsing=OnRep_Team)
	FGenericTeamId MyTeamID = FGenericTeamId::NoTeam;
	UPROPERTY(ReplicatedUsing=OnRep_EndTime)
	float ServerEndTime = 0.0f;
	UPROPERTY()
	FOnLyraTeamIndexChangedDelegate OnTeamChangedDelegate;

	UPROPERTY(EditDefaultsOnly, Category="Hero Arena|Relay|Team Amplifier")
	float MaxHealth = 400.0f;
	UPROPERTY(EditDefaultsOnly, Category="Hero Arena|Relay|Team Amplifier", meta=(Units="cm"))
	float AuraRadius = 700.0f;
	UPROPERTY(EditDefaultsOnly, Category="Hero Arena|Relay|Team Amplifier", meta=(Units="s"))
	float Duration = 10.0f;
	UPROPERTY(EditDefaultsOnly, Category="Hero Arena|Relay|Team Amplifier", meta=(Units="s"))
	float RefreshInterval = 0.5f;
	FTimerHandle AuraTimer;
	bool bLoggedFirstActiveAura = false;
};
