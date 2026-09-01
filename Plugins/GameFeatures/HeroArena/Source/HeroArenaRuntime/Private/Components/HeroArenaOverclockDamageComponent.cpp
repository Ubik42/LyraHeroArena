#include "Components/HeroArenaOverclockDamageComponent.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystem/Attributes/LyraHealthSet.h"
#include "AbilitySystem/LyraAbilitySystemComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "LyraGameplayTags.h"
#include "Messages/LyraVerbMessage.h"
#include "Player/LyraPlayerState.h"
#include "System/LyraAssetManager.h"
#include "System/LyraGameData.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaOverclockDamageComponent)

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaOverclockDamage, Log, All);

namespace HeroArenaOverclockDamage
{
	APlayerState* ResolvePlayerState(AActor* Actor)
	{
		if (APlayerState* PlayerState = Cast<APlayerState>(Actor)) return PlayerState;
		if (const APawn* Pawn = Cast<APawn>(Actor)) return Pawn->GetPlayerState();
		if (const AController* Controller = Cast<AController>(Actor)) return Controller->PlayerState;
		if (Actor)
		{
			if (AController* InstigatorController = Actor->GetInstigatorController())
			{
				return InstigatorController->PlayerState;
			}
			if (AActor* Owner = Actor->GetOwner(); Owner && Owner != Actor)
			{
				return ResolvePlayerState(Owner);
			}
		}
		return nullptr;
	}
}

void UHeroArenaOverclockDamageComponent::BeginPlay()
{
	Super::BeginPlay();
	ALyraPlayerState* PlayerState = Cast<ALyraPlayerState>(GetOwner());
	if (!PlayerState)
	{
		return;
	}
	PlayerState->OnPawnSet.AddUniqueDynamic(this, &ThisClass::HandlePawnSet);
	if (ULyraAbilitySystemComponent* ASC = PlayerState->GetLyraAbilitySystemComponent())
	{
		ASC->RegisterGameplayTagEvent(
			FGameplayTag::RequestGameplayTag(TEXT("Status.HeroArena.Overclocked")),
			EGameplayTagEventType::NewOrRemoved).AddUObject(this, &ThisClass::HandleOverclockTagChanged);
	}
	if (PlayerState->HasAuthority())
	{
		ListenerHandle = UGameplayMessageSubsystem::Get(this).RegisterListener(
			TAG_Lyra_Damage_Message, this, &ThisClass::HandleDamageMessage);
	}
	RefreshMovementBoost();
}

void UHeroArenaOverclockDamageComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RestoreMovementBoost();
	if (ALyraPlayerState* PlayerState = Cast<ALyraPlayerState>(GetOwner()))
	{
		PlayerState->OnPawnSet.RemoveDynamic(this, &ThisClass::HandlePawnSet);
		if (ULyraAbilitySystemComponent* ASC = PlayerState->GetLyraAbilitySystemComponent())
		{
			ASC->RegisterGameplayTagEvent(
				FGameplayTag::RequestGameplayTag(TEXT("Status.HeroArena.Overclocked")),
				EGameplayTagEventType::NewOrRemoved).RemoveAll(this);
		}
	}
	if (ListenerHandle.IsValid())
	{
		UGameplayMessageSubsystem::Get(this).UnregisterListener(ListenerHandle);
	}
	Super::EndPlay(EndPlayReason);
}

void UHeroArenaOverclockDamageComponent::HandleOverclockTagChanged(const FGameplayTag Tag, const int32 NewCount)
{
	if (NewCount > 0)
	{
		RefreshMovementBoost();
	}
	else
	{
		RestoreMovementBoost();
	}
}

void UHeroArenaOverclockDamageComponent::HandlePawnSet(
	APlayerState* PlayerState, APawn* NewPawn, APawn* OldPawn)
{
	RestoreMovementBoost();
	RefreshMovementBoost();
}

void UHeroArenaOverclockDamageComponent::RefreshMovementBoost()
{
	ALyraPlayerState* PlayerState = Cast<ALyraPlayerState>(GetOwner());
	ULyraAbilitySystemComponent* ASC = PlayerState ? PlayerState->GetLyraAbilitySystemComponent() : nullptr;
	const FGameplayTag OverclockTag = FGameplayTag::RequestGameplayTag(TEXT("Status.HeroArena.Overclocked"));
	ACharacter* Character = PlayerState ? Cast<ACharacter>(PlayerState->GetPawn()) : nullptr;
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!ASC || !ASC->HasMatchingGameplayTag(OverclockTag) || !Movement || ModifiedMovement.Get() == Movement)
	{
		return;
	}

	RestoreMovementBoost();
	ModifiedMovement = Movement;
	OriginalMaxWalkSpeed = Movement->MaxWalkSpeed;
	Movement->MaxWalkSpeed = OriginalMaxWalkSpeed * MovementSpeedMultiplier;
	UE_LOG(LogHeroArenaOverclockDamage, Log,
		TEXT("Overclock movement applied: pawn=%s speed=%.2f->%.2f authority=%s"),
		*GetNameSafe(Character), OriginalMaxWalkSpeed, Movement->MaxWalkSpeed,
		Character->HasAuthority() ? TEXT("true") : TEXT("false"));
}

void UHeroArenaOverclockDamageComponent::RestoreMovementBoost()
{
	if (UCharacterMovementComponent* Movement = ModifiedMovement.Get())
	{
		const float BoostedSpeed = Movement->MaxWalkSpeed;
		if (FMath::IsNearlyEqual(BoostedSpeed, OriginalMaxWalkSpeed * MovementSpeedMultiplier))
		{
			Movement->MaxWalkSpeed = OriginalMaxWalkSpeed;
		}
		UE_LOG(LogHeroArenaOverclockDamage, Log,
			TEXT("Overclock movement restored: pawn=%s speed=%.2f->%.2f authority=%s"),
			*GetNameSafe(Movement->GetOwner()), BoostedSpeed, Movement->MaxWalkSpeed,
			Movement->GetOwner() && Movement->GetOwner()->HasAuthority() ? TEXT("true") : TEXT("false"));
	}
	ModifiedMovement.Reset();
	OriginalMaxWalkSpeed = 0.0f;
}

void UHeroArenaOverclockDamageComponent::HandleDamageMessage(
	FGameplayTag Channel, const FLyraVerbMessage& Payload)
{
	ALyraPlayerState* SourcePlayerState = Cast<ALyraPlayerState>(GetOwner());
	if (bApplyingBonus || !SourcePlayerState ||
		HeroArenaOverclockDamage::ResolvePlayerState(Cast<AActor>(Payload.Instigator.Get())) != SourcePlayerState)
	{
		return;
	}

	ULyraAbilitySystemComponent* SourceASC = SourcePlayerState->GetLyraAbilitySystemComponent();
	UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(
		Cast<AActor>(Payload.Target.Get()));
	const FGameplayTag OverclockTag = FGameplayTag::RequestGameplayTag(TEXT("Status.HeroArena.Overclocked"));
	const FGameplayTag TeamAmplifiedTag = FGameplayTag::RequestGameplayTag(TEXT("Status.HeroArena.TeamAmplified"));
	const FGameplayTag BattlePulseTag = FGameplayTag::RequestGameplayTag(TEXT("Status.HeroArena.Event.PowerSurge"));
	const float PrimaryDamage = FMath::Abs(Payload.Magnitude);
	const float ActiveBonusRatio = SourceASC && SourceASC->HasMatchingGameplayTag(OverclockTag)
		? BonusDamageRatio
		: (SourceASC && SourceASC->HasMatchingGameplayTag(TeamAmplifiedTag) ? TeamAmplifiedBonusDamageRatio
		: (SourceASC && SourceASC->HasMatchingGameplayTag(BattlePulseTag) ? BattlePulseBonusDamageRatio : 0.0f));
	const float BonusDamage = PrimaryDamage * ActiveBonusRatio;
	if (!SourceASC || !TargetASC || BonusDamage <= 0.0f)
	{
		return;
	}

	const TSubclassOf<UGameplayEffect> DamageEffect =
		ULyraAssetManager::GetSubclass(ULyraGameData::Get().DamageGameplayEffect_SetByCaller);
	FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();
	FGameplayEffectSpecHandle Spec = SourceASC->MakeOutgoingSpec(DamageEffect, 1.0f, Context);
	if (!Spec.IsValid())
	{
		return;
	}
	Spec.Data->SetSetByCallerMagnitude(LyraGameplayTags::SetByCaller_Damage, BonusDamage);
	TGuardValue<bool> RecursionGuard(bApplyingBonus, true);
	TargetASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());

	UE_LOG(LogHeroArenaOverclockDamage, Log,
		TEXT("Hero damage amplifier resolved: source=%s target=%s primary=%.2f bonus=%.2f ratio=%.2f"),
		*GetNameSafe(SourcePlayerState->GetPawn()), *GetNameSafe(Payload.Target.Get()),
		PrimaryDamage, BonusDamage, ActiveBonusRatio);
}
