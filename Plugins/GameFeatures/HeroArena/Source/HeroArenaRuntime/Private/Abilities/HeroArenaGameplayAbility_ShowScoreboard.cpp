#include "Abilities/HeroArenaGameplayAbility_ShowScoreboard.h"

#include "Abilities/Tasks/AbilityTask_WaitInputRelease.h"
#include "CommonActivatableWidget.h"
#include "CommonUIExtensions.h"
#include "Player/LyraPlayerController.h"
#include "UI/HeroArenaScoreboardWidget.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaGameplayAbility_ShowScoreboard)

UHeroArenaGameplayAbility_ShowScoreboard::UHeroArenaGameplayAbility_ShowScoreboard(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalOnly;
	ActivationPolicy = ELyraAbilityActivationPolicy::OnInputTriggered;
	ActivationGroup = ELyraAbilityActivationGroup::Exclusive_Blocking;

	FGameplayTagContainer Tags;
	Tags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Ability.Type.Info.ShowLeaderboard")));
	SetAssetTags(Tags);
}

void UHeroArenaGameplayAbility_ShowScoreboard::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	ALyraPlayerController* PlayerController = GetLyraPlayerControllerFromActorInfo();
	ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
	if (!LocalPlayer)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ScoreboardWidget = UCommonUIExtensions::PushContentToLayer_ForPlayer(
		LocalPlayer, FGameplayTag::RequestGameplayTag(TEXT("UI.Layer.GameMenu")),
		UHeroArenaScoreboardWidget::StaticClass());
	if (!ScoreboardWidget)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	UAbilityTask_WaitInputRelease* WaitRelease = UAbilityTask_WaitInputRelease::WaitInputRelease(this, false);
	WaitRelease->OnRelease.AddDynamic(this, &ThisClass::HandleInputReleased);
	WaitRelease->ReadyForActivation();
}

void UHeroArenaGameplayAbility_ShowScoreboard::HandleInputReleased(const float TimeHeld)
{
	K2_EndAbility();
}

void UHeroArenaGameplayAbility_ShowScoreboard::EndAbility(
	const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const bool bReplicateEndAbility, const bool bWasCancelled)
{
	if (ScoreboardWidget)
	{
		UCommonUIExtensions::PopContentFromLayer(ScoreboardWidget);
		ScoreboardWidget = nullptr;
	}
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
