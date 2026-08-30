#include "Actors/HeroArenaBioticGrenade.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Character/LyraHealthComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Effects/HeroArenaGameplayEffect_BioticGrenade.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Teams/LyraTeamSubsystem.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaBioticGrenade)

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaBioticGrenade, Log, All);

AHeroArenaBioticGrenade::AHeroArenaBioticGrenade()
{
	bReplicates = true;
	SetReplicateMovement(true);
	SetNetUpdateFrequency(30.0f);
	InitialLifeSpan = 5.0f;
	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(16.0f);
	Collision->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	SetRootComponent(Collision);
	GrenadeMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GrenadeMesh"));
	GrenadeMesh->SetupAttachment(Collision);
	GrenadeMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Game/Weapons/Grenade/Mesh/SM_grenade.SM_grenade"));
	if (Mesh.Succeeded()) GrenadeMesh->SetStaticMesh(Mesh.Object);
	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->UpdatedComponent = Collision;
	ProjectileMovement->InitialSpeed = 1800.0f;
	ProjectileMovement->MaxSpeed = 1800.0f;
	ProjectileMovement->ProjectileGravityScale = 1.0f;
	ProjectileMovement->bShouldBounce = true;
	ProjectileMovement->Bounciness = 0.35f;
	TrailFx = CreateDefaultSubobject<UNiagaraComponent>(TEXT("TrailFx"));
	TrailFx->SetupAttachment(Collision);
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> Trail(TEXT("/Game/Effects/Particles/Explosion/NS_Grenade_Trail.NS_Grenade_Trail"));
	if (Trail.Succeeded()) TrailFx->SetAsset(Trail.Object);
}

void AHeroArenaBioticGrenade::BeginPlay()
{
	Super::BeginPlay();
	Collision->IgnoreActorWhenMoving(GetInstigator(), true);
	ProjectileMovement->OnProjectileBounce.AddDynamic(this, &ThisClass::OnProjectileBounce);
	if (HasAuthority()) GetWorldTimerManager().SetTimer(FuseTimer, this, &ThisClass::Detonate, 2.0f, false);
}

void AHeroArenaBioticGrenade::InitializeVelocity(const FVector& Velocity) { ProjectileMovement->Velocity = Velocity; }

void AHeroArenaBioticGrenade::OnProjectileBounce(const FHitResult&, const FVector&)
{
	if (HasAuthority()) Detonate();
}

void AHeroArenaBioticGrenade::Detonate()
{
	if (!HasAuthority() || bDetonated) return;
	bDetonated = true;
	ForceNetUpdate();
	AActor* Source = GetInstigator();
	UAbilitySystemComponent* SourceASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Source);
	ULyraTeamSubsystem* Teams = GetWorld()->GetSubsystem<ULyraTeamSubsystem>();
	int32 Amplified = 0, Suppressed = 0;
	if (SourceASC && Teams)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(HeroArenaBioticGrenade), false, this);
		TArray<FOverlapResult> Results;
		GetWorld()->OverlapMultiByObjectType(Results, GetActorLocation(), FQuat::Identity,
			FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeSphere(Radius), Params);
		TSet<TObjectPtr<AActor>> Seen;
		for (const FOverlapResult& Result : Results)
		{
			AActor* Target = Result.GetActor();
			if (!IsValid(Target) || Seen.Contains(Target)) continue;
			Seen.Add(Target);
			ULyraHealthComponent* Health = ULyraHealthComponent::FindHealthComponent(Target);
			UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target);
			if (!Health || Health->IsDeadOrDying() || !TargetASC) continue;
			const ELyraTeamComparison Relation = Teams->CompareTeams(Source, Target);
			TSubclassOf<UGameplayEffect> Effect;
			if (Relation == ELyraTeamComparison::OnSameTeam) { Effect = UHeroArenaGameplayEffect_BioticGrenadeAmplify::StaticClass(); ++Amplified; }
			else if (Relation == ELyraTeamComparison::DifferentTeams) { Effect = UHeroArenaGameplayEffect_BioticGrenadeSuppress::StaticClass(); ++Suppressed; }
			if (Effect)
			{
				FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();
				Context.AddSourceObject(this);
				FGameplayEffectSpecHandle Spec = SourceASC->MakeOutgoingSpec(Effect, 1.0f, Context);
				if (Spec.IsValid())
				{
					TargetASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
					const FGameplayTag StatusTag = FGameplayTag::RequestGameplayTag(Relation == ELyraTeamComparison::OnSameTeam
						? TEXT("Status.HeroArena.HealingAmplified") : TEXT("Status.HeroArena.HealingSuppressed"));
					UE_LOG(LogHeroArenaBioticGrenade, Log, TEXT("Biotic status applied: target=%s status=%s tag_count=%d"),
						*GetNameSafe(Target), *StatusTag.ToString(), TargetASC->GetTagCount(StatusTag));
				}
			}
		}
	}
	UE_LOG(LogHeroArenaBioticGrenade, Log, TEXT("Biotic grenade detonated: source=%s location=%s amplified=%d suppressed=%d radius=%.0f"),
		*GetNameSafe(Source), *GetActorLocation().ToCompactString(), Amplified, Suppressed, Radius);
	PlayDetonationFeedback();
	Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ProjectileMovement->StopMovementImmediately();
	GrenadeMesh->SetVisibility(false);
	SetLifeSpan(0.75f);
}

void AHeroArenaBioticGrenade::OnRep_Detonated()
{
	if (bDetonated)
	{
		PlayDetonationFeedback();
		Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		GrenadeMesh->SetVisibility(false);
	}
}

void AHeroArenaBioticGrenade::PlayDetonationFeedback()
{
	if (TrailFx) TrailFx->Deactivate();
	static TSoftObjectPtr<UNiagaraSystem> Explosion(FSoftObjectPath(TEXT("/Game/Effects/Particles/Explosion/NS_Grenade_Explosion.NS_Grenade_Explosion")));
	if (UNiagaraSystem* System = Explosion.LoadSynchronous()) UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, System, GetActorLocation(), FRotator::ZeroRotator, FVector(0.75f));
}

void AHeroArenaBioticGrenade::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, bDetonated);
}
