#include "Abilities/HeroArenaGameplayAbility_OverclockInjection.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Actors/HeroArenaWeaponOverloadIndicator.h"
#include "Character/LyraCharacter.h"
#include "Character/LyraHealthComponent.h"
#include "Effects/HeroArenaGameplayEffect_OverclockInjection.h"
#include "GameFramework/Controller.h"
#include "Teams/LyraTeamSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaGameplayAbility_OverclockInjection)

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaOverclockInjection, Log, All);

UHeroArenaGameplayAbility_OverclockInjection::UHeroArenaGameplayAbility_OverclockInjection(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	ActivationPolicy = ELyraAbilityActivationPolicy::OnInputTriggered;
	CooldownGameplayEffectClass = UHeroArenaGameplayEffect_OverclockInjectionCooldown::StaticClass();

	FGameplayTagContainer Tags;
	Tags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Ability.Type.Action.OverclockInjection")));
	SetAssetTags(Tags);
}

void UHeroArenaGameplayAbility_OverclockInjection::ActivateAbility(
	FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	ALyraCharacter* Source = GetLyraCharacterFromActorInfo();
	AController* Controller = Source ? Source->GetController() : nullptr;
	if (!Source || !Controller)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(HeroArenaOverclockInjection), true, Source);
	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		Hit, ViewLocation, ViewLocation + ViewRotation.Vector() * TraceRange, ECC_Visibility, QueryParams);
	AActor* Target = bHit ? Hit.GetActor() : nullptr;
	ULyraHealthComponent* TargetHealth = ULyraHealthComponent::FindHealthComponent(Target);
	UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target);
	ULyraTeamSubsystem* Teams = GetWorld()->GetSubsystem<ULyraTeamSubsystem>();
	int32 SourceTeam = INDEX_NONE;
	int32 TargetTeam = INDEX_NONE;
	const ELyraTeamComparison Relationship = Teams && Target
		? Teams->CompareTeams(Source, Target, SourceTeam, TargetTeam) : ELyraTeamComparison::InvalidArgument;

	auto Reject = [&](const TCHAR* Reason)
	{
		if (Source->HasAuthority())
		{
			UE_LOG(LogHeroArenaOverclockInjection, Log,
				TEXT("Overclock injection rejected: reason=%s source=%s team=%d target=%s team=%d"),
				Reason, *GetNameSafe(Source), SourceTeam, *GetNameSafe(Target), TargetTeam);
		}
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
	};

	if (!Target || !TargetHealth || !TargetASC)
	{
		Reject(TEXT("no_valid_target"));
		return;
	}
	if (Target == Source)
	{
		Reject(TEXT("self_target"));
		return;
	}
	if (Relationship != ELyraTeamComparison::OnSameTeam || TargetHealth->IsDeadOrDying())
	{
		Reject(TEXT("not_living_ally"));
		return;
	}
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		Reject(TEXT("commit_failed"));
		return;
	}

	if (Source->HasAuthority())
	{
		FGameplayEffectContextHandle Context = GetAbilitySystemComponentFromActorInfo()->MakeEffectContext();
		Context.AddSourceObject(Source);
		TargetASC->ApplyGameplayEffectToSelf(GetDefault<UHeroArenaGameplayEffect_OverclockInjection>(), 1.0f, Context);

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = Source;
		SpawnParameters.Instigator = Source;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AHeroArenaWeaponOverloadIndicator* Indicator = GetWorld()->SpawnActor<AHeroArenaWeaponOverloadIndicator>(
			AHeroArenaWeaponOverloadIndicator::StaticClass(), Target->GetActorLocation(), FRotator::ZeroRotator, SpawnParameters))
		{
			Indicator->InitializeIndicator(Target, 8.0f);
		}

		UE_LOG(LogHeroArenaOverclockInjection, Log,
			TEXT("Overclock injection resolved: source=%s target=%s duration=8.0 damage_multiplier=1.25 tag_count=%d"),
			*GetNameSafe(Source), *GetNameSafe(Target),
			TargetASC->GetTagCount(FGameplayTag::RequestGameplayTag(TEXT("Status.HeroArena.Overclocked"))));
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
