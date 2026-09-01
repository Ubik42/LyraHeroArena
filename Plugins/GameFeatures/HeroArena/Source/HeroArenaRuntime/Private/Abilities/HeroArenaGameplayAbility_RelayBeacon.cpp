#include "Abilities/HeroArenaGameplayAbility_RelayBeacon.h"

#include "AbilitySystem/Abilities/LyraAbilitySimpleFailureMessage.h"
#include "Actors/HeroArenaRelayBeacon.h"
#include "Character/LyraCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Effects/HeroArenaGameplayEffect_RelayBeaconCooldown.h"
#include "EngineUtils.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "GameFramework/PlayerController.h"
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
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(HeroArenaRelayBeaconPlacement), true, Character);
	const float PlacementDistances[] = { 260.0f, 180.0f };
	const float BotYawOffsets[] = { 0.0f, 45.0f, -45.0f, 90.0f, -90.0f, 135.0f, -135.0f, 180.0f };
	FHitResult GroundHit;
	FVector BeaconLocation = FVector::ZeroVector;
	float PlacementDistance = PlacementDistances[0];
	float PlacementYawOffset = 0.0f;
	bool bGroundHit = false;
	bool bWalkableSurface = false;
	bool bBlocked = false;
	bool bPlacementFound = false;
	const int32 DistanceAttemptCount = OwnerPlayerState->IsABot() ? UE_ARRAY_COUNT(PlacementDistances) : 1;
	const int32 DirectionAttemptCount = OwnerPlayerState->IsABot() ? UE_ARRAY_COUNT(BotYawOffsets) : 1;
	for (int32 DistanceAttempt = 0; DistanceAttempt < DistanceAttemptCount && !bPlacementFound; ++DistanceAttempt)
	{
		PlacementDistance = PlacementDistances[DistanceAttempt];
		for (int32 DirectionAttempt = 0; DirectionAttempt < DirectionAttemptCount; ++DirectionAttempt)
		{
			PlacementYawOffset = OwnerPlayerState->IsABot() ? BotYawOffsets[DirectionAttempt] : 0.0f;
			const FVector PlacementDirection = Forward.RotateAngleAxis(PlacementYawOffset, FVector::UpVector);
			const FVector TraceCenter = Character->GetActorLocation() + PlacementDirection * PlacementDistance;
			const FVector TraceStart = TraceCenter + FVector::UpVector * 180.0f;
			const FVector TraceEnd = TraceCenter - FVector::UpVector * 420.0f;
			GroundHit = FHitResult();
			bGroundHit = GetWorld()->LineTraceSingleByChannel(
				GroundHit, TraceStart, TraceEnd, ECC_Visibility, QueryParams);
			BeaconLocation = GroundHit.ImpactPoint + GroundHit.ImpactNormal * 4.0f;
			bWalkableSurface = bGroundHit && GroundHit.ImpactNormal.Z >= 0.65f;
			bBlocked = bWalkableSurface && GetWorld()->OverlapBlockingTestByChannel(
				BeaconLocation + FVector::UpVector * 55.0f, FQuat::Identity, ECC_Visibility,
				FCollisionShape::MakeCapsule(40.0f, 45.0f), QueryParams);
			if (bWalkableSurface && !bBlocked)
			{
				bPlacementFound = true;
				break;
			}
		}
	}
	if (!bPlacementFound)
	{
		if (ActorInfo && ActorInfo->IsLocallyControlled())
		{
			FLyraAbilitySimpleFailureMessage Message;
			Message.PlayerController = Cast<APlayerController>(Character->GetController());
			Message.UserFacingReason = !bGroundHit
				? NSLOCTEXT("HeroArenaAbilityFeedback", "BeaconNoGround", "前方没有可部署中继信标的地面")
				: !bWalkableSurface
					? NSLOCTEXT("HeroArenaAbilityFeedback", "BeaconSteepGround", "地面坡度过大，无法部署中继信标")
					: NSLOCTEXT("HeroArenaAbilityFeedback", "BeaconBlocked", "中继信标的部署位置被占用");
			UGameplayMessageSubsystem::Get(this).BroadcastMessage(
				FGameplayTag::RequestGameplayTag(TEXT("Ability.UserFacingSimpleActivateFail.Message")), Message);
		}
		if (Character->HasAuthority())
		{
			UE_LOG(LogHeroArenaRelayBeaconAbility, Log,
				TEXT("Relay beacon placement rejected: source=%s distance=%.0f yaw_offset=%.0f ground=%s normal_z=%.2f blocked=%s"),
				*GetNameSafe(Character), PlacementDistance, PlacementYawOffset,
				bGroundHit ? TEXT("true") : TEXT("false"),
				GroundHit.ImpactNormal.Z, bBlocked ? TEXT("true") : TEXT("false"));
		}
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
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
			TEXT("Relay beacon deployed: source=%s beacon=%s location=%s distance=%.0f yaw_offset=%.0f"),
			*GetNameSafe(Character), *GetNameSafe(Beacon), *BeaconLocation.ToCompactString(),
			PlacementDistance, PlacementYawOffset);
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
