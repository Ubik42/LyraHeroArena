#pragma once

#include "AbilitySystem/LyraAbilitySet.h"
#include "Components/PlayerStateComponent.h"

#include "HeroArenaHeroSelectionComponent.generated.h"

class UHeroArenaHeroDefinition;
class UHeroArenaHeroRoster;
class APlayerState;
class APawn;

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
	void HandlePawnSet(APlayerState* PlayerState, APawn* NewPawn, APawn* OldPawn);

private:
	EHeroArenaHeroSelectionResult TrySelectHeroById(FName HeroId);
	void BroadcastSelectionResult(FName HeroId, EHeroArenaHeroSelectionResult Result);
	bool ApplySelectedHero(const UHeroArenaHeroDefinition* NewHero);
	bool ApplySelectedHeroLoadout(APawn* Pawn);
	void HandlePawnAbilitySystemInitialized();
	void RemoveGrantedAbilitySets();

	UPROPERTY(EditDefaultsOnly, Category = "Hero Arena|Selection")
	TObjectPtr<const UHeroArenaHeroRoster> Roster;

	UPROPERTY(ReplicatedUsing = OnRep_SelectedHero)
	TObjectPtr<const UHeroArenaHeroDefinition> SelectedHero;

	UPROPERTY(ReplicatedUsing = OnRep_SelectionLocked)
	bool bSelectionLocked = false;

	UPROPERTY(Transient)
	TArray<FLyraAbilitySet_GrantedHandles> GrantedAbilitySetHandles;

	TWeakObjectPtr<APawn> PendingLoadoutPawn;
};
