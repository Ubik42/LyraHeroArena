#include "Actors/HeroArenaStickyBomb.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Character/LyraHealthComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Effects/HeroArenaGameplayEffect_StickyBomb.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "LyraGameplayTags.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Teams/LyraTeamSubsystem.h"
#include "TimerManager.h"
#include "System/LyraAssetManager.h"
#include "System/LyraGameData.h"
#include "UObject/ConstructorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaStickyBomb)

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaStickyBomb, Log, All);

AHeroArenaStickyBomb::AHeroArenaStickyBomb()
{
	bReplicates = true;
	SetReplicateMovement(true);
	SetNetUpdateFrequency(30.0f);
	InitialLifeSpan = 6.0f;

	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(14.0f);
	Collision->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	SetRootComponent(Collision);

	BombMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BombMesh"));
	BombMesh->SetupAttachment(Collision);
	BombMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BombMesh->SetRelativeScale3D(FVector(0.75f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(
		TEXT("/Game/Weapons/Grenade/Mesh/SM_grenade.SM_grenade"));
	if (Mesh.Succeeded())
	{
		BombMesh->SetStaticMesh(Mesh.Object);
	}

	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->UpdatedComponent = Collision;
	ProjectileMovement->InitialSpeed = 2200.0f;
	ProjectileMovement->MaxSpeed = 2200.0f;
	ProjectileMovement->ProjectileGravityScale = 0.55f;
	ProjectileMovement->bShouldBounce = false;

	TrailFx = CreateDefaultSubobject<UNiagaraComponent>(TEXT("TrailFx"));
	TrailFx->SetupAttachment(Collision);
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> Trail(
		TEXT("/Game/Effects/Particles/Explosion/NS_Grenade_Trail.NS_Grenade_Trail"));
	if (Trail.Succeeded())
	{
		TrailFx->SetAsset(Trail.Object);
	}
}

void AHeroArenaStickyBomb::BeginPlay()
{
	Super::BeginPlay();
	Collision->IgnoreActorWhenMoving(GetInstigator(), true);
	Collision->OnComponentHit.AddDynamic(this, &ThisClass::OnCollisionHit);
	if (HasAuthority())
	{
		constexpr float MaxAirFuseSeconds = 3.0f;
		DetonationServerTime = GetWorld()->GetTimeSeconds() + MaxAirFuseSeconds;
		GetWorldTimerManager().SetTimer(FuseTimer, this, &ThisClass::Detonate, MaxAirFuseSeconds, false);
	}
}

void AHeroArenaStickyBomb::InitializeBomb(
	UAbilitySystemComponent* InSourceASC, AActor* InSourceTeamAgent, const FVector& Velocity)
{
	check(HasAuthority());
	SourceASC = InSourceASC;
	SourceTeamAgent = InSourceTeamAgent;
	ProjectileMovement->Velocity = Velocity;
}

void AHeroArenaStickyBomb::OnCollisionHit(UPrimitiveComponent*, AActor* OtherActor, UPrimitiveComponent*,
	FVector, const FHitResult& Hit)
{
	if (!HasAuthority() || bStuck || bDetonated || OtherActor == GetInstigator())
	{
		return;
	}

	UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(OtherActor);
	ULyraHealthComponent* TargetHealth = ULyraHealthComponent::FindHealthComponent(OtherActor);
	ULyraTeamSubsystem* Teams = GetWorld()->GetSubsystem<ULyraTeamSubsystem>();
	const bool bLivingEnemy = TargetASC && TargetHealth && !TargetHealth->IsDeadOrDying() && Teams &&
		Teams->CompareTeams(SourceTeamAgent.Get(), OtherActor) == ELyraTeamComparison::DifferentTeams;

	if (bLivingEnemy)
	{
		StickToEnemy(OtherActor, TargetASC);
	}
	else
	{
		StickToWorld(Hit);
	}
}

void AHeroArenaStickyBomb::StickToEnemy(AActor* Target, UAbilitySystemComponent* TargetASC)
{
	bStuck = true;
	StuckTarget = Target;
	AttachedTargetASC = TargetASC;
	ProjectileMovement->StopMovementImmediately();
	ProjectileMovement->Deactivate();
	Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	AttachToComponent(Target->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
	StuckRelativeLocation = GetRootComponent()->GetRelativeLocation();
	SetReplicateMovement(false);

	if (UAbilitySystemComponent* ValidSourceASC = SourceASC.Get())
	{
		FGameplayEffectContextHandle Context = ValidSourceASC->MakeEffectContext();
		Context.AddSourceObject(this);
		FGameplayEffectSpecHandle Spec = ValidSourceASC->MakeOutgoingSpec(
			UHeroArenaGameplayEffect_StickyBombAttached::StaticClass(), 1.0f, Context);
		if (Spec.IsValid())
		{
			AttachedEffectHandle = TargetASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
		}
	}

	DetonationServerTime = GetWorld()->GetTimeSeconds() + EnemyFuseSeconds;
	GetWorldTimerManager().SetTimer(FuseTimer, this, &ThisClass::Detonate, EnemyFuseSeconds, false);
	GetWorldTimerManager().SetTimer(AttachmentCheckTimer, this, &ThisClass::CheckAttachedState, 0.05f, true);
	ForceNetUpdate();

	UE_LOG(LogHeroArenaStickyBomb, Log,
		TEXT("Sticky bomb attached: source=%s target=%s fuse=%.2f effect=%s relative=%s"),
		*GetNameSafe(SourceTeamAgent.Get()), *GetNameSafe(Target), EnemyFuseSeconds,
		AttachedEffectHandle.IsValid() ? TEXT("valid") : TEXT("invalid"), *StuckRelativeLocation.ToCompactString());
}

void AHeroArenaStickyBomb::StickToWorld(const FHitResult& Hit)
{
	bStuck = true;
	StuckTarget = nullptr;
	ProjectileMovement->StopMovementImmediately();
	ProjectileMovement->Deactivate();
	Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetActorLocation(Hit.ImpactPoint + Hit.ImpactNormal * 2.0f);
	SetReplicateMovement(true);
	DetonationServerTime = GetWorld()->GetTimeSeconds() + WorldFuseSeconds;
	GetWorldTimerManager().SetTimer(FuseTimer, this, &ThisClass::Detonate, WorldFuseSeconds, false);
	ForceNetUpdate();

	UE_LOG(LogHeroArenaStickyBomb, Log,
		TEXT("Sticky bomb fixed to world: source=%s hit=%s fuse=%.2f"),
		*GetNameSafe(SourceTeamAgent.Get()), *GetNameSafe(Hit.GetActor()), WorldFuseSeconds);
}

void AHeroArenaStickyBomb::CheckAttachedState()
{
	if (!HasAuthority() || bDetonated || !StuckTarget)
	{
		GetWorldTimerManager().ClearTimer(AttachmentCheckTimer);
		return;
	}

	ULyraHealthComponent* Health = ULyraHealthComponent::FindHealthComponent(StuckTarget);
	if (!IsValid(StuckTarget) || !Health || Health->IsDeadOrDying())
	{
		ClearAttachedEffect();
		DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		StuckTarget = nullptr;
		AttachedTargetASC.Reset();
		SetReplicateMovement(true);
		ForceNetUpdate();
		GetWorldTimerManager().ClearTimer(AttachmentCheckTimer);
		UE_LOG(LogHeroArenaStickyBomb, Log,
			TEXT("Sticky bomb target died or vanished; fuse continues at %s"), *GetActorLocation().ToCompactString());
		return;
	}

	UAbilitySystemComponent* TargetASC = AttachedTargetASC.Get();
	if (!TargetASC || !AttachedEffectHandle.IsValid() || !TargetASC->GetActiveGameplayEffect(AttachedEffectHandle))
	{
		Disarm(TEXT("purified"));
	}
}

void AHeroArenaStickyBomb::Detonate()
{
	if (!HasAuthority() || bDetonated)
	{
		return;
	}

	bDetonated = true;
	GetWorldTimerManager().ClearTimer(AttachmentCheckTimer);
	ClearAttachedEffect();
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	ForceNetUpdate();

	int32 DamagedTargets = 0;
	UAbilitySystemComponent* ValidSourceASC = SourceASC.Get();
	ULyraTeamSubsystem* Teams = GetWorld()->GetSubsystem<ULyraTeamSubsystem>();
	if (ValidSourceASC && Teams && SourceTeamAgent.IsValid())
	{
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(HeroArenaStickyBomb), false, this);
		TArray<FOverlapResult> Results;
		GetWorld()->OverlapMultiByObjectType(Results, GetActorLocation(), FQuat::Identity,
			FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeSphere(ExplosionRadius), QueryParams);
		TSet<TObjectPtr<AActor>> Seen;
		for (const FOverlapResult& Result : Results)
		{
			AActor* Target = Result.GetActor();
			if (!IsValid(Target) || Seen.Contains(Target))
			{
				continue;
			}
			Seen.Add(Target);
			ULyraHealthComponent* Health = ULyraHealthComponent::FindHealthComponent(Target);
			UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target);
			if (!Health || Health->IsDeadOrDying() || !TargetASC ||
				Teams->CompareTeams(SourceTeamAgent.Get(), Target) != ELyraTeamComparison::DifferentTeams ||
				!HasLineOfSightTo(Target))
			{
				continue;
			}
			ApplyDamage(Target, TargetASC);
			++DamagedTargets;
		}
	}

	UE_LOG(LogHeroArenaStickyBomb, Log,
		TEXT("Sticky bomb detonated: source=%s location=%s targets=%d radius=%.0f damage=%.0f"),
		*GetNameSafe(SourceTeamAgent.Get()), *GetActorLocation().ToCompactString(), DamagedTargets,
		ExplosionRadius, ExplosionDamage);

	PlayDetonationFeedback();
	Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BombMesh->SetVisibility(false);
	SetLifeSpan(0.75f);
}

void AHeroArenaStickyBomb::Disarm(const TCHAR* Reason)
{
	if (!HasAuthority() || bDetonated)
	{
		return;
	}

	GetWorldTimerManager().ClearTimer(FuseTimer);
	GetWorldTimerManager().ClearTimer(AttachmentCheckTimer);
	ClearAttachedEffect();
	UE_LOG(LogHeroArenaStickyBomb, Log, TEXT("Sticky bomb disarmed: reason=%s target=%s"),
		Reason, *GetNameSafe(StuckTarget));
	Destroy();
}

void AHeroArenaStickyBomb::ClearAttachedEffect()
{
	if (UAbilitySystemComponent* TargetASC = AttachedTargetASC.Get();
		TargetASC && AttachedEffectHandle.IsValid() && TargetASC->GetActiveGameplayEffect(AttachedEffectHandle))
	{
		TargetASC->RemoveActiveGameplayEffect(AttachedEffectHandle);
	}
	AttachedEffectHandle.Invalidate();
}

bool AHeroArenaStickyBomb::HasLineOfSightTo(AActor* Target) const
{
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(HeroArenaStickyBombOcclusion), true, this);
	QueryParams.AddIgnoredActor(SourceTeamAgent.Get());
	FHitResult Hit;
	const FVector Start = GetActorLocation();
	const FVector End = Target->GetActorLocation() + FVector::UpVector * 45.0f;
	const bool bBlocked = GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, QueryParams);
	return !bBlocked || Hit.GetActor() == Target;
}

void AHeroArenaStickyBomb::ApplyDamage(AActor* Target, UAbilitySystemComponent* TargetASC)
{
	UAbilitySystemComponent* ValidSourceASC = SourceASC.Get();
	if (!ValidSourceASC)
	{
		return;
	}
	const TSubclassOf<UGameplayEffect> DamageEffect =
		ULyraAssetManager::GetSubclass(ULyraGameData::Get().DamageGameplayEffect_SetByCaller);
	FGameplayEffectContextHandle Context = ValidSourceASC->MakeEffectContext();
	Context.AddSourceObject(this);
	Context.AddInstigator(SourceTeamAgent.Get(), this);
	FGameplayEffectSpecHandle Spec = ValidSourceASC->MakeOutgoingSpec(DamageEffect, 1.0f, Context);
	if (Spec.IsValid())
	{
		Spec.Data->SetSetByCallerMagnitude(LyraGameplayTags::SetByCaller_Damage, ExplosionDamage);
		ValidSourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), TargetASC);
		UE_LOG(LogHeroArenaStickyBomb, Verbose, TEXT("Sticky bomb damage committed: target=%s amount=%.0f"),
			*GetNameSafe(Target), ExplosionDamage);
	}
}

void AHeroArenaStickyBomb::OnRep_AttachmentState()
{
	ApplyReplicatedAttachment();
	UE_LOG(LogHeroArenaStickyBomb, Log,
		TEXT("Sticky bomb attachment replicated: target=%s stuck=%s relative=%s detonation_time=%.2f"),
		*GetNameSafe(StuckTarget), bStuck ? TEXT("true") : TEXT("false"),
		*StuckRelativeLocation.ToCompactString(), DetonationServerTime);
}

void AHeroArenaStickyBomb::ApplyReplicatedAttachment()
{
	if (!bStuck)
	{
		return;
	}
	ProjectileMovement->StopMovementImmediately();
	ProjectileMovement->Deactivate();
	Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (StuckTarget && GetAttachParentActor() != StuckTarget)
	{
		AttachToComponent(StuckTarget->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
		SetActorRelativeLocation(StuckRelativeLocation);
	}
	else if (!StuckTarget && GetAttachParentActor())
	{
		DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	}
}

void AHeroArenaStickyBomb::OnRep_Detonated()
{
	if (bDetonated)
	{
		PlayDetonationFeedback();
		Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		BombMesh->SetVisibility(false);
		UE_LOG(LogHeroArenaStickyBomb, Log, TEXT("Sticky bomb detonation replicated: location=%s"),
			*GetActorLocation().ToCompactString());
	}
}

void AHeroArenaStickyBomb::PlayDetonationFeedback()
{
	if (TrailFx)
	{
		TrailFx->Deactivate();
	}
	static TSoftObjectPtr<UNiagaraSystem> Explosion(
		FSoftObjectPath(TEXT("/Game/Effects/Particles/Explosion/NS_Grenade_Explosion.NS_Grenade_Explosion")));
	if (UNiagaraSystem* System = Explosion.LoadSynchronous())
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			this, System, GetActorLocation(), FRotator::ZeroRotator, FVector(0.9f));
	}
}

void AHeroArenaStickyBomb::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, StuckTarget);
	DOREPLIFETIME(ThisClass, StuckRelativeLocation);
	DOREPLIFETIME(ThisClass, bStuck);
	DOREPLIFETIME(ThisClass, bDetonated);
	DOREPLIFETIME(ThisClass, DetonationServerTime);
}
