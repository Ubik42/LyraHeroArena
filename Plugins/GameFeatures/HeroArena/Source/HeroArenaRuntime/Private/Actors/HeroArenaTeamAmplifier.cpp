#include "Actors/HeroArenaTeamAmplifier.h"

#include "AbilitySystem/Attributes/LyraHealthSet.h"
#include "AbilitySystem/LyraAbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Effects/HeroArenaGameplayEffect_TeamAmplifier.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "Hero/HeroArenaHeroDefinition.h"
#include "Hero/HeroArenaHeroSelectionComponent.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Physics/LyraCollisionChannels.h"
#include "Teams/LyraTeamSubsystem.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaTeamAmplifier)

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaTeamAmplifier, Log, All);

AHeroArenaTeamAmplifier::AHeroArenaTeamAmplifier(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bReplicates = true;
	SetReplicateMovement(true);
	bAlwaysRelevant = false;
	SetNetCullDistanceSquared(FMath::Square(14000.0f));
	SetNetUpdateFrequency(15.0f);
	InitialLifeSpan = Duration;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	AmplifierMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("AmplifierMesh"));
	AmplifierMesh->SetupAttachment(SceneRoot);
	AmplifierMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	AmplifierMesh->SetCollisionObjectType(ECC_WorldDynamic);
	AmplifierMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	AmplifierMesh->SetCollisionResponseToChannel(Lyra_TraceChannel_Weapon, ECR_Block);
	AmplifierMesh->SetCollisionResponseToChannel(Lyra_TraceChannel_Weapon_Capsule, ECR_Block);
	AmplifierMesh->SetCollisionResponseToChannel(Lyra_TraceChannel_Weapon_Multi, ECR_Block);
	AmplifierMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	AmplifierMesh->SetCastShadow(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(
		TEXT("/Game/Weapons/Spawnpad/Mesh/SM_launchpad_Hex.SM_launchpad_Hex"));
	if (Mesh.Succeeded())
	{
		AmplifierMesh->SetStaticMesh(Mesh.Object);
		AmplifierMesh->SetRelativeScale3D(FVector(0.9f));
	}

	AuraVolume = CreateDefaultSubobject<USphereComponent>(TEXT("AuraVolume"));
	AuraVolume->SetupAttachment(SceneRoot);
	AuraVolume->SetSphereRadius(AuraRadius);
	AuraVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	AuraVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	AuraVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

	AuraFx = CreateDefaultSubobject<UNiagaraComponent>(TEXT("AuraFx"));
	AuraFx->SetupAttachment(SceneRoot);
	AuraFx->SetAutoActivate(true);
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> Fx(
		TEXT("/Game/Effects/Particles/Item/NS_Heal.NS_Heal"));
	if (Fx.Succeeded()) AuraFx->SetAsset(Fx.Object);

	StatusText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("StatusText"));
	StatusText->SetupAttachment(SceneRoot);
	StatusText->SetHorizontalAlignment(EHTA_Center);
	StatusText->SetVerticalAlignment(EVRTA_TextCenter);
	StatusText->SetWorldSize(16.0f);
	StatusText->SetRelativeLocation(FVector(0.0f, 0.0f, 85.0f));
	StatusText->SetCastShadow(false);
	StatusText->SetText(FText::FromString(TEXT("TEAM AMP  +15%")));

	AbilitySystemComponent = ObjectInitializer.CreateDefaultSubobject<ULyraAbilitySystemComponent>(
		this, TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Full);
	HealthSet = CreateDefaultSubobject<ULyraHealthSet>(TEXT("HealthSet"));
}

void AHeroArenaTeamAmplifier::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	AbilitySystemComponent->InitAbilityActorInfo(this, this);
	HealthSet->OnHealthChanged.AddUObject(this, &ThisClass::HandleHealthChanged);
	HealthSet->OnOutOfHealth.AddUObject(this, &ThisClass::HandleOutOfHealth);
}

void AHeroArenaTeamAmplifier::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		AbilitySystemComponent->SetNumericAttributeBase(ULyraHealthSet::GetMaxHealthAttribute(), MaxHealth);
		AbilitySystemComponent->SetNumericAttributeBase(ULyraHealthSet::GetHealthAttribute(), MaxHealth);
		ServerEndTime = GetWorld()->GetTimeSeconds() + Duration;
		SetLifeSpan(Duration);
		GetWorldTimerManager().SetTimer(AuraTimer, this, &ThisClass::RefreshAura,
			RefreshInterval, true, 0.1f);
		if (UHeroArenaHeroSelectionComponent* Selection =
			GetOwner() ? GetOwner()->FindComponentByClass<UHeroArenaHeroSelectionComponent>() : nullptr)
		{
			Selection->OnHeroChanged.AddUniqueDynamic(this, &ThisClass::HandleOwnerHeroChanged);
		}
		ForceNetUpdate();
	}
	RefreshPresentation();
	UE_LOG(LogHeroArenaTeamAmplifier, Log,
		TEXT("Relay team amplifier ready: actor=%s team=%d health=%.0f radius=%.0f duration=%.1f"),
		*GetNameSafe(this), GenericTeamIdToInteger(MyTeamID), MaxHealth, AuraRadius, Duration);
}

void AHeroArenaTeamAmplifier::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(AuraTimer);
	if (HasAuthority())
	{
		if (UHeroArenaHeroSelectionComponent* Selection =
			GetOwner() ? GetOwner()->FindComponentByClass<UHeroArenaHeroSelectionComponent>() : nullptr)
		{
			Selection->OnHeroChanged.RemoveDynamic(this, &ThisClass::HandleOwnerHeroChanged);
		}
	}
	Super::EndPlay(EndPlayReason);
}

UAbilitySystemComponent* AHeroArenaTeamAmplifier::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void AHeroArenaTeamAmplifier::SetGenericTeamId(const FGenericTeamId& NewTeamID)
{
	if (!HasAuthority()) return;
	const FGenericTeamId OldTeamID = MyTeamID;
	MyTeamID = NewTeamID;
	ConditionalBroadcastTeamChanged(this, OldTeamID, MyTeamID);
	RefreshPresentation();
	ForceNetUpdate();
}

void AHeroArenaTeamAmplifier::RefreshAura()
{
	if (!HasAuthority()) return;
	ULyraTeamSubsystem* Teams = GetWorld()->GetSubsystem<ULyraTeamSubsystem>();
	UAbilitySystemComponent* SourceASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner());
	if (!Teams || !SourceASC) return;

	TArray<AActor*> Targets;
	AuraVolume->GetOverlappingActors(Targets, APawn::StaticClass());
	int32 Applied = 0;
	for (AActor* Target : Targets)
	{
		UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target);
		if (!TargetASC || Teams->CompareTeams(this, Target) != ELyraTeamComparison::OnSameTeam) continue;
		FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();
		Context.AddSourceObject(this);
		TargetASC->ApplyGameplayEffectToSelf(GetDefault<UHeroArenaGameplayEffect_TeamAmplified>(), 1.0f, Context);
		++Applied;
	}
	if (Applied > 0)
	{
		if (!bLoggedFirstActiveAura)
		{
			UE_LOG(LogHeroArenaTeamAmplifier, Log,
				TEXT("Relay team amplifier activated aura: actor=%s allies=%d"), *GetNameSafe(this), Applied);
		}
		bLoggedFirstActiveAura = true;
	}
}

void AHeroArenaTeamAmplifier::HandleOutOfHealth(AActor* DamageInstigator, AActor*,
	const FGameplayEffectSpec*, float, float, float)
{
	if (HasAuthority())
	{
		UE_LOG(LogHeroArenaTeamAmplifier, Log,
			TEXT("Relay team amplifier destroyed: actor=%s instigator=%s"),
			*GetNameSafe(this), *GetNameSafe(DamageInstigator));
		Destroy();
	}
}

void AHeroArenaTeamAmplifier::HandleHealthChanged(AActor* DamageInstigator, AActor*,
	const FGameplayEffectSpec*, float, const float OldValue, const float NewValue)
{
	RefreshPresentation();
	UE_LOG(LogHeroArenaTeamAmplifier, Log,
		TEXT("Relay team amplifier health: actor=%s %.0f->%.0f instigator=%s authority=%s"),
		*GetNameSafe(this), OldValue, NewValue, *GetNameSafe(DamageInstigator),
		HasAuthority() ? TEXT("true") : TEXT("false"));
}

void AHeroArenaTeamAmplifier::HandleOwnerHeroChanged(
	const UHeroArenaHeroDefinition*, const UHeroArenaHeroDefinition* NewHero)
{
	if (HasAuthority() && (!NewHero || NewHero->HeroId != TEXT("Relay"))) Destroy();
}

void AHeroArenaTeamAmplifier::RefreshPresentation()
{
	if (!StatusText) return;
	const bool bBlue = GenericTeamIdToInteger(MyTeamID) == 1;
	StatusText->SetTextRenderColor((bBlue ? FLinearColor(0.15f, 0.85f, 1.0f) :
		FLinearColor(1.0f, 0.25f, 0.10f)).ToFColor(true));
	StatusText->SetText(FText::FromString(FString::Printf(
		TEXT("TEAM AMP  +15%%  %.0f"), HealthSet ? HealthSet->GetHealth() : 0.0f)));
}

void AHeroArenaTeamAmplifier::OnRep_Team(const FGenericTeamId OldTeamID)
{
	ConditionalBroadcastTeamChanged(this, OldTeamID, MyTeamID);
	RefreshPresentation();
}

void AHeroArenaTeamAmplifier::OnRep_EndTime()
{
	UE_LOG(LogHeroArenaTeamAmplifier, Verbose,
		TEXT("Relay team amplifier lifetime replicated: actor=%s end=%.2f"), *GetNameSafe(this), ServerEndTime);
}

void AHeroArenaTeamAmplifier::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, MyTeamID);
	DOREPLIFETIME(ThisClass, ServerEndTime);
}
