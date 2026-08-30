#include "Effects/HeroArenaPulseShotExecution.h"

#include "AbilitySystem/Attributes/LyraCombatSet.h"
#include "AbilitySystem/Attributes/LyraHealthSet.h"
#include "AbilitySystem/LyraAbilitySourceInterface.h"
#include "AbilitySystem/LyraGameplayEffectContext.h"
#include "Effects/HeroArenaHealingRules.h"
#include "Engine/World.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Teams/LyraTeamSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaPulseShotExecution)

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaPulseShot, Log, All);

namespace HeroArenaPulseShot
{
	struct FCaptures
	{
		FGameplayEffectAttributeCaptureDefinition BaseDamage;

		FCaptures()
			: BaseDamage(ULyraCombatSet::GetBaseDamageAttribute(), EGameplayEffectAttributeCaptureSource::Source, true)
		{
		}
	};

	const FCaptures& Captures()
	{
		static const FCaptures Value;
		return Value;
	}
}

UHeroArenaPulseShotExecution::UHeroArenaPulseShotExecution()
{
	RelevantAttributesToCapture.Add(HeroArenaPulseShot::Captures().BaseDamage);
}

void UHeroArenaPulseShotExecution::Execute_Implementation(
	const FGameplayEffectCustomExecutionParameters& ExecutionParams,
	FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const
{
#if WITH_SERVER_CODE
	const FGameplayEffectSpec& Spec = ExecutionParams.GetOwningSpec();
	const FLyraGameplayEffectContext* Context = FLyraGameplayEffectContext::ExtractEffectContext(Spec.GetContext());
	if (!Context)
	{
		UE_LOG(LogHeroArenaPulseShot, Warning, TEXT("Pulse shot execution skipped: missing Lyra effect context"));
		return;
	}

	UAbilitySystemComponent* TargetASC = ExecutionParams.GetTargetAbilitySystemComponent();
	AActor* TargetActor = TargetASC ? TargetASC->GetAvatarActor_Direct() : nullptr;
	const FHitResult* HitResult = Context->GetHitResult();
	if (HitResult && HitResult->HitObjectHandle.FetchActor())
	{
		TargetActor = HitResult->HitObjectHandle.FetchActor();
	}
	AActor* EffectCauser = Context->GetEffectCauser();
	ULyraTeamSubsystem* Teams = TargetActor && TargetActor->GetWorld()
		? TargetActor->GetWorld()->GetSubsystem<ULyraTeamSubsystem>() : nullptr;
	if (!TargetActor || !EffectCauser || !Teams)
	{
		UE_LOG(LogHeroArenaPulseShot, Warning,
			TEXT("Pulse shot execution skipped: target=%s causer=%s teams=%s"),
			*GetNameSafe(TargetActor), *GetNameSafe(EffectCauser), *GetNameSafe(Teams));
		return;
	}

	int32 SourceTeamId = INDEX_NONE;
	int32 TargetTeamId = INDEX_NONE;
	const ELyraTeamComparison Relationship = Teams->CompareTeams(
		EffectCauser, TargetActor, SourceTeamId, TargetTeamId);
	if (Relationship != ELyraTeamComparison::OnSameTeam && Relationship != ELyraTeamComparison::DifferentTeams)
	{
		UE_LOG(LogHeroArenaPulseShot, Warning,
			TEXT("Pulse shot execution skipped: invalid relationship causer=%s team=%d target=%s team=%d"),
			*GetNameSafe(EffectCauser), SourceTeamId, *GetNameSafe(TargetActor), TargetTeamId);
		return;
	}

	FAggregatorEvaluateParameters Evaluation;
	Evaluation.SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
	Evaluation.TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();
	float BaseDamage = 0.0f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
		HeroArenaPulseShot::Captures().BaseDamage, Evaluation, BaseDamage);

	FVector ImpactLocation = TargetActor->GetActorLocation();
	if (HitResult)
	{
		ImpactLocation = HitResult->ImpactPoint;
	}
	double Distance = Context->HasOrigin()
		? FVector::Dist(Context->GetOrigin(), ImpactLocation)
		: FVector::Dist(EffectCauser->GetActorLocation(), ImpactLocation);
	float DistanceAttenuation = 1.0f;
	float PhysicalMaterialAttenuation = 1.0f;
	if (const ILyraAbilitySourceInterface* AbilitySource = Cast<ILyraAbilitySourceInterface>(Context->GetSourceObject()))
	{
		DistanceAttenuation = AbilitySource->GetDistanceAttenuation(
			Distance, Evaluation.SourceTags, Evaluation.TargetTags);
		if (Relationship == ELyraTeamComparison::DifferentTeams)
		{
			if (const UPhysicalMaterial* PhysicalMaterial = HitResult ? HitResult->PhysMaterial.Get() : nullptr)
			{
				PhysicalMaterialAttenuation = AbilitySource->GetPhysicalMaterialAttenuation(
					PhysicalMaterial, Evaluation.SourceTags, Evaluation.TargetTags);
			}
		}
	}

	const float AttenuatedBase = FMath::Max(BaseDamage * FMath::Max(DistanceAttenuation, 0.0f), 0.0f);
	if (Relationship == ELyraTeamComparison::OnSameTeam)
	{
		const float Heal = AttenuatedBase * AllyHealMultiplier * UHeroArenaHealingRules::GetIncomingHealingMultiplier(TargetASC);
		if (Heal > 0.0f)
		{
			OutExecutionOutput.AddOutputModifier(FGameplayModifierEvaluatedData(
				ULyraHealthSet::GetHealingAttribute(), EGameplayModOp::Additive, Heal));
			UE_LOG(LogHeroArenaPulseShot, Log, TEXT("Pulse shot healed %s for %.2f from %s"),
				*GetNameSafe(TargetActor), Heal, *GetNameSafe(EffectCauser));
		}
	}
	else
	{
		const float Damage = AttenuatedBase * PhysicalMaterialAttenuation;
		if (Damage > 0.0f)
		{
			OutExecutionOutput.AddOutputModifier(FGameplayModifierEvaluatedData(
				ULyraHealthSet::GetDamageAttribute(), EGameplayModOp::Additive, Damage));
			UE_LOG(LogHeroArenaPulseShot, Log, TEXT("Pulse shot damaged %s for %.2f from %s"),
				*GetNameSafe(TargetActor), Damage, *GetNameSafe(EffectCauser));
		}
	}
#endif
}
