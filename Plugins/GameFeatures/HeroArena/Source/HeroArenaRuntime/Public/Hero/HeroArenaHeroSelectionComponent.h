#pragma once

#include "AbilitySystem/LyraAbilitySet.h"
#include "Components/PlayerStateComponent.h"
#include "GameFramework/GameplayMessageSubsystem.h"

#include "HeroArenaHeroSelectionComponent.generated.h"

class UHeroArenaHeroDefinition;
class UHeroArenaHeroRoster;
class APlayerState;
class APawn;
struct FLyraVerbMessage;

UENUM(BlueprintType)
enum class EHeroArenaHeroSelectionResult : uint8
{
	Success UMETA(DisplayName = "选择成功"),
	AlreadySelected UMETA(DisplayName = "已选择该英雄"),
	InvalidHero UMETA(DisplayName = "英雄不可用"),
	SelectionLocked UMETA(DisplayName = "选择已锁定"),
	RosterUnavailable UMETA(DisplayName = "英雄列表不可用"),
	AbilitySystemUnavailable UMETA(DisplayName = "角色系统尚未就绪")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FHeroArenaHeroChanged, const UHeroArenaHeroDefinition*, OldHero, const UHeroArenaHeroDefinition*, NewHero);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FHeroArenaSelectionLockChanged, bool, bIsLocked);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FHeroArenaSelectionRequestResolved, FName, HeroId, EHeroArenaHeroSelectionResult, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FHeroArenaUltimateChargeChanged, float, ChargePercent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FHeroArenaPawnChanged, APlayerState*, PlayerState, APawn*, NewPawn, APawn*, OldPawn);

/**
 * Server-authoritative hero selection that lives on PlayerState, so the selected
 * hero and its grants survive pawn death and respawn.
 */
UCLASS(Blueprintable, Meta = (BlueprintSpawnableComponent))
class HEROARENARUNTIME_API UHeroArenaHeroSelectionComponent : public UPlayerStateComponent
{
	GENERATED_BODY()

public:
	UHeroArenaHeroSelectionComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UFUNCTION(BlueprintPure, Category = "Hero Arena|Selection")
	const UHeroArenaHeroDefinition* GetSelectedHero() const { return SelectedHero; }

	UFUNCTION(BlueprintPure, Category = "Hero Arena|Selection")
	const UHeroArenaHeroRoster* GetRoster() const { return Roster; }

	UFUNCTION(BlueprintPure, Category = "Hero Arena|Selection")
	bool IsSelectionLocked() const { return bSelectionLocked; }

	UFUNCTION(BlueprintPure, Category = "Hero Arena|Ultimate")
	float GetUltimateCharge() const { return UltimateCharge; }

	UFUNCTION(BlueprintPure, Category = "Hero Arena|Ultimate")
	bool IsUltimateReady() const { return UltimateCharge >= MaxUltimateCharge - KINDA_SMALL_NUMBER; }

	/** Server-authoritative combat reward. Damage and healing pass their effective magnitude. */
	void AddUltimateCharge(float Amount, const TCHAR* Reason);
	bool TryConsumeUltimateCharge();

	/** Compact local-team summary built from replicated PlayerStates and their selected heroes. */
	UFUNCTION(BlueprintPure, Category = "Hero Arena|Selection")
	FText GetTeamCompositionSummary() const;

	/** Ordered list of the heroes currently selected by the local player's team. */
	UFUNCTION(BlueprintPure, Category = "Hero Arena|Selection")
	FText GetTeamHeroList() const;

	UFUNCTION(BlueprintPure, Category = "Hero Arena|Selection")
	static FText GetSelectionResultText(EHeroArenaHeroSelectionResult Result);

	/** Client-safe entry point. The owning client request is always revalidated by the server. */
	UFUNCTION(BlueprintCallable, Category = "Hero Arena|Selection")
	void RequestHeroSelection(FName HeroId);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Hero Arena|Selection")
	bool SelectHeroById(FName HeroId);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Hero Arena|Selection")
	void SetSelectionLocked(bool bLocked);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Hero Arena|Selection")
	void SetRoster(const UHeroArenaHeroRoster* NewRoster);

	UPROPERTY(BlueprintAssignable, Category = "Hero Arena|Selection")
	FHeroArenaHeroChanged OnHeroChanged;

	UPROPERTY(BlueprintAssignable, Category = "Hero Arena|Selection")
	FHeroArenaSelectionLockChanged OnSelectionLockChanged;

	/** Owning-client acknowledgement for both successful and rejected requests. */
	UPROPERTY(BlueprintAssignable, Category = "Hero Arena|Selection")
	FHeroArenaSelectionRequestResolved OnSelectionRequestResolved;

	UPROPERTY(BlueprintAssignable, Category = "Hero Arena|Ultimate")
	FHeroArenaUltimateChargeChanged OnUltimateChargeChanged;

	/** Replicated pawn transition used by event-driven teammate HUD elements. */
	UPROPERTY(BlueprintAssignable, Category = "Hero Arena|Selection")
	FHeroArenaPawnChanged OnPawnChanged;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(Server, Reliable)
	void ServerRequestHeroSelection(FName HeroId);

	UFUNCTION(Client, Reliable)
	void ClientNotifyHeroSelectionResult(FName HeroId, EHeroArenaHeroSelectionResult Result);

	UFUNCTION()
	void OnRep_SelectedHero(const UHeroArenaHeroDefinition* OldHero);

	UFUNCTION()
	void OnRep_SelectionLocked();

	UFUNCTION()
	void OnRep_UltimateCharge();

	UFUNCTION()
	void HandlePawnSet(APlayerState* PlayerState, APawn* NewPawn, APawn* OldPawn);

private:
	EHeroArenaHeroSelectionResult TrySelectHeroById(FName HeroId);
	void BroadcastSelectionResult(FName HeroId, EHeroArenaHeroSelectionResult Result);
	bool ApplySelectedHero(const UHeroArenaHeroDefinition* NewHero);
	bool ApplySelectedHeroLoadout(APawn* Pawn);
	void HandlePawnAbilitySystemInitialized();
	void RemoveGrantedAbilitySets();
	void HandleDamageMessage(FGameplayTag Channel, const FLyraVerbMessage& Payload);
	void HandleEliminationMessage(FGameplayTag Channel, const FLyraVerbMessage& Payload);
	void HandleAssistMessage(FGameplayTag Channel, const FLyraVerbMessage& Payload);
	void SetUltimateCharge(float NewCharge, const TCHAR* Reason);

	UPROPERTY(EditDefaultsOnly, Category = "Hero Arena|Selection")
	TObjectPtr<const UHeroArenaHeroRoster> Roster;

	UPROPERTY(ReplicatedUsing = OnRep_SelectedHero)
	TObjectPtr<const UHeroArenaHeroDefinition> SelectedHero;

	UPROPERTY(ReplicatedUsing = OnRep_SelectionLocked)
	bool bSelectionLocked = false;

	UPROPERTY(ReplicatedUsing = OnRep_UltimateCharge)
	float UltimateCharge = 0.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Hero Arena|Ultimate", meta = (ClampMin = "1.0"))
	float MaxUltimateCharge = 100.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Hero Arena|Ultimate", meta = (ClampMin = "0.0"))
	float DamageChargeScale = 0.35f;

	UPROPERTY(EditDefaultsOnly, Category = "Hero Arena|Ultimate", meta = (ClampMin = "0.0"))
	float EliminationCharge = 8.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Hero Arena|Ultimate", meta = (ClampMin = "0.0"))
	float AssistCharge = 4.0f;

	UPROPERTY(Transient)
	TArray<FLyraAbilitySet_GrantedHandles> GrantedAbilitySetHandles;

	TWeakObjectPtr<APawn> PendingLoadoutPawn;
	FGameplayMessageListenerHandle DamageListenerHandle;
	FGameplayMessageListenerHandle EliminationListenerHandle;
	FGameplayMessageListenerHandle AssistListenerHandle;
};
