#pragma once

#include "GameFramework/Actor.h"
#include "Teams/LyraTeamAgentInterface.h"

#include "HeroArenaRelayJumpPad.generated.h"

class UHeroArenaHeroDefinition;
class ACharacter;
class UNiagaraComponent;
class USceneComponent;
class USphereComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

/** Replicated Relay deployable that launches allies forward and rejects enemies. */
UCLASS()
class HEROARENARUNTIME_API AHeroArenaRelayJumpPad : public AActor, public ILyraTeamAgentInterface
{
	GENERATED_BODY()

public:
	AHeroArenaRelayJumpPad(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual void SetGenericTeamId(const FGenericTeamId& NewTeamID) override;
	virtual FGenericTeamId GetGenericTeamId() const override { return MyTeamID; }
	virtual FOnLyraTeamIndexChangedDelegate* GetOnTeamIndexChangedDelegate() override { return &OnTeamChangedDelegate; }

	bool RecallBy(AActor* Requester);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UFUNCTION()
	void HandleOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep,
		const FHitResult& SweepResult);
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
	TObjectPtr<UStaticMeshComponent> PadMesh;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USphereComponent> LaunchVolume;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UNiagaraComponent> LaunchFx;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UTextRenderComponent> StatusText;

	UPROPERTY(ReplicatedUsing=OnRep_Team)
	FGenericTeamId MyTeamID = FGenericTeamId::NoTeam;
	UPROPERTY(ReplicatedUsing=OnRep_EndTime)
	float ServerEndTime = 0.0f;
	UPROPERTY()
	FOnLyraTeamIndexChangedDelegate OnTeamChangedDelegate;

	UPROPERTY(EditDefaultsOnly, Category="Hero Arena|Relay|Jump Pad", meta=(ClampMin="1.0", Units="s"))
	float Duration = 18.0f;
	UPROPERTY(EditDefaultsOnly, Category="Hero Arena|Relay|Jump Pad", meta=(ClampMin="0.0", Units="cm/s"))
	float AllyForwardSpeed = 900.0f;
	UPROPERTY(EditDefaultsOnly, Category="Hero Arena|Relay|Jump Pad", meta=(ClampMin="0.0", Units="cm/s"))
	float AllyUpSpeed = 850.0f;
	UPROPERTY(EditDefaultsOnly, Category="Hero Arena|Relay|Jump Pad", meta=(ClampMin="0.0", Units="cm/s"))
	float EnemyPushSpeed = 650.0f;
	UPROPERTY(EditDefaultsOnly, Category="Hero Arena|Relay|Jump Pad", meta=(ClampMin="0.0", Units="cm/s"))
	float EnemyUpSpeed = 420.0f;
	UPROPERTY(EditDefaultsOnly, Category="Hero Arena|Relay|Jump Pad", meta=(ClampMin="0.0", Units="s"))
	float PerCharacterRelaunchDelay = 2.0f;

	TMap<TWeakObjectPtr<ACharacter>, float> NextLaunchTimeByCharacter;
};
