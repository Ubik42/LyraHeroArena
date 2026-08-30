#pragma once

#include "AbilitySystem/Abilities/LyraGameplayAbility.h"

#include "HeroArenaGameplayAbility_Blink.generated.h"

class UAnimMontage;
class UHeroArenaBlinkChargeComponent;

/** Short forward blink predicted locally and independently swept by the authoritative server. */
UCLASS()
class HEROARENARUNTIME_API UHeroArenaGameplayAbility_Blink : public ULyraGameplayAbility
{
	GENERATED_BODY()

public:
	UHeroArenaGameplayAbility_Blink(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual bool CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags,
		FGameplayTagContainer* OptionalRelevantTags) const override;

	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

private:
	UHeroArenaBlinkChargeComponent* GetChargeComponent(const FGameplayAbilityActorInfo* ActorInfo) const;

	UPROPERTY(EditDefaultsOnly, Category="HeroArena|Blink", meta=(ClampMin="100.0"))
	float BlinkDistance = 700.0f;

	UPROPERTY(EditDefaultsOnly, Category="HeroArena|Blink", meta=(ClampMin="0.0"))
	float WallClearance = 8.0f;

	UPROPERTY(EditDefaultsOnly, Category="HeroArena|Blink", meta=(ClampMin="0.0"))
	float MinimumTravelDistance = 80.0f;

	UPROPERTY(EditDefaultsOnly, Category="HeroArena|Blink")
	TObjectPtr<UAnimMontage> BlinkMontage;
};
