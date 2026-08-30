#include "Hero/HeroArenaHeroSelectionComponent.h"

#include "AbilitySystem/LyraAbilitySystemComponent.h"
#include "Character/LyraPawnExtensionComponent.h"
#include "Equipment/LyraEquipmentManagerComponent.h"
#include "Equipment/LyraQuickBarComponent.h"
#include "Hero/HeroArenaHeroDefinition.h"
#include "Hero/HeroArenaHeroRoster.h"
#include "Inventory/LyraInventoryItemDefinition.h"
#include "Inventory/LyraInventoryManagerComponent.h"
#include "Net/UnrealNetwork.h"
#include "Player/LyraPlayerState.h"
#include "Weapons/LyraRangedWeaponInstance.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaHeroSelectionComponent)

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaSelection, Log, All);

UHeroArenaHeroSelectionComponent::UHeroArenaHeroSelectionComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsReplicatedByDefault(true);
}

void UHeroArenaHeroSelectionComponent::BeginPlay()
{
	Super::BeginPlay();

	if (APlayerState* PlayerState = Cast<APlayerState>(GetOwner()))
	{
		PlayerState->OnPawnSet.AddUniqueDynamic(this, &ThisClass::HandlePawnSet);
	}

	if (GetOwner()->HasAuthority() && !SelectedHero && Roster && Roster->DefaultHero)
	{
		ApplySelectedHero(Roster->DefaultHero);
	}
}

void UHeroArenaHeroSelectionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (APlayerState* PlayerState = Cast<APlayerState>(GetOwner()))
	{
		PlayerState->OnPawnSet.RemoveDynamic(this, &ThisClass::HandlePawnSet);
	}

	if (GetOwner()->HasAuthority())
	{
		RemoveGrantedAbilitySets();
	}

	Super::EndPlay(EndPlayReason);
}

void UHeroArenaHeroSelectionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ThisClass, SelectedHero);
	DOREPLIFETIME(ThisClass, bSelectionLocked);
}

void UHeroArenaHeroSelectionComponent::RequestHeroSelection(const FName HeroId)
{
	if (GetOwner()->HasAuthority())
	{
		BroadcastSelectionResult(HeroId, TrySelectHeroById(HeroId));
	}
	else
	{
		ServerRequestHeroSelection(HeroId);
	}
}

void UHeroArenaHeroSelectionComponent::ServerRequestHeroSelection_Implementation(const FName HeroId)
{
	ClientNotifyHeroSelectionResult(HeroId, TrySelectHeroById(HeroId));
}

bool UHeroArenaHeroSelectionComponent::SelectHeroById(const FName HeroId)
{
	const EHeroArenaHeroSelectionResult Result = TrySelectHeroById(HeroId);
	return Result == EHeroArenaHeroSelectionResult::Success ||
		Result == EHeroArenaHeroSelectionResult::AlreadySelected;
}

EHeroArenaHeroSelectionResult UHeroArenaHeroSelectionComponent::TrySelectHeroById(const FName HeroId)
{
	if (!GetOwner()->HasAuthority() || !Roster)
	{
		return EHeroArenaHeroSelectionResult::RosterUnavailable;
	}
	if (bSelectionLocked)
	{
		return EHeroArenaHeroSelectionResult::SelectionLocked;
	}

	const UHeroArenaHeroDefinition* RequestedHero = Roster->FindHeroById(HeroId);
	if (!RequestedHero)
	{
		return EHeroArenaHeroSelectionResult::InvalidHero;
	}
	if (SelectedHero == RequestedHero)
	{
		return EHeroArenaHeroSelectionResult::AlreadySelected;
	}

	return ApplySelectedHero(RequestedHero)
		? EHeroArenaHeroSelectionResult::Success
		: EHeroArenaHeroSelectionResult::AbilitySystemUnavailable;
}

void UHeroArenaHeroSelectionComponent::ClientNotifyHeroSelectionResult_Implementation(
	const FName HeroId, const EHeroArenaHeroSelectionResult Result)
{
	BroadcastSelectionResult(HeroId, Result);
}

void UHeroArenaHeroSelectionComponent::BroadcastSelectionResult(
	const FName HeroId, const EHeroArenaHeroSelectionResult Result)
{
	OnSelectionRequestResolved.Broadcast(HeroId, Result);
	UE_LOG(LogHeroArenaSelection, Log, TEXT("Hero selection request '%s' resolved as %s for %s (authority=%s)"),
		*HeroId.ToString(), *UEnum::GetValueAsString(Result), *GetNameSafe(GetOwner()),
		GetOwner()->HasAuthority() ? TEXT("true") : TEXT("false"));
}

FText UHeroArenaHeroSelectionComponent::GetSelectionResultText(const EHeroArenaHeroSelectionResult Result)
{
	switch (Result)
	{
	case EHeroArenaHeroSelectionResult::Success:
		return NSLOCTEXT("HeroArenaSelection", "Success", "英雄选择成功");
	case EHeroArenaHeroSelectionResult::AlreadySelected:
		return NSLOCTEXT("HeroArenaSelection", "AlreadySelected", "你已经选择了该英雄");
	case EHeroArenaHeroSelectionResult::InvalidHero:
		return NSLOCTEXT("HeroArenaSelection", "InvalidHero", "该英雄不在当前阵容中");
	case EHeroArenaHeroSelectionResult::SelectionLocked:
		return NSLOCTEXT("HeroArenaSelection", "SelectionLocked", "当前阶段无法更换英雄");
	case EHeroArenaHeroSelectionResult::RosterUnavailable:
		return NSLOCTEXT("HeroArenaSelection", "RosterUnavailable", "英雄列表尚未就绪");
	case EHeroArenaHeroSelectionResult::AbilitySystemUnavailable:
		return NSLOCTEXT("HeroArenaSelection", "AbilitySystemUnavailable", "角色系统尚未就绪，请稍后重试");
	default:
		return NSLOCTEXT("HeroArenaSelection", "Unknown", "英雄选择失败");
	}
}

void UHeroArenaHeroSelectionComponent::SetSelectionLocked(const bool bLocked)
{
	if (!GetOwner()->HasAuthority() || bSelectionLocked == bLocked)
	{
		return;
	}

	bSelectionLocked = bLocked;
	OnSelectionLockChanged.Broadcast(bSelectionLocked);
}

void UHeroArenaHeroSelectionComponent::SetRoster(const UHeroArenaHeroRoster* NewRoster)
{
	if (!GetOwner()->HasAuthority() || Roster == NewRoster)
	{
		return;
	}

	Roster = NewRoster;

	if (!Roster)
	{
		ApplySelectedHero(nullptr);
	}
	else if (!Roster->ContainsHero(SelectedHero))
	{
		ApplySelectedHero(Roster->DefaultHero);
	}
}

bool UHeroArenaHeroSelectionComponent::ApplySelectedHero(const UHeroArenaHeroDefinition* NewHero)
{
	if (!GetOwner()->HasAuthority())
	{
		return false;
	}

	if (NewHero && (!Roster || !Roster->ContainsHero(NewHero)))
	{
		return false;
	}

	if (SelectedHero == NewHero)
	{
		return true;
	}

	ALyraPlayerState* LyraPlayerState = Cast<ALyraPlayerState>(GetOwner());
	ULyraAbilitySystemComponent* AbilitySystem = LyraPlayerState ? LyraPlayerState->GetLyraAbilitySystemComponent() : nullptr;
	if (!AbilitySystem)
	{
		return false;
	}

	const UHeroArenaHeroDefinition* OldHero = SelectedHero;
	RemoveGrantedAbilitySets();
	SelectedHero = NewHero;

	if (!NewHero)
	{
		OnHeroChanged.Broadcast(OldHero, nullptr);
		return true;
	}

	GrantedAbilitySetHandles.Reserve(NewHero->AbilitySets.Num());
	for (const ULyraAbilitySet* AbilitySet : NewHero->AbilitySets)
	{
		if (AbilitySet)
		{
			FLyraAbilitySet_GrantedHandles& Handles = GrantedAbilitySetHandles.AddDefaulted_GetRef();
			AbilitySet->GiveToAbilitySystem(AbilitySystem, &Handles, const_cast<UHeroArenaHeroDefinition*>(NewHero));
		}
	}

	ApplySelectedHeroLoadout(LyraPlayerState->GetPawn());

	OnHeroChanged.Broadcast(OldHero, SelectedHero);
	UE_LOG(LogHeroArenaSelection, Log, TEXT("Selected hero '%s' for %s (authority=%s)"),
		*SelectedHero->HeroId.ToString(), *GetNameSafe(GetOwner()), GetOwner()->HasAuthority() ? TEXT("true") : TEXT("false"));
	return true;
}

bool UHeroArenaHeroSelectionComponent::ApplySelectedHeroLoadout(APawn* Pawn)
{
	if (!GetOwner()->HasAuthority() || !SelectedHero || !Pawn || SelectedHero->DefaultWeaponItem.IsNull())
	{
		return false;
	}
	ULyraPawnExtensionComponent* PawnExtension = ULyraPawnExtensionComponent::FindPawnExtensionComponent(Pawn);
	if (!PawnExtension || !PawnExtension->GetLyraAbilitySystemComponent())
	{
		return false;
	}

	AController* Controller = Pawn->GetController();
	ULyraInventoryManagerComponent* Inventory = Controller ? Controller->FindComponentByClass<ULyraInventoryManagerComponent>() : nullptr;
	ULyraQuickBarComponent* QuickBar = Controller ? Controller->FindComponentByClass<ULyraQuickBarComponent>() : nullptr;
	UClass* WeaponItemClass = SelectedHero->DefaultWeaponItem.LoadSynchronous();
	if (!Inventory || !QuickBar || !WeaponItemClass)
	{
		UE_LOG(LogHeroArenaSelection, Warning, TEXT("Could not apply weapon loadout for hero '%s' on %s"),
			*SelectedHero->HeroId.ToString(), *GetNameSafe(Pawn));
		return false;
	}

	if (ULyraInventoryItemInstance* OldItem = QuickBar->RemoveItemFromSlot(0))
	{
		Inventory->RemoveItemInstance(OldItem);
	}

	ULyraInventoryItemInstance* NewItem = Inventory->AddItemDefinition(WeaponItemClass, 1);
	if (!NewItem)
	{
		UE_LOG(LogHeroArenaSelection, Warning, TEXT("Failed to create default weapon '%s' for hero '%s'"),
			*GetNameSafe(WeaponItemClass), *SelectedHero->HeroId.ToString());
		return false;
	}

	QuickBar->AddItemToSlot(0, NewItem);
	QuickBar->SetActiveSlotIndex(0);
	ULyraEquipmentManagerComponent* EquipmentManager = Pawn->FindComponentByClass<ULyraEquipmentManagerComponent>();
	UObject* EquippedRangedWeapon = EquipmentManager
		? EquipmentManager->GetFirstInstanceOfType(ULyraRangedWeaponInstance::StaticClass())
		: nullptr;
	UE_LOG(LogHeroArenaSelection, Log,
		TEXT("Applied default weapon '%s' to hero '%s' on %s (active_item=%s ranged_instance=%s)"),
		*GetNameSafe(WeaponItemClass), *SelectedHero->HeroId.ToString(), *GetNameSafe(Pawn),
		*GetNameSafe(QuickBar->GetActiveSlotItem()), *GetNameSafe(EquippedRangedWeapon));
	return true;
}

void UHeroArenaHeroSelectionComponent::HandlePawnAbilitySystemInitialized()
{
	if (APawn* Pawn = PendingLoadoutPawn.Get())
	{
		ApplySelectedHeroLoadout(Pawn);
	}
	PendingLoadoutPawn.Reset();
}

void UHeroArenaHeroSelectionComponent::RemoveGrantedAbilitySets()
{
	ALyraPlayerState* LyraPlayerState = Cast<ALyraPlayerState>(GetOwner());
	ULyraAbilitySystemComponent* AbilitySystem = LyraPlayerState ? LyraPlayerState->GetLyraAbilitySystemComponent() : nullptr;
	if (AbilitySystem)
	{
		for (FLyraAbilitySet_GrantedHandles& Handles : GrantedAbilitySetHandles)
		{
			Handles.TakeFromAbilitySystem(AbilitySystem);
		}
	}

	GrantedAbilitySetHandles.Reset();
}

void UHeroArenaHeroSelectionComponent::OnRep_SelectedHero(const UHeroArenaHeroDefinition* OldHero)
{
	OnHeroChanged.Broadcast(OldHero, SelectedHero);
	const FString HeroId = SelectedHero ? SelectedHero->HeroId.ToString() : TEXT("None");
	UE_LOG(LogHeroArenaSelection, Log, TEXT("Replicated hero '%s' to %s"),
		*HeroId, *GetNameSafe(GetOwner()));
}

void UHeroArenaHeroSelectionComponent::OnRep_SelectionLocked()
{
	OnSelectionLockChanged.Broadcast(bSelectionLocked);
}

void UHeroArenaHeroSelectionComponent::HandlePawnSet(APlayerState* PlayerState, APawn* NewPawn, APawn* OldPawn)
{
	if (!SelectedHero || !NewPawn)
	{
		return;
	}

	UE_LOG(LogHeroArenaSelection, Log, TEXT("Hero '%s' persisted for %s across pawn transition %s -> %s (authority=%s)"),
		*SelectedHero->HeroId.ToString(), *GetNameSafe(PlayerState), *GetNameSafe(OldPawn), *GetNameSafe(NewPawn),
		GetOwner()->HasAuthority() ? TEXT("true") : TEXT("false"));

	if (GetOwner()->HasAuthority())
	{
		if (const ALyraPlayerState* LyraPlayerState = Cast<ALyraPlayerState>(PlayerState))
		{
			if (const ULyraAbilitySystemComponent* AbilitySystem = LyraPlayerState->GetLyraAbilitySystemComponent())
			{
				const FGameplayTag Amplified = FGameplayTag::RequestGameplayTag(TEXT("Status.HeroArena.HealingAmplified"));
				const FGameplayTag Suppressed = FGameplayTag::RequestGameplayTag(TEXT("Status.HeroArena.HealingSuppressed"));
				UE_LOG(LogHeroArenaSelection, Log, TEXT("Pawn transition healing statuses: player=%s amplified=%d suppressed=%d"),
					*GetNameSafe(PlayerState), AbilitySystem->GetTagCount(Amplified), AbilitySystem->GetTagCount(Suppressed));
			}
		}
		PendingLoadoutPawn = NewPawn;
		if (ULyraPawnExtensionComponent* PawnExtension = ULyraPawnExtensionComponent::FindPawnExtensionComponent(NewPawn))
		{
			PawnExtension->OnAbilitySystemInitialized_RegisterAndCall(
				FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &ThisClass::HandlePawnAbilitySystemInitialized));
		}
	}
}
