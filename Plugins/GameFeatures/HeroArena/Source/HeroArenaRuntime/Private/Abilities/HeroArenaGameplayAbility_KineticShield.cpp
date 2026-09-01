#include "Abilities/HeroArenaGameplayAbility_KineticShield.h"

#include "AbilitySystem/Attributes/LyraHealthSet.h"
#include "AbilitySystem/LyraAbilitySystemComponent.h"
#include "Actors/HeroArenaKineticShieldIndicator.h"
#include "Effects/HeroArenaGameplayEffect_KineticShieldCooldown.h"
#include "GameFramework/Pawn.h"
#include "Scoring/HeroArenaContribution.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaGameplayAbility_KineticShield)

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaKineticShield, Log, All);

UHeroArenaGameplayAbility_KineticShield::UHeroArenaGameplayAbility_KineticShield(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	ActivationPolicy = ELyraAbilityActivationPolicy::OnInputTriggered;
	CooldownGameplayEffectClass = UHeroArenaGameplayEffect_KineticShieldCooldown::StaticClass();

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Ability.Type.Action.KineticShield")));
	SetAssetTags(AssetTags);
	ActivationOwnedTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Status.HeroArena.KineticShield")));
}

void UHeroArenaGameplayAbility_KineticShield::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	AActor* AvatarActor = GetAvatarActorFromActorInfo();
	ULyraAbilitySystemComponent* AbilitySystem = GetLyraAbilitySystemComponentFromActorInfo();
	if (IsValid(AvatarActor) && AvatarActor->HasAuthority() && IsValid(AbilitySystem))
	{
		const ULyraHealthSet* HealthSet = AbilitySystem->GetSet<ULyraHealthSet>();
		if (!IsValid(HealthSet) || HealthSet->GetHealth() <= 0.0f)
		{
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}

		BaseMaxHealth = HealthSet->GetMaxHealth();
		RemainingShield = ShieldAmount;
		AbilitySystem->SetNumericAttributeBase(ULyraHealthSet::GetMaxHealthAttribute(), BaseMaxHealth + ShieldAmount);
		AbilitySystem->SetNumericAttributeBase(ULyraHealthSet::GetHealthAttribute(), HealthSet->GetHealth() + ShieldAmount);
		HealthSet->OnHealthChanged.AddUObject(this, &ThisClass::HandleHealthChanged);
		BoundHealthSet = HealthSet;
		bShieldApplied = true;

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = GetOwningActorFromActorInfo();
		SpawnParameters.Instigator = Cast<APawn>(AvatarActor);
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AHeroArenaKineticShieldIndicator* SpawnedIndicator = GetWorld()->SpawnActor<AHeroArenaKineticShieldIndicator>(
			AHeroArenaKineticShieldIndicator::StaticClass(), AvatarActor->GetActorLocation(), FRotator::ZeroRotator,
			SpawnParameters);
		if (IsValid(SpawnedIndicator))
		{
			SpawnedIndicator->InitializeIndicator(AvatarActor, ShieldAmount, ShieldDuration);
			Indicator = SpawnedIndicator;
		}
	}

	GetWorld()->GetTimerManager().SetTimer(DurationTimerHandle, this, &ThisClass::FinishShield, ShieldDuration, false);
	UE_LOG(LogHeroArenaKineticShield, Log,
		TEXT("Kinetic shield activated: avatar=%s shield=%.0f duration=%.1f authority=%s"),
		*GetNameSafe(AvatarActor), ShieldAmount, ShieldDuration,
		AvatarActor && AvatarActor->HasAuthority() ? TEXT("true") : TEXT("false"));
}

void UHeroArenaGameplayAbility_KineticShield::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const bool bReplicateEndAbility, const bool bWasCancelled)
{
	GetWorld()->GetTimerManager().ClearTimer(DurationTimerHandle);
	if (const ULyraHealthSet* HealthSet = BoundHealthSet.Get())
	{
		HealthSet->OnHealthChanged.RemoveAll(this);
	}

	AActor* AvatarActor = GetAvatarActorFromActorInfo();
	ULyraAbilitySystemComponent* AbilitySystem = GetLyraAbilitySystemComponentFromActorInfo();
	if (bShieldApplied && IsValid(AvatarActor) && AvatarActor->HasAuthority() && IsValid(AbilitySystem))
	{
		const ULyraHealthSet* HealthSet = AbilitySystem->GetSet<ULyraHealthSet>();
		if (IsValid(HealthSet))
		{
			const float HealthAfterRemainderRemoval = FMath::Clamp(
				HealthSet->GetHealth() - RemainingShield, 0.0f, BaseMaxHealth);
			AbilitySystem->SetNumericAttributeBase(ULyraHealthSet::GetHealthAttribute(), HealthAfterRemainderRemoval);
			AbilitySystem->SetNumericAttributeBase(ULyraHealthSet::GetMaxHealthAttribute(), BaseMaxHealth);
		}
	}

	if (AHeroArenaKineticShieldIndicator* ActiveIndicator = Indicator.Get();
		IsValid(ActiveIndicator) && ActiveIndicator->HasAuthority())
	{
		ActiveIndicator->Destroy();
	}

	UE_LOG(LogHeroArenaKineticShield, Log,
		TEXT("Kinetic shield ended: avatar=%s remaining=%.0f cancelled=%s authority=%s"),
		*GetNameSafe(AvatarActor), RemainingShield, bWasCancelled ? TEXT("true") : TEXT("false"),
		AvatarActor && AvatarActor->HasAuthority() ? TEXT("true") : TEXT("false"));

	bShieldApplied = false;
	RemainingShield = 0.0f;
	BaseMaxHealth = 0.0f;
	BoundHealthSet.Reset();
	Indicator.Reset();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UHeroArenaGameplayAbility_KineticShield::HandleHealthChanged(AActor* DamageInstigator, AActor* DamageCauser,
	const FGameplayEffectSpec* DamageEffectSpec, const float DamageMagnitude, const float OldValue, const float NewValue)
{
	const float HealthLoss = FMath::Max(0.0f, OldValue - NewValue);
	if (!bShieldApplied || HealthLoss <= 0.0f)
	{
		return;
	}

	const float Absorbed = FMath::Min(RemainingShield, HealthLoss);
	RemainingShield = FMath::Max(0.0f, RemainingShield - Absorbed);
	HeroArenaContribution::Add(GetOwningActorFromActorInfo(), Absorbed);
	if (AHeroArenaKineticShieldIndicator* ActiveIndicator = Indicator.Get())
	{
		ActiveIndicator->SetShieldAmount(RemainingShield);
	}

	UE_LOG(LogHeroArenaKineticShield, Log,
		TEXT("Kinetic shield absorbed: avatar=%s absorbed=%.0f remaining=%.0f health=%.0f->%.0f instigator=%s authority=true"),
		*GetNameSafe(GetAvatarActorFromActorInfo()), Absorbed, RemainingShield, OldValue, NewValue,
		*GetNameSafe(DamageInstigator));
	if (RemainingShield <= 0.0f)
	{
		FinishShield();
	}
}

void UHeroArenaGameplayAbility_KineticShield::FinishShield()
{
	EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, false);
}
