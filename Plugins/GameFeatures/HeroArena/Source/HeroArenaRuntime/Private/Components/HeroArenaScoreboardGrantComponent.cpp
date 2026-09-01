#include "Components/HeroArenaScoreboardGrantComponent.h"

#include "Abilities/HeroArenaGameplayAbility_ShowScoreboard.h"
#include "AbilitySystem/Attributes/LyraHealthSet.h"
#include "AbilitySystem/LyraAbilitySystemComponent.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "Messages/LyraVerbMessage.h"
#include "Messages/LyraVerbMessageHelpers.h"
#include "Player/LyraPlayerState.h"
#include "Scoring/HeroArenaContribution.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaScoreboardGrantComponent)

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaScoreboardGrant, Log, All);

UHeroArenaScoreboardGrantComponent::UHeroArenaScoreboardGrantComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UHeroArenaScoreboardGrantComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		DamageListener = UGameplayMessageSubsystem::Get(this).RegisterListener(
			FGameplayTag::RequestGameplayTag(TEXT("Lyra.Damage.Message")),
			this, &ThisClass::HandleDamageMessage);
		TryReplaceScoreboardAbility();
		if (!GrantedHandle.IsValid())
		{
			GetWorld()->GetTimerManager().SetTimer(
				RetryTimer, this, &ThisClass::TryReplaceScoreboardAbility, 0.25f, true);
		}
	}
}

void UHeroArenaScoreboardGrantComponent::HandleDamageMessage(
	FGameplayTag Channel, const FLyraVerbMessage& Payload)
{
	ALyraPlayerState* OwnerPlayerState = Cast<ALyraPlayerState>(GetOwner());
	ALyraPlayerState* InstigatorPlayerState = Cast<ALyraPlayerState>(
		ULyraVerbMessageHelpers::GetPlayerStateFromObject(Payload.Instigator));
	ALyraPlayerState* TargetPlayerState = Cast<ALyraPlayerState>(
		ULyraVerbMessageHelpers::GetPlayerStateFromObject(Payload.Target));
	if (!OwnerPlayerState || InstigatorPlayerState != OwnerPlayerState || !TargetPlayerState ||
		TargetPlayerState == OwnerPlayerState || InstigatorPlayerState->GetTeamId() <= 0 ||
		InstigatorPlayerState->GetTeamId() == TargetPlayerState->GetTeamId())
	{
		return;
	}

	const ULyraAbilitySystemComponent* TargetASC = TargetPlayerState->GetLyraAbilitySystemComponent();
	const float HealthBeforeDamage = TargetASC
		? TargetASC->GetNumericAttribute(ULyraHealthSet::GetHealthAttribute()) : 0.0f;
	HeroArenaContribution::Add(OwnerPlayerState,
		FMath::Min(static_cast<float>(Payload.Magnitude), FMath::Max(0.0f, HealthBeforeDamage)));
}

void UHeroArenaScoreboardGrantComponent::TryReplaceScoreboardAbility()
{
	ALyraPlayerState* PlayerState = Cast<ALyraPlayerState>(GetOwner());
	ULyraAbilitySystemComponent* ASC = PlayerState ? PlayerState->GetLyraAbilitySystemComponent() : nullptr;
	if (!ASC || GrantedHandle.IsValid())
	{
		return;
	}

	FGameplayAbilitySpecHandle GenericScoreboardHandle;
	FString GenericScoreboardAbilityName;
	const FGameplayTag ScoreboardAbilityTag =
		FGameplayTag::RequestGameplayTag(TEXT("Ability.Type.Info.ShowLeaderboard"));
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (!Spec.Ability)
		{
			continue;
		}
		if (Spec.Ability->GetClass() == UHeroArenaGameplayAbility_ShowScoreboard::StaticClass())
		{
			GrantedHandle = Spec.Handle;
			GetWorld()->GetTimerManager().ClearTimer(RetryTimer);
			return;
		}
		if (Spec.Ability->GetAssetTags().HasTagExact(ScoreboardAbilityTag))
		{
			GenericScoreboardHandle = Spec.Handle;
			GenericScoreboardAbilityName = Spec.Ability->GetClass()->GetPathName();
			break;
		}
	}

	if (!GenericScoreboardHandle.IsValid())
	{
		return;
	}

	ASC->ClearAbility(GenericScoreboardHandle);
	FGameplayAbilitySpec HeroArenaSpec(
		UHeroArenaGameplayAbility_ShowScoreboard::StaticClass()->GetDefaultObject<UGameplayAbility>(), 1);
	HeroArenaSpec.GetDynamicSpecSourceTags().AddTag(
		FGameplayTag::RequestGameplayTag(TEXT("InputTag.Ability.ShowLeaderboard")));
	GrantedHandle = ASC->GiveAbility(HeroArenaSpec);
	GetWorld()->GetTimerManager().ClearTimer(RetryTimer);
	UE_LOG(LogHeroArenaScoreboardGrant, Log,
		TEXT("Hero Arena scoreboard ability replaced for %s: old=%s (%s) new=%s"),
		*GetNameSafe(PlayerState), *GenericScoreboardHandle.ToString(), *GenericScoreboardAbilityName,
		*GrantedHandle.ToString());
}

void UHeroArenaScoreboardGrantComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorld()->GetTimerManager().ClearTimer(RetryTimer);
	if (DamageListener.IsValid())
	{
		UGameplayMessageSubsystem::Get(this).UnregisterListener(DamageListener);
		DamageListener = FGameplayMessageListenerHandle();
	}
	if (ALyraPlayerState* PlayerState = Cast<ALyraPlayerState>(GetOwner());
		PlayerState && PlayerState->HasAuthority() && GrantedHandle.IsValid())
	{
		if (ULyraAbilitySystemComponent* ASC = PlayerState->GetLyraAbilitySystemComponent())
		{
			ASC->ClearAbility(GrantedHandle);
		}
	}
	GrantedHandle = FGameplayAbilitySpecHandle();
	Super::EndPlay(EndPlayReason);
}
