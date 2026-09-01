#include "Components/HeroArenaRecallHistoryComponent.h"

#include "Character/LyraHealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "AbilitySystem/Attributes/LyraHealthSet.h"
#include "AbilitySystem/LyraAbilitySystemComponent.h"
#include "Equipment/LyraEquipmentManagerComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "Hero/HeroArenaHeroDefinition.h"
#include "Hero/HeroArenaHeroSelectionComponent.h"
#include "Inventory/InventoryFragment_SetStats.h"
#include "Inventory/LyraInventoryItemInstance.h"
#include "Player/LyraPlayerState.h"
#include "TimerManager.h"
#include "Weapons/LyraRangedWeaponInstance.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaRecallHistoryComponent)

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaRecallHistory, Log, All);

UHeroArenaRecallHistoryComponent::UHeroArenaRecallHistoryComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsReplicatedByDefault(false);
}

void UHeroArenaRecallHistoryComponent::BeginPlay()
{
	Super::BeginPlay();

	APlayerState* PlayerState = Cast<APlayerState>(GetOwner());
	if (!PlayerState || !PlayerState->HasAuthority())
	{
		return;
	}

	PlayerState->OnPawnSet.AddUniqueDynamic(this, &ThisClass::HandlePawnSet);
	if (UHeroArenaHeroSelectionComponent* Selection =
		PlayerState->FindComponentByClass<UHeroArenaHeroSelectionComponent>())
	{
		Selection->OnHeroChanged.AddUniqueDynamic(this, &ThisClass::HandleHeroChanged);
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			SampleTimerHandle, this, &ThisClass::CaptureSample, SampleInterval, true, SampleInterval);
	}
	ResetHistory(TEXT("begin_play"));
}

void UHeroArenaRecallHistoryComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SampleTimerHandle);
	}
	if (APlayerState* PlayerState = Cast<APlayerState>(GetOwner()))
	{
		PlayerState->OnPawnSet.RemoveDynamic(this, &ThisClass::HandlePawnSet);
		if (UHeroArenaHeroSelectionComponent* Selection =
			PlayerState->FindComponentByClass<UHeroArenaHeroSelectionComponent>())
		{
			Selection->OnHeroChanged.RemoveDynamic(this, &ThisClass::HandleHeroChanged);
		}
	}
	Samples.Reset();
	Super::EndPlay(EndPlayReason);
}

void UHeroArenaRecallHistoryComponent::CaptureSample()
{
	APlayerState* PlayerState = Cast<APlayerState>(GetOwner());
	ACharacter* Character = PlayerState ? Cast<ACharacter>(PlayerState->GetPawn()) : nullptr;
	ULyraHealthComponent* Health = ULyraHealthComponent::FindHealthComponent(Character);
	UWorld* World = GetWorld();
	if (!PlayerState || !PlayerState->HasAuthority() || !World || !IsPhaseSelected() ||
		!Character || !Health || Health->IsDeadOrDying() || Health->GetHealth() <= 0.0f)
	{
		if (!Samples.IsEmpty())
		{
			ResetHistory(TEXT("ineligible_sample_context"));
		}
		return;
	}
	ULyraInventoryItemInstance* PistolItem = nullptr;
	int32 MagazineAmmo = 0;
	int32 MagazineCapacity = 0;
	if (!GetCurrentPistolState(Character, PistolItem, MagazineAmmo, MagazineCapacity))
	{
		if (!Samples.IsEmpty())
		{
			ResetHistory(TEXT("missing_pistol"));
		}
		return;
	}

	const float Now = World->GetTimeSeconds();
	if (!Samples.IsEmpty())
	{
		const FRecallSample& Previous = Samples.Last();
		if (Previous.Pawn.Get() != Character || Previous.PistolItem.Get() != PistolItem ||
			Now - Previous.ServerTime > MaximumSampleGap)
		{
			ResetHistory(Previous.PistolItem.Get() != PistolItem
				? TEXT("weapon_changed") : TEXT("pawn_or_sample_gap"));
		}
	}

	FRecallSample& Sample = Samples.AddDefaulted_GetRef();
	Sample.Pawn = Character;
	Sample.ServerTime = Now;
	Sample.Location = Character->GetActorLocation();
	Sample.Rotation = Character->GetActorRotation();
	Sample.Health = Health->GetHealth();
	Sample.PistolItem = PistolItem;
	Sample.MagazineAmmo = MagazineAmmo;
	Sample.MagazineCapacity = MagazineCapacity;

	const int32 MaxSamples = FMath::CeilToInt(HistoryDuration / SampleInterval) + 2;
	if (Samples.Num() > MaxSamples)
	{
		Samples.RemoveAt(0, Samples.Num() - MaxSamples, EAllowShrinking::No);
	}
}

bool UHeroArenaRecallHistoryComponent::HasUsefulRecallSample(const float MinimumHealthGain) const
{
	APlayerState* PlayerState = Cast<APlayerState>(GetOwner());
	ACharacter* Character = PlayerState ? Cast<ACharacter>(PlayerState->GetPawn()) : nullptr;
	ULyraHealthComponent* Health = ULyraHealthComponent::FindHealthComponent(Character);
	const UWorld* World = GetWorld();
	if (!PlayerState || !PlayerState->HasAuthority() || !World || !IsPhaseSelected()
		|| !Character || !Health || Health->IsDeadOrDying() || Health->GetHealth() <= 0.0f
		|| World->GetTimeSeconds() < NextRecallAllowedServerTime)
	{
		return false;
	}

	ULyraInventoryItemInstance* CurrentPistolItem = nullptr;
	int32 CurrentMagazineAmmo = 0;
	int32 CurrentMagazineCapacity = 0;
	if (!GetCurrentPistolState(
		Character, CurrentPistolItem, CurrentMagazineAmmo, CurrentMagazineCapacity))
	{
		return false;
	}

	const float TargetTime = World->GetTimeSeconds() - RecallDelay;
	int32 TargetIndex = INDEX_NONE;
	for (int32 Index = Samples.Num() - 1; Index >= 0; --Index)
	{
		if (Samples[Index].Pawn.Get() == Character && Samples[Index].ServerTime <= TargetTime)
		{
			TargetIndex = Index;
			break;
		}
	}
	if (TargetIndex == INDEX_NONE)
	{
		return false;
	}

	for (int32 Index = TargetIndex; Index < Samples.Num(); ++Index)
	{
		const FRecallSample& Sample = Samples[Index];
		if (Sample.Pawn.Get() != Character)
		{
			continue;
		}
		if (Sample.PistolItem.Get() != CurrentPistolItem)
		{
			return false;
		}
		if (IsLocationLegal(Character, Sample.Location, Sample.Rotation))
		{
			const float RestoredHealth = FMath::Clamp(Sample.Health, 1.0f, Health->GetMaxHealth());
			return RestoredHealth - Health->GetHealth() >= FMath::Max(0.0f, MinimumHealthGain);
		}
	}

	return false;
}

bool UHeroArenaRecallHistoryComponent::TryRecallPosition(FHeroArenaRecallResult& OutResult)
{
	OutResult = FHeroArenaRecallResult();
	APlayerState* PlayerState = Cast<APlayerState>(GetOwner());
	ACharacter* Character = PlayerState ? Cast<ACharacter>(PlayerState->GetPawn()) : nullptr;
	ULyraHealthComponent* Health = ULyraHealthComponent::FindHealthComponent(Character);
	UWorld* World = GetWorld();
	if (!PlayerState || !PlayerState->HasAuthority() || !World || !IsPhaseSelected())
	{
		OutResult.Reason = TEXT("not_authoritative_phase");
		return false;
	}
	if (!Character || !Health || Health->IsDeadOrDying() || Health->GetHealth() <= 0.0f)
	{
		OutResult.Reason = TEXT("invalid_pawn_lifecycle");
		return false;
	}
	ULyraInventoryItemInstance* CurrentPistolItem = nullptr;
	int32 CurrentMagazineAmmo = 0;
	int32 CurrentMagazineCapacity = 0;
	if (!GetCurrentPistolState(
		Character, CurrentPistolItem, CurrentMagazineAmmo, CurrentMagazineCapacity))
	{
		OutResult.Reason = TEXT("missing_current_pistol");
		return false;
	}

	const float Now = World->GetTimeSeconds();
	if (Now < NextRecallAllowedServerTime)
	{
		OutResult.Reason = TEXT("cooldown");
		return false;
	}
	const float TargetTime = Now - RecallDelay;
	int32 TargetIndex = INDEX_NONE;
	for (int32 Index = Samples.Num() - 1; Index >= 0; --Index)
	{
		if (Samples[Index].Pawn.Get() == Character && Samples[Index].ServerTime <= TargetTime)
		{
			TargetIndex = Index;
			break;
		}
	}
	if (TargetIndex == INDEX_NONE)
	{
		OutResult.Reason = TEXT("insufficient_history");
		return false;
	}

	OutResult.StartLocation = Character->GetActorLocation();
	for (int32 Index = TargetIndex; Index < Samples.Num(); ++Index)
	{
		const FRecallSample& Sample = Samples[Index];
		if (Sample.Pawn.Get() != Character)
		{
			continue;
		}
		if (Sample.PistolItem.Get() != CurrentPistolItem)
		{
			OutResult.Reason = TEXT("weapon_instance_changed");
			return false;
		}
		if (!IsLocationLegal(Character, Sample.Location, Sample.Rotation))
		{
			continue;
		}

		const float HealthBefore = Health->GetHealth();
		const float HealthAfter = FMath::Clamp(Sample.Health, 1.0f, Health->GetMaxHealth());
		const int32 SafeCapacity = FMath::Max(0, FMath::Min(Sample.MagazineCapacity, CurrentMagazineCapacity));
		const int32 AmmoAfter = FMath::Clamp(Sample.MagazineAmmo, 0, SafeCapacity);
		if (ALyraPlayerState* LyraPlayerState = Cast<ALyraPlayerState>(PlayerState))
		{
			if (ULyraAbilitySystemComponent* AbilitySystem = LyraPlayerState->GetLyraAbilitySystemComponent())
			{
				AbilitySystem->SetNumericAttributeBase(ULyraHealthSet::GetHealthAttribute(), HealthAfter);
			}
		}
		if (AmmoAfter > CurrentMagazineAmmo)
		{
			CurrentPistolItem->AddStatTagStack(
				FGameplayTag::RequestGameplayTag(TEXT("Lyra.ShooterGame.Weapon.MagazineAmmo")),
				AmmoAfter - CurrentMagazineAmmo);
		}
		else if (AmmoAfter < CurrentMagazineAmmo)
		{
			CurrentPistolItem->RemoveStatTagStack(
				FGameplayTag::RequestGameplayTag(TEXT("Lyra.ShooterGame.Weapon.MagazineAmmo")),
				CurrentMagazineAmmo - AmmoAfter);
		}

		Character->SetActorLocationAndRotation(
			Sample.Location, Sample.Rotation, false, nullptr, ETeleportType::TeleportPhysics);
		if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
		}
		OutResult.bSucceeded = true;
		OutResult.Reason = Index == TargetIndex ? TEXT("target_sample") : TEXT("newer_legal_fallback");
		OutResult.RecalledSeconds = Now - Sample.ServerTime;
		OutResult.EndLocation = Character->GetActorLocation();
		NextRecallAllowedServerTime = Now + RecallCooldown;
		UE_LOG(LogHeroArenaRecallHistory, Log,
			TEXT("Recall state resolved: player=%s pawn=%s seconds=%.2f reason=%s start=%s end=%s health=%.2f->%.2f ammo=%d->%d/%d samples=%d"),
			*GetNameSafe(PlayerState), *GetNameSafe(Character), OutResult.RecalledSeconds,
			*OutResult.Reason.ToString(), *OutResult.StartLocation.ToCompactString(),
			*OutResult.EndLocation.ToCompactString(), HealthBefore, HealthAfter,
			CurrentMagazineAmmo, AmmoAfter, SafeCapacity, Samples.Num());
		Samples.Reset();
		CaptureSample();
		return true;
	}

	OutResult.Reason = TEXT("no_legal_historical_location");
	UE_LOG(LogHeroArenaRecallHistory, Log,
		TEXT("Recall position rejected: player=%s pawn=%s reason=%s samples=%d"),
		*GetNameSafe(PlayerState), *GetNameSafe(Character), *OutResult.Reason.ToString(), Samples.Num());
	return false;
}

bool UHeroArenaRecallHistoryComponent::GetCurrentPistolState(
	ACharacter* Character, ULyraInventoryItemInstance*& OutItem,
	int32& OutMagazineAmmo, int32& OutMagazineCapacity) const
{
	OutItem = nullptr;
	OutMagazineAmmo = 0;
	OutMagazineCapacity = 0;
	ULyraEquipmentManagerComponent* Equipment =
		Character ? Character->FindComponentByClass<ULyraEquipmentManagerComponent>() : nullptr;
	ULyraRangedWeaponInstance* Weapon = Equipment
		? Equipment->GetFirstInstanceOfType<ULyraRangedWeaponInstance>() : nullptr;
	ULyraInventoryItemInstance* Item = Weapon
		? Cast<ULyraInventoryItemInstance>(Weapon->GetInstigator()) : nullptr;
	if (!Item || !Item->GetItemDef() || !Item->GetItemDef()->GetName().Contains(TEXT("Pistol")))
	{
		return false;
	}

	const FGameplayTag MagazineTag =
		FGameplayTag::RequestGameplayTag(TEXT("Lyra.ShooterGame.Weapon.MagazineAmmo"));
	const UInventoryFragment_SetStats* Stats = Item->FindFragmentByClass<UInventoryFragment_SetStats>();
	const int32 Capacity = Stats ? Stats->GetItemStatByTag(MagazineTag) : 0;
	if (Capacity <= 0)
	{
		return false;
	}

	OutItem = Item;
	OutMagazineAmmo = FMath::Clamp(Item->GetStatTagStackCount(MagazineTag), 0, Capacity);
	OutMagazineCapacity = Capacity;
	return true;
}

bool UHeroArenaRecallHistoryComponent::IsLocationLegal(
	ACharacter* Character, const FVector& Location, const FRotator& Rotation) const
{
	const UWorld* World = GetWorld();
	const UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
	if (!World || !Capsule)
	{
		return false;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(HeroArenaRecallLocation), false, Character);
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(
		Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight());
	return !World->OverlapBlockingTestByChannel(
		Location, Rotation.Quaternion(), Capsule->GetCollisionObjectType(), Shape, QueryParams);
}

bool UHeroArenaRecallHistoryComponent::IsPhaseSelected() const
{
	const UHeroArenaHeroSelectionComponent* Selection =
		GetOwner() ? GetOwner()->FindComponentByClass<UHeroArenaHeroSelectionComponent>() : nullptr;
	const UHeroArenaHeroDefinition* Hero = Selection ? Selection->GetSelectedHero() : nullptr;
	return Hero && Hero->HeroId == TEXT("Phase");
}

void UHeroArenaRecallHistoryComponent::HandleHeroChanged(
	const UHeroArenaHeroDefinition* OldHero, const UHeroArenaHeroDefinition* NewHero)
{
	ResetHistory(TEXT("hero_changed"));
}

void UHeroArenaRecallHistoryComponent::HandlePawnSet(
	APlayerState* PlayerState, APawn* NewPawn, APawn* OldPawn)
{
	if (OldPawn != NewPawn)
	{
		ResetHistory(NewPawn ? TEXT("pawn_spawned") : TEXT("pawn_removed"));
	}
}

void UHeroArenaRecallHistoryComponent::ResetHistory(const TCHAR* Reason)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	const int32 Removed = Samples.Num();
	Samples.Reset();
	NextRecallAllowedServerTime = 0.0f;
	UE_LOG(LogHeroArenaRecallHistory, Log,
		TEXT("Recall history reset: player=%s removed=%d reason=%s"),
		*GetNameSafe(GetOwner()), Removed, Reason);
}
