#include "Abilities/HeroArenaGameplayAbility_RelayJumpPad.h"

#include "AbilitySystem/Abilities/LyraAbilitySimpleFailureMessage.h"
#include "Actors/HeroArenaRelayJumpPad.h"
#include "Character/LyraCharacter.h"
#include "Effects/HeroArenaGameplayEffect_RelayJumpPadCooldown.h"
#include "EngineUtils.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Teams/LyraTeamSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaGameplayAbility_RelayJumpPad)

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaRelayJumpPadAbility, Log, All);

UHeroArenaGameplayAbility_RelayJumpPad::UHeroArenaGameplayAbility_RelayJumpPad(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	ActivationPolicy = ELyraAbilityActivationPolicy::OnInputTriggered;
	CooldownGameplayEffectClass = UHeroArenaGameplayEffect_RelayJumpPadCooldown::StaticClass();
	FGameplayTagContainer Tags;
	Tags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Ability.Type.Action.RelayJumpPad")));
	SetAssetTags(Tags);
}

void UHeroArenaGameplayAbility_RelayJumpPad::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	ALyraCharacter* Character = GetLyraCharacterFromActorInfo();
	APlayerState* OwnerPlayerState = Character ? Character->GetPlayerState() : nullptr;
	if (!Character || !OwnerPlayerState)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	for (TActorIterator<AHeroArenaRelayJumpPad> It(GetWorld()); It; ++It)
	{
		if (It->GetOwner() == OwnerPlayerState)
		{
			if (Character->HasAuthority())
			{
				It->RecallBy(OwnerPlayerState);
			}
			EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
			return;
		}
	}

	const FVector Forward = Character->GetActorForwardVector();
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(HeroArenaRelayJumpPadPlacement), true, Character);
	const float PlacementDistances[] = { 285.0f, 190.0f };
	const float BotYawOffsets[] = { 0.0f, 45.0f, -45.0f, 90.0f, -90.0f, 135.0f, -135.0f, 180.0f };
	FHitResult GroundHit;
	FVector PadLocation = FVector::ZeroVector;
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
			bWalkableSurface = bGroundHit && GroundHit.ImpactNormal.Z >= 0.72f;
			PadLocation = GroundHit.ImpactPoint + GroundHit.ImpactNormal * 5.0f;
			bBlocked = bWalkableSurface && GetWorld()->OverlapBlockingTestByChannel(
				PadLocation + FVector::UpVector * 35.0f, FQuat::Identity, ECC_Visibility,
				FCollisionShape::MakeBox(FVector(80.0f, 80.0f, 30.0f)), QueryParams);
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
				? NSLOCTEXT("HeroArenaAbilityFeedback", "JumpPadNoGround", "前方没有可部署推进跳板的地面")
				: !bWalkableSurface
					? NSLOCTEXT("HeroArenaAbilityFeedback", "JumpPadSteepGround", "地面坡度过大，无法部署推进跳板")
					: NSLOCTEXT("HeroArenaAbilityFeedback", "JumpPadBlocked", "推进跳板的部署位置被占用");
			UGameplayMessageSubsystem::Get(this).BroadcastMessage(
				FGameplayTag::RequestGameplayTag(TEXT("Ability.UserFacingSimpleActivateFail.Message")), Message);
		}
		if (Character->HasAuthority())
		{
			UE_LOG(LogHeroArenaRelayJumpPadAbility, Log,
				TEXT("Relay jump pad placement rejected: source=%s distance=%.0f yaw_offset=%.0f ground=%s normal_z=%.2f blocked=%s"),
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
		FRotator PadRotation(0.0f, Character->GetActorRotation().Yaw, 0.0f);
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = OwnerPlayerState;
		SpawnParameters.Instigator = Character;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AHeroArenaRelayJumpPad* Pad = GetWorld()->SpawnActorDeferred<AHeroArenaRelayJumpPad>(
			AHeroArenaRelayJumpPad::StaticClass(), FTransform(PadRotation, PadLocation),
			OwnerPlayerState, Character, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Pad)
		{
			if (ULyraTeamSubsystem* Teams = GetWorld()->GetSubsystem<ULyraTeamSubsystem>())
			{
				Pad->SetGenericTeamId(IntegerToGenericTeamId(Teams->FindTeamFromObject(OwnerPlayerState)));
			}
			Pad->FinishSpawning(FTransform(PadRotation, PadLocation));
		}
		UE_LOG(LogHeroArenaRelayJumpPadAbility, Log,
			TEXT("Relay jump pad deployed: source=%s pad=%s location=%s yaw=%.1f distance=%.0f yaw_offset=%.0f"),
			*GetNameSafe(Character), *GetNameSafe(Pad), *PadLocation.ToCompactString(),
			PadRotation.Yaw, PlacementDistance, PlacementYawOffset);
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
