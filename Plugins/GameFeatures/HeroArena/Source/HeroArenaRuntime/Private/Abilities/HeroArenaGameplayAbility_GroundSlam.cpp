#include "Abilities/HeroArenaGameplayAbility_GroundSlam.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Actors/HeroArenaGroundSlamPulse.h"
#include "Actors/HeroArenaGroundSlamSlow.h"
#include "Character/LyraCharacter.h"
#include "Character/LyraHealthComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Effects/HeroArenaGameplayEffect_GroundSlamCooldown.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "LyraGameplayTags.h"
#include "System/LyraAssetManager.h"
#include "System/LyraGameData.h"
#include "Teams/LyraTeamSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaGameplayAbility_GroundSlam)

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaGroundSlam, Log, All);

UHeroArenaGameplayAbility_GroundSlam::UHeroArenaGameplayAbility_GroundSlam(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	ActivationPolicy = ELyraAbilityActivationPolicy::OnInputTriggered;
	ActivationGroup = ELyraAbilityActivationGroup::Exclusive_Replaceable;
	CooldownGameplayEffectClass = UHeroArenaGameplayEffect_GroundSlamCooldown::StaticClass();

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Ability.Type.Action.GroundSlam")));
	SetAssetTags(AssetTags);
}

void UHeroArenaGameplayAbility_GroundSlam::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	ALyraCharacter* Character = GetLyraCharacterFromActorInfo();
	if (!IsValid(Character) || !CommitUltimateAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	const FRotator FacingRotation(0.0f,
		Character->GetController() ? Character->GetController()->GetControlRotation().Yaw : Character->GetActorRotation().Yaw,
		0.0f);
	const FVector Forward = FacingRotation.Vector().GetSafeNormal2D();
	const FVector Origin = Character->GetActorLocation();
	int32 HitCount = 0;

	if (Character->HasAuthority())
	{
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(HeroArenaGroundSlam), false, Character);
		TArray<FOverlapResult> Overlaps;
		GetWorld()->OverlapMultiByObjectType(Overlaps, Origin, FQuat::Identity,
			FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeSphere(Range), QueryParams);

		TSet<TObjectPtr<AActor>> CheckedActors;
		for (const FOverlapResult& Overlap : Overlaps)
		{
			AActor* TargetActor = Overlap.GetActor();
			if (IsValid(TargetActor) && !CheckedActors.Contains(TargetActor))
			{
				CheckedActors.Add(TargetActor);
				HitCount += TryAffectEnemy(TargetActor, Origin, Forward) ? 1 : 0;
			}
		}

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = Character;
		SpawnParameters.Instigator = Character;
		AHeroArenaGroundSlamPulse* Pulse = GetWorld()->SpawnActor<AHeroArenaGroundSlamPulse>(
			AHeroArenaGroundSlamPulse::StaticClass(), Origin, FacingRotation, SpawnParameters);
		if (IsValid(Pulse))
		{
			const ULyraTeamSubsystem* TeamSubsystem = GetWorld()->GetSubsystem<ULyraTeamSubsystem>();
			Pulse->InitializePulse(TeamSubsystem ? TeamSubsystem->FindTeamFromObject(Character) : INDEX_NONE, Range);
		}
	}

	UE_LOG(LogHeroArenaGroundSlam, Log,
		TEXT("Ground slam activated: source=%s hits=%d range=%.0f authority=%s"),
		*GetNameSafe(Character), HitCount, Range, Character->HasAuthority() ? TEXT("true") : TEXT("false"));
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

bool UHeroArenaGameplayAbility_GroundSlam::TryAffectEnemy(AActor* TargetActor, const FVector& Origin,
	const FVector& Forward)
{
	AActor* SourceActor = GetAvatarActorFromActorInfo();
	const ULyraTeamSubsystem* TeamSubsystem = GetWorld()->GetSubsystem<ULyraTeamSubsystem>();
	if (!IsValid(SourceActor) || !IsValid(TargetActor) || !IsValid(TeamSubsystem) ||
		!TeamSubsystem->CanCauseDamage(SourceActor, TargetActor, false))
	{
		return false;
	}

	const FVector Offset = TargetActor->GetActorLocation() - Origin;
	if (FMath::Abs(Offset.Z) > VerticalTolerance || Offset.Size2D() > Range ||
		FVector::DotProduct(Offset.GetSafeNormal2D(), Forward) < MinimumForwardDot)
	{
		return false;
	}

	FCollisionQueryParams VisibilityParams(SCENE_QUERY_STAT(HeroArenaGroundSlamVisibility), true, SourceActor);
	FHitResult VisibilityHit;
	const FVector TraceOffset(0.0f, 0.0f, 50.0f);
	if (GetWorld()->LineTraceSingleByChannel(VisibilityHit, Origin + TraceOffset,
		TargetActor->GetActorLocation() + TraceOffset, ECC_Visibility, VisibilityParams)
		&& VisibilityHit.GetActor() != TargetActor)
	{
		return false;
	}

	ULyraHealthComponent* HealthComponent = ULyraHealthComponent::FindHealthComponent(TargetActor);
	UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo();
	UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(TargetActor);
	if (!IsValid(HealthComponent) || HealthComponent->IsDeadOrDying() || !IsValid(SourceASC) || !IsValid(TargetASC))
	{
		return false;
	}

	const TSubclassOf<UGameplayEffect> DamageEffectClass =
		ULyraAssetManager::GetSubclass(ULyraGameData::Get().DamageGameplayEffect_SetByCaller);
	if (!DamageEffectClass)
	{
		return false;
	}

	FHitResult HitResult(TargetActor, Cast<UPrimitiveComponent>(TargetActor->GetRootComponent()),
		TargetActor->GetActorLocation(), -Forward);
	FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();
	Context.AddSourceObject(this);
	Context.AddHitResult(HitResult);
	FGameplayEffectSpecHandle SpecHandle = SourceASC->MakeOutgoingSpec(DamageEffectClass, 1.0f, Context);
	if (!SpecHandle.IsValid())
	{
		return false;
	}

	const float OldHealth = HealthComponent->GetHealth();
	SpecHandle.Data->SetSetByCallerMagnitude(LyraGameplayTags::SetByCaller_Damage, Damage);
	TargetASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());

	const FVector KnockbackDirection = Offset.GetSafeNormal2D();
	if (ACharacter* TargetCharacter = Cast<ACharacter>(TargetActor))
	{
		TargetCharacter->LaunchCharacter(
			KnockbackDirection * KnockbackStrength + FVector::UpVector * KnockbackLift, true, true);
	}

	bool bHasActiveSlow = false;
	for (TActorIterator<AHeroArenaGroundSlamSlow> It(GetWorld()); It; ++It)
	{
		if (It->GetTargetActor() == TargetActor)
		{
			bHasActiveSlow = true;
			break;
		}
	}
	if (!bHasActiveSlow)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = SourceActor;
		SpawnParameters.Instigator = Cast<APawn>(SourceActor);
		AHeroArenaGroundSlamSlow* Slow = GetWorld()->SpawnActor<AHeroArenaGroundSlamSlow>(
			AHeroArenaGroundSlamSlow::StaticClass(), TargetActor->GetActorLocation(), FRotator::ZeroRotator, SpawnParameters);
		if (IsValid(Slow))
		{
			Slow->InitializeSlow(TargetActor, SlowMultiplier, SlowDuration);
		}
	}

	UE_LOG(LogHeroArenaGroundSlam, Log,
		TEXT("Ground slam hit: target=%s health=%.0f->%.0f knockback=%.0f slow=%.2fx/%.1fs authority=true"),
		*GetNameSafe(TargetActor), OldHealth, HealthComponent->GetHealth(), KnockbackStrength,
		SlowMultiplier, SlowDuration);
	return true;
}
