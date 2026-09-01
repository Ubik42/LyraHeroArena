#include "Abilities/HeroArenaGameplayAbility_RelayTeamAmplifier.h"

#include "AbilitySystem/Abilities/LyraAbilitySimpleFailureMessage.h"
#include "Actors/HeroArenaTeamAmplifier.h"
#include "Character/LyraCharacter.h"
#include "Effects/HeroArenaGameplayEffect_TeamAmplifier.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Teams/LyraTeamSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaGameplayAbility_RelayTeamAmplifier)

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaRelayTeamAmplifierAbility, Log, All);

UHeroArenaGameplayAbility_RelayTeamAmplifier::UHeroArenaGameplayAbility_RelayTeamAmplifier(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	ActivationPolicy = ELyraAbilityActivationPolicy::OnInputTriggered;
	CooldownGameplayEffectClass = UHeroArenaGameplayEffect_TeamAmplifierCooldown::StaticClass();
	FGameplayTagContainer Tags;
	Tags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Ability.Type.Action.RelayTeamAmplifier")));
	SetAssetTags(Tags);
}

void UHeroArenaGameplayAbility_RelayTeamAmplifier::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	ALyraCharacter* Character = GetLyraCharacterFromActorInfo();
	APlayerState* OwnerPlayerState = Character ? Character->GetPlayerState() : nullptr;
	UE_LOG(LogHeroArenaRelayTeamAmplifierAbility, Log,
		TEXT("Relay team amplifier activation requested: source=%s authority=%s"),
		*GetNameSafe(Character), Character && Character->HasAuthority() ? TEXT("true") : TEXT("false"));
	if (!Character || !OwnerPlayerState)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	const FVector Forward = Character->GetActorForwardVector();
	const FVector TraceCenter = Character->GetActorLocation() + Forward * 300.0f;
	const FVector TraceStart = TraceCenter + FVector::UpVector * 180.0f;
	const FVector TraceEnd = TraceCenter - FVector::UpVector * 420.0f;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(HeroArenaTeamAmplifierPlacement), true, Character);
	FHitResult GroundHit;
	const bool bGroundHit = GetWorld()->LineTraceSingleByChannel(
		GroundHit, TraceStart, TraceEnd, ECC_Visibility, QueryParams);
	const bool bValidGround = bGroundHit && GroundHit.ImpactNormal.Z >= 0.7f;
	const FVector Location = GroundHit.ImpactPoint + GroundHit.ImpactNormal * 5.0f;
	if (!bValidGround)
	{
		if (ActorInfo && ActorInfo->IsLocallyControlled())
		{
			FLyraAbilitySimpleFailureMessage Message;
			Message.PlayerController = Cast<APlayerController>(Character->GetController());
			Message.UserFacingReason = !bGroundHit
				? NSLOCTEXT("HeroArenaAbilityFeedback", "AmplifierNoGround", "前方没有可部署团队放大器的地面")
				: NSLOCTEXT("HeroArenaAbilityFeedback", "AmplifierSteepGround", "地面坡度过大，无法部署团队放大器");
			UGameplayMessageSubsystem::Get(this).BroadcastMessage(
				FGameplayTag::RequestGameplayTag(TEXT("Ability.UserFacingSimpleActivateFail.Message")), Message);
		}
		UE_LOG(LogHeroArenaRelayTeamAmplifierAbility, Log,
			TEXT("Relay team amplifier rejected: source=%s ground=%s normal_z=%.2f"),
			*GetNameSafe(Character), bGroundHit ? TEXT("true") : TEXT("false"), GroundHit.ImpactNormal.Z);
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	if (!CommitUltimateAbility(Handle, ActorInfo, ActivationInfo))
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
		AHeroArenaTeamAmplifier* Amplifier = GetWorld()->SpawnActorDeferred<AHeroArenaTeamAmplifier>(
			AHeroArenaTeamAmplifier::StaticClass(), FTransform(FRotator::ZeroRotator, Location),
			OwnerPlayerState, Character, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Amplifier)
		{
			if (ULyraTeamSubsystem* Teams = GetWorld()->GetSubsystem<ULyraTeamSubsystem>())
			{
				Amplifier->SetGenericTeamId(IntegerToGenericTeamId(Teams->FindTeamFromObject(OwnerPlayerState)));
			}
			Amplifier->FinishSpawning(FTransform(FRotator::ZeroRotator, Location));
		}
		UE_LOG(LogHeroArenaRelayTeamAmplifierAbility, Log,
			TEXT("Relay team amplifier deployed: source=%s actor=%s location=%s"),
			*GetNameSafe(Character), *GetNameSafe(Amplifier), *Location.ToCompactString());
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
