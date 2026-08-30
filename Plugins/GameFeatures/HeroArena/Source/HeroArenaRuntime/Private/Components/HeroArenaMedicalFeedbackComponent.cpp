#include "Components/HeroArenaMedicalFeedbackComponent.h"

#include "AbilitySystem/Attributes/LyraHealthSet.h"
#include "AbilitySystem/LyraAbilitySystemComponent.h"
#include "Effects/HeroArenaGameplayEffect_MedicalFeedbackCooldown.h"
#include "GameplayEffect.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Hero/HeroArenaHeroDefinition.h"
#include "Hero/HeroArenaHeroSelectionComponent.h"
#include "LyraGameplayTags.h"
#include "Player/LyraPlayerState.h"
#include "System/LyraAssetManager.h"
#include "System/LyraGameData.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaMedicalFeedbackComponent)

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaMedicalFeedback, Log, All);

namespace HeroArenaMedicalFeedback
{
	APlayerState* ResolvePlayerState(AActor* Actor)
	{
		if (APlayerState* PlayerState = Cast<APlayerState>(Actor))
		{
			return PlayerState;
		}
		if (const APawn* Pawn = Cast<APawn>(Actor))
		{
			return Pawn->GetPlayerState();
		}
		if (const AController* Controller = Cast<AController>(Actor))
		{
			return Controller->PlayerState;
		}
		return nullptr;
	}
}

UHeroArenaMedicalFeedbackComponent::UHeroArenaMedicalFeedbackComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UHeroArenaMedicalFeedbackComponent::BeginPlay()
{
	Super::BeginPlay();

	if (ALyraPlayerState* PlayerState = Cast<ALyraPlayerState>(GetOwner()))
	{
		if (ULyraAbilitySystemComponent* ASC = PlayerState->GetLyraAbilitySystemComponent())
		{
			if (const ULyraHealthSet* HealthSet = ASC->GetSet<ULyraHealthSet>())
			{
				BoundHealthSet = HealthSet;
				HealthSet->OnHealthChanged.AddUObject(this, &ThisClass::HandleHealthChanged);
			}
		}
	}
}

void UHeroArenaMedicalFeedbackComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (const ULyraHealthSet* HealthSet = BoundHealthSet.Get())
	{
		HealthSet->OnHealthChanged.RemoveAll(this);
	}
	BoundHealthSet.Reset();
	Super::EndPlay(EndPlayReason);
}

void UHeroArenaMedicalFeedbackComponent::HandleHealthChanged(AActor* Instigator, AActor* Causer,
	const FGameplayEffectSpec* EffectSpec, float Magnitude, float OldValue, float NewValue)
{
	ALyraPlayerState* TargetPlayerState = Cast<ALyraPlayerState>(GetOwner());
	if (!TargetPlayerState || !TargetPlayerState->HasAuthority() || NewValue <= OldValue)
	{
		return;
	}

	APlayerState* SourcePlayerState = HeroArenaMedicalFeedback::ResolvePlayerState(Instigator);
	if (!SourcePlayerState && EffectSpec)
	{
		SourcePlayerState = HeroArenaMedicalFeedback::ResolvePlayerState(EffectSpec->GetEffectContext().GetEffectCauser());
	}
	if (!SourcePlayerState || SourcePlayerState == TargetPlayerState)
	{
		return;
	}

	const UHeroArenaHeroSelectionComponent* Selection =
		SourcePlayerState->FindComponentByClass<UHeroArenaHeroSelectionComponent>();
	const UHeroArenaHeroDefinition* Hero = Selection ? Selection->GetSelectedHero() : nullptr;
	if (!Hero || Hero->HeroId != TEXT("Pulse"))
	{
		return;
	}

	ALyraPlayerState* SourceLyraPlayerState = Cast<ALyraPlayerState>(SourcePlayerState);
	ULyraAbilitySystemComponent* SourceASC = SourceLyraPlayerState
		? SourceLyraPlayerState->GetLyraAbilitySystemComponent() : nullptr;
	const ULyraHealthSet* SourceHealthSet = SourceASC ? SourceASC->GetSet<ULyraHealthSet>() : nullptr;
	const FGameplayTag CooldownTag = FGameplayTag::RequestGameplayTag(TEXT("Cooldown.Passive.MedicalFeedback"));
	if (!SourceASC || !SourceHealthSet || SourceHealthSet->GetHealth() <= 0.0f ||
		SourceHealthSet->GetHealth() >= SourceHealthSet->GetMaxHealth() || SourceASC->HasMatchingGameplayTag(CooldownTag))
	{
		return;
	}

	const float EffectiveHealing = NewValue - OldValue;
	const float FeedbackHealing = FMath::Min(EffectiveHealing * FeedbackRatio, MaxFeedbackPerTrigger);
	if (FeedbackHealing <= 0.0f)
	{
		return;
	}

	FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();
	const TSubclassOf<UGameplayEffect> HealEffect =
		ULyraAssetManager::GetSubclass(ULyraGameData::Get().HealGameplayEffect_SetByCaller);
	FGameplayEffectSpecHandle HealSpec = SourceASC->MakeOutgoingSpec(HealEffect, 1.0f, Context);
	if (!HealSpec.IsValid())
	{
		return;
	}
	HealSpec.Data->SetSetByCallerMagnitude(LyraGameplayTags::SetByCaller_Heal, FeedbackHealing);
	const float SourceHealthBefore = SourceHealthSet->GetHealth();
	SourceASC->ApplyGameplayEffectSpecToSelf(*HealSpec.Data.Get());
	const float AppliedFeedback = SourceHealthSet->GetHealth() - SourceHealthBefore;
	SourceASC->ApplyGameplayEffectToSelf(
		GetDefault<UHeroArenaGameplayEffect_MedicalFeedbackCooldown>(), 1.0f, Context);
	if (AppliedFeedback > 0.0f)
	{
		FGameplayCueParameters CueParameters;
		CueParameters.Instigator = SourcePlayerState->GetPawn();
		CueParameters.EffectCauser = SourcePlayerState->GetPawn();
		CueParameters.Location = SourcePlayerState->GetPawn() ? SourcePlayerState->GetPawn()->GetActorLocation() : FVector::ZeroVector;
		SourceASC->ExecuteGameplayCue(
			FGameplayTag::RequestGameplayTag(TEXT("GameplayCue.Character.Heal")), CueParameters);
	}

	UE_LOG(LogHeroArenaMedicalFeedback, Log,
		TEXT("Medical feedback resolved: source=%s target=%s effective_heal=%.2f feedback=%.2f source_health=%.2f"),
		*GetNameSafe(SourcePlayerState->GetPawn()), *GetNameSafe(TargetPlayerState->GetPawn()),
		EffectiveHealing, AppliedFeedback, SourceHealthSet->GetHealth());
}
