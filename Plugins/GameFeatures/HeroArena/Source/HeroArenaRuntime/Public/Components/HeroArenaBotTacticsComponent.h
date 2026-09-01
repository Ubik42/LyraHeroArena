#pragma once

#include "Components/PlayerStateComponent.h"

#include "HeroArenaBotTacticsComponent.generated.h"

class ALyraPlayerState;
class UHeroArenaHeroDefinition;
class ULyraAbilitySystemComponent;

/** Server-only role decisions layered over Lyra's existing objective/combat bot tree. */
UCLASS(Meta=(BlueprintSpawnableComponent))
class HEROARENARUNTIME_API UHeroArenaBotTacticsComponent : public UPlayerStateComponent
{
	GENERATED_BODY()

public:
	UHeroArenaBotTacticsComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void EvaluateTactics();
	bool EnsureBalancedHero(ALyraPlayerState* PlayerState);
	bool TryActivate(ULyraAbilitySystemComponent* AbilitySystem, const TCHAR* AbilityTag, const TCHAR* Reason);
	void AimAt(AActor* Target) const;
	void AimForPhaseFlank(AActor* Target, float SideSign) const;
	float GetHealthRatio(const ALyraPlayerState* PlayerState) const;
	bool IsViableTarget(const ALyraPlayerState* PlayerState) const;

	FTimerHandle TacticsTimer;
	double NextHeroAssignmentAttempt = 0.0;
	double NextDecisionTime = 0.0;
	double NextPhaseBlinkTime = 0.0;
	double NextRelayBeaconAttemptTime = 0.0;
	double NextRelayJumpPadAttemptTime = 0.0;
	float NextPhaseFlankSide = 1.0f;
};
