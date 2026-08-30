#include "Abilities/HeroArenaGameplayAbility_RelayBeacon.h"

#include "Actors/HeroArenaRelayBeacon.h"
#include "Character/LyraCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Effects/HeroArenaGameplayEffect_RelayBeaconCooldown.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "Teams/LyraTeamSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaGameplayAbility_RelayBeacon)

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaRelayBeaconAbility, Log, All);

UHeroArenaGameplayAbility_RelayBeacon::UHeroArenaGameplayAbility_RelayBeacon(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	ActivationPolicy = ELyraAbilityActivationPolicy::OnInputTriggered;
	CooldownGameplayEffectClass = UHeroArenaGameplayEffect_RelayBeaconCooldown::StaticClass();
	FGameplayTagContainer Tags;
	Tags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Ability.Type.Action.RelayBeacon")));
	SetAssetTags(Tags);
}

void UHeroArenaGameplayAbility_RelayBeacon::ActivateAbility(
	FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	ALyraCharacter* Character = GetLyraCharacterFromActorInfo();
	APlayerState* OwnerPlayerState = Character ? Character->GetPlayerState() : nullptr;
	if (!Character || !OwnerPlayerState)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	for (TActorIterator<AHeroArenaRelayBeacon> It(GetWorld()); It; ++It)
	{
		if (It->GetOwner() == OwnerPlayerState)
		{
			bool bRecalled = true;
			if (Character->HasAuthority())
			{
				bRecalled = It->RecallBy(OwnerPlayerState);
			}
			UE_LOG(LogHeroArenaRelayBeaconAbility, Log,
				TEXT("Relay beacon ability recalled existing beacon: source=%s success=%s authority=%s"),
				*GetNameSafe(Character), bRecalled ? TEXT("true") : TEXT("false"),
				Character->HasAuthority() ? TEXT("true") : TEXT("false"));
			EndAbility(Handle, ActorInfo, ActivationInfo, true, !bRecalled);
			return;
		}
	}

	const FVector Forward = Character->GetActorForwardVector();
	const FVector TraceCenter = Character->GetActorLocation() + Forward * 260.0f;
	const FVector TraceStart = TraceCenter + FVector::UpVector * 180.0f;
	const FVector TraceEnd = TraceCenter - FVector::UpVector * 420.0f;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(HeroArenaRelayBeaconPlacement), true, Character);
	FHitResult GroundHit;
	const bool bGroundHit = GetWorld()->LineTraceSingleByChannel(
		GroundHit, TraceStart, TraceEnd, ECC_Visibility, QueryParams);
	const FVector BeaconLocation = GroundHit.ImpactPoint + GroundHit.ImpactNormal * 4.0f;
	const bool bWalkableSurface = bGroundHit && GroundHit.ImpactNormal.Z >= 0.65f;
	const bool bBlocked = bWalkableSurface && GetWorld()->OverlapBlockingTestByChannel(
		BeaconLocation + FVector::UpVector * 55.0f, FQuat::Identity, ECC_Visibility,
		FCollisionShape::MakeCapsule(40.0f, 45.0f), QueryParams);
	if (!bWalkableSurface || bBlocked || !CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		if (Character->HasAuthority())
		{
			UE_LOG(LogHeroArenaRelayBeaconAbility, Log,
				TEXT("Relay beacon placement rejected: source=%s ground=%s normal_z=%.2f blocked=%s"),
				*GetNameSafe(Character), bGroundHit ? TEXT("true") : TEXT("false"),
				GroundHit.ImpactNormal.Z, bBlocked ? TEXT("true") : TEXT("false"));
		}
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (Character->HasAuthority())
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = OwnerPlayerState;
		SpawnParameters.Instigator = Character;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AHeroArenaRelayBeacon* Beacon = GetWorld()->SpawnActorDeferred<AHeroArenaRelayBeacon>(
			AHeroArenaRelayBeacon::StaticClass(), FTransform(FRotator::ZeroRotator, BeaconLocation),
			OwnerPlayerState, Character, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Beacon)
		{
			if (ULyraTeamSubsystem* Teams = GetWorld()->GetSubsystem<ULyraTeamSubsystem>())
			{
				Beacon->SetGenericTeamId(IntegerToGenericTeamId(Teams->FindTeamFromObject(OwnerPlayerState)));
			}
			Beacon->FinishSpawning(FTransform(FRotator::ZeroRotator, BeaconLocation));
		}
		UE_LOG(LogHeroArenaRelayBeaconAbility, Log,
			TEXT("Relay beacon deployed: source=%s beacon=%s location=%s"),
			*GetNameSafe(Character), *GetNameSafe(Beacon), *BeaconLocation.ToCompactString());
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
