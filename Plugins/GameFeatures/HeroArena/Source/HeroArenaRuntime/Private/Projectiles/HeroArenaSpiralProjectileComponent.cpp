// Copyright Epic Games, Inc. All Rights Reserved.

#include "Projectiles/HeroArenaSpiralProjectileComponent.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Actors/HeroArenaBarrier.h"
#include "GameFramework/Actor.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GameplayEffect.h"
#include "LyraGameplayTags.h"
#include "System/LyraAssetManager.h"
#include "System/LyraGameData.h"
#include "Teams/LyraTeamSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaProjectile, Log, All);

UHeroArenaSpiralProjectileComponent::UHeroArenaSpiralProjectileComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UHeroArenaSpiralProjectileComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (!IsValid(Owner))
	{
		SetComponentTickEnabled(false);
		return;
	}

	if (!Owner->HasAuthority())
	{
		UE_LOG(LogHeroArenaProjectile, Log,
			TEXT("Spiral projectile replicated to client: %s authority=false"), *GetNameSafe(Owner));
		SetComponentTickEnabled(false);
		return;
	}

	ProjectileMovement = Owner->FindComponentByClass<UProjectileMovementComponent>();
	if (!IsValid(ProjectileMovement))
	{
		UE_LOG(LogHeroArenaProjectile, Warning, TEXT("Spiral motion disabled for %s: no ProjectileMovementComponent"), *GetNameSafe(Owner));
		SetComponentTickEnabled(false);
		return;
	}

	const FVector InitialDirection = ProjectileMovement->Velocity.GetSafeNormal();
	RebuildFrame(InitialDirection.IsNearlyZero() ? Owner->GetActorForwardVector() : InitialDirection);

	UE_LOG(LogHeroArenaProjectile, Log,
		TEXT("Spiral projectile started: %s radius=%.0f turns=%.2f speed=%.0f authority=true"),
		*GetNameSafe(Owner), SpiralRadius, TurnsPerSecond, ProjectileMovement->Velocity.Size());
}

bool UHeroArenaSpiralProjectileComponent::TryApplyBarrierImpact(AHeroArenaBarrier* Barrier, const FHitResult& Hit)
{
	AActor* Owner = GetOwner();
	AActor* Instigator = Owner ? Owner->GetInstigator() : nullptr;
	const ULyraTeamSubsystem* TeamSubsystem = GetWorld() ? GetWorld()->GetSubsystem<ULyraTeamSubsystem>() : nullptr;
	if (bAppliedBarrierImpactDamage || !Owner || !Owner->HasAuthority() || !IsValid(Barrier) ||
		!IsValid(Instigator) || !IsValid(TeamSubsystem) ||
		!TeamSubsystem->CanCauseDamage(Instigator, Barrier, false))
	{
		return false;
	}

	UAbilitySystemComponent* SourceASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Instigator);
	UAbilitySystemComponent* TargetASC = Barrier->GetAbilitySystemComponent();
	const TSubclassOf<UGameplayEffect> DamageEffectClass =
		ULyraAssetManager::GetSubclass(ULyraGameData::Get().DamageGameplayEffect_SetByCaller);
	if (!IsValid(SourceASC) || !IsValid(TargetASC) || !DamageEffectClass)
	{
		UE_LOG(LogHeroArenaProjectile, Warning,
			TEXT("Barrier impact damage skipped for %s: source_asc=%s target_asc=%s effect=%s"),
			*GetNameSafe(Owner), *GetNameSafe(SourceASC), *GetNameSafe(TargetASC), *GetNameSafe(DamageEffectClass));
		return false;
	}

	FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();
	Context.AddSourceObject(Owner);
	Context.AddHitResult(Hit);
	FGameplayEffectSpecHandle SpecHandle = SourceASC->MakeOutgoingSpec(DamageEffectClass, 1.0f, Context);
	if (!SpecHandle.IsValid())
	{
		return false;
	}

	const float OldHealth = Barrier->GetBarrierHealth();
	SpecHandle.Data->SetSetByCallerMagnitude(LyraGameplayTags::SetByCaller_Damage, BarrierImpactDamage);
	TargetASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
	bAppliedBarrierImpactDamage = true;

	UE_LOG(LogHeroArenaProjectile, Log,
		TEXT("Spiral projectile hit enemy barrier: projectile=%s barrier=%s health=%.0f->%.0f damage=%.0f authority=true"),
		*GetNameSafe(Owner), *GetNameSafe(Barrier), OldHealth, Barrier->GetBarrierHealth(), BarrierImpactDamage);
	return true;
}

void UHeroArenaSpiralProjectileComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!IsValid(ProjectileMovement) || !ProjectileMovement->IsActive() || DeltaTime <= 0.0f)
	{
		return;
	}

	// Remove only the offset injected last frame. Gravity, collision response and
	// bounce velocity produced by ProjectileMovement remain owned by Lyra/Engine.
	const FVector BaseVelocity = ProjectileMovement->Velocity - LastInjectedVelocity;
	const FVector BaseDirection = BaseVelocity.GetSafeNormal();
	if (!BaseDirection.IsNearlyZero() && FVector::DotProduct(BaseDirection, ForwardAxis) < DirectionResetDotThreshold)
	{
		RebuildFrame(BaseDirection);
		ElapsedTime = 0.0f;
	}

	ElapsedTime += DeltaTime;
	const float AngularSpeed = 2.0f * PI * TurnsPerSecond;
	const float Phase = AngularSpeed * ElapsedTime;
	const float TangentialSpeed = AngularSpeed * SpiralRadius;
	const FVector NewInjectedVelocity = TangentialSpeed *
		((-FMath::Sin(Phase) * SpiralAxisX) + (FMath::Cos(Phase) * SpiralAxisY));

	ProjectileMovement->Velocity = BaseVelocity + NewInjectedVelocity;
	LastInjectedVelocity = NewInjectedVelocity;
}

void UHeroArenaSpiralProjectileComponent::RebuildFrame(const FVector& ForwardDirection)
{
	ForwardAxis = ForwardDirection.GetSafeNormal();
	if (ForwardAxis.IsNearlyZero())
	{
		ForwardAxis = FVector::ForwardVector;
	}

	const FVector ReferenceAxis = FMath::Abs(ForwardAxis.Z) < 0.9f ? FVector::UpVector : FVector::RightVector;
	SpiralAxisX = FVector::CrossProduct(ReferenceAxis, ForwardAxis).GetSafeNormal();
	SpiralAxisY = FVector::CrossProduct(ForwardAxis, SpiralAxisX).GetSafeNormal();
	LastInjectedVelocity = FVector::ZeroVector;
}
