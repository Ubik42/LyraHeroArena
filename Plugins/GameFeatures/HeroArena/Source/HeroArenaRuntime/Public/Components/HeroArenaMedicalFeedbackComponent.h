#pragma once

#include "Components/PlayerStateComponent.h"
#include "HeroArenaMedicalFeedbackComponent.generated.h"

class ULyraHealthSet;
struct FGameplayEffectSpec;

/** Observes final server health deltas and rewards Pulse for effective healing without coupling individual heal sources. */
UCLASS(Blueprintable, Meta=(BlueprintSpawnableComponent))
class HEROARENARUNTIME_API UHeroArenaMedicalFeedbackComponent : public UPlayerStateComponent
{
	GENERATED_BODY()

public:
	UHeroArenaMedicalFeedbackComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void HandleHealthChanged(AActor* Instigator, AActor* Causer, const FGameplayEffectSpec* EffectSpec,
		float Magnitude, float OldValue, float NewValue);

	TWeakObjectPtr<const ULyraHealthSet> BoundHealthSet;

	UPROPERTY(EditDefaultsOnly, Category="HeroArena|MedicalFeedback")
	float FeedbackRatio = 0.25f;

	UPROPERTY(EditDefaultsOnly, Category="HeroArena|MedicalFeedback")
	float MaxFeedbackPerTrigger = 5.0f;
};
