#include "Actors/HeroArenaRelayJumpPad.h"

#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Character.h"
#include "Hero/HeroArenaHeroDefinition.h"
#include "Hero/HeroArenaHeroSelectionComponent.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Teams/LyraTeamSubsystem.h"
#include "UObject/ConstructorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaRelayJumpPad)

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaRelayJumpPad, Log, All);

AHeroArenaRelayJumpPad::AHeroArenaRelayJumpPad(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bReplicates = true;
	bAlwaysRelevant = false;
	SetReplicateMovement(true);
	SetNetCullDistanceSquared(FMath::Square(12000.0f));
	SetNetUpdateFrequency(10.0f);
	InitialLifeSpan = Duration;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	PadMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PadMesh"));
	PadMesh->SetupAttachment(SceneRoot);
	PadMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	PadMesh->SetCollisionResponseToAllChannels(ECR_Block);
	PadMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	PadMesh->SetCastShadow(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(
		TEXT("/Game/Weapons/Spawnpad/Mesh/SM_launchpad_Square.SM_launchpad_Square"));
	if (Mesh.Succeeded())
	{
		PadMesh->SetStaticMesh(Mesh.Object);
		PadMesh->SetRelativeScale3D(FVector(0.72f));
	}

	LaunchVolume = CreateDefaultSubobject<USphereComponent>(TEXT("LaunchVolume"));
	LaunchVolume->SetupAttachment(SceneRoot);
	LaunchVolume->SetSphereRadius(115.0f);
	LaunchVolume->SetRelativeLocation(FVector(0.0f, 0.0f, 65.0f));
	LaunchVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	LaunchVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	LaunchVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	LaunchVolume->SetGenerateOverlapEvents(true);
	LaunchVolume->OnComponentBeginOverlap.AddDynamic(this, &ThisClass::HandleOverlap);

	LaunchFx = CreateDefaultSubobject<UNiagaraComponent>(TEXT("LaunchFx"));
	LaunchFx->SetupAttachment(SceneRoot);
	LaunchFx->SetAutoActivate(true);
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> Fx(
		TEXT("/Game/Effects/Particles/Environmental/NS_JumpPad.NS_JumpPad"));
	if (Fx.Succeeded())
	{
		LaunchFx->SetAsset(Fx.Object);
	}

	StatusText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("StatusText"));
	StatusText->SetupAttachment(SceneRoot);
	StatusText->SetHorizontalAlignment(EHTA_Center);
	StatusText->SetVerticalAlignment(EVRTA_TextCenter);
	StatusText->SetWorldSize(13.0f);
	StatusText->SetRelativeLocation(FVector(0.0f, 0.0f, 32.0f));
	StatusText->SetCastShadow(false);
	StatusText->SetText(FText::FromString(TEXT("RELAY LIFT")));
}

void AHeroArenaRelayJumpPad::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		ServerEndTime = GetWorld()->GetTimeSeconds() + Duration;
		SetLifeSpan(Duration);
		if (UHeroArenaHeroSelectionComponent* Selection =
			GetOwner() ? GetOwner()->FindComponentByClass<UHeroArenaHeroSelectionComponent>() : nullptr)
		{
			Selection->OnHeroChanged.AddUniqueDynamic(this, &ThisClass::HandleOwnerHeroChanged);
		}
		ForceNetUpdate();
	}
	RefreshPresentation();
	UE_LOG(LogHeroArenaRelayJumpPad, Log,
		TEXT("Relay jump pad ready: pad=%s authority=%s team=%d duration=%.1f"),
		*GetNameSafe(this), HasAuthority() ? TEXT("true") : TEXT("false"),
		GenericTeamIdToInteger(MyTeamID), Duration);
}

void AHeroArenaRelayJumpPad::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
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

void AHeroArenaRelayJumpPad::SetGenericTeamId(const FGenericTeamId& NewTeamID)
{
	if (!HasAuthority())
	{
		return;
	}
	const FGenericTeamId OldTeamID = MyTeamID;
	MyTeamID = NewTeamID;
	ConditionalBroadcastTeamChanged(this, OldTeamID, MyTeamID);
	RefreshPresentation();
	ForceNetUpdate();
}

bool AHeroArenaRelayJumpPad::RecallBy(AActor* Requester)
{
	if (!HasAuthority() || Requester != GetOwner())
	{
		return false;
	}
	Destroy();
	return true;
}

void AHeroArenaRelayJumpPad::HandleOverlap(UPrimitiveComponent*, AActor* OtherActor,
	UPrimitiveComponent*, int32, bool, const FHitResult&)
{
	ACharacter* Character = Cast<ACharacter>(OtherActor);
	ULyraTeamSubsystem* Teams = GetWorld()->GetSubsystem<ULyraTeamSubsystem>();
	if (!HasAuthority() || !IsValid(Character) || !Teams)
	{
		return;
	}

	const ELyraTeamComparison Relationship = Teams->CompareTeams(this, Character);
	if (Relationship != ELyraTeamComparison::OnSameTeam
		&& Relationship != ELyraTeamComparison::DifferentTeams)
	{
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	for (auto It = NextLaunchTimeByCharacter.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || It.Value() <= Now)
		{
			It.RemoveCurrent();
		}
	}
	if (const float* NextLaunchTime = NextLaunchTimeByCharacter.Find(Character))
	{
		UE_LOG(LogHeroArenaRelayJumpPad, Verbose,
			TEXT("Relay jump pad ignored immediate retrigger: pad=%s target=%s remaining=%.2f"),
			*GetNameSafe(this), *GetNameSafe(Character), *NextLaunchTime - Now);
		return;
	}
	NextLaunchTimeByCharacter.Add(Character, Now + PerCharacterRelaunchDelay);

	if (Relationship == ELyraTeamComparison::OnSameTeam)
	{
		const FVector LaunchVelocity = GetActorForwardVector() * AllyForwardSpeed
			+ FVector::UpVector * AllyUpSpeed;
		Character->LaunchCharacter(LaunchVelocity, true, true);
		UE_LOG(LogHeroArenaRelayJumpPad, Log,
			TEXT("Relay jump pad launched ally: pad=%s target=%s velocity=%s"),
			*GetNameSafe(this), *GetNameSafe(Character), *LaunchVelocity.ToCompactString());
	}
	else if (Relationship == ELyraTeamComparison::DifferentTeams)
	{
		FVector PushDirection = Character->GetActorLocation() - GetActorLocation();
		PushDirection.Z = 0.0f;
		PushDirection = PushDirection.GetSafeNormal(UE_SMALL_NUMBER, GetActorForwardVector());
		const FVector LaunchVelocity = PushDirection * EnemyPushSpeed + FVector::UpVector * EnemyUpSpeed;
		Character->LaunchCharacter(LaunchVelocity, true, true);
		UE_LOG(LogHeroArenaRelayJumpPad, Log,
			TEXT("Relay jump pad rejected enemy: pad=%s target=%s velocity=%s"),
			*GetNameSafe(this), *GetNameSafe(Character), *LaunchVelocity.ToCompactString());
	}
}

void AHeroArenaRelayJumpPad::HandleOwnerHeroChanged(
	const UHeroArenaHeroDefinition*, const UHeroArenaHeroDefinition* NewHero)
{
	if (HasAuthority() && (!NewHero || NewHero->HeroId != TEXT("Relay")))
	{
		Destroy();
	}
}

void AHeroArenaRelayJumpPad::RefreshPresentation()
{
	if (!StatusText)
	{
		return;
	}
	const bool bBlue = GenericTeamIdToInteger(MyTeamID) == 1;
	StatusText->SetTextRenderColor((bBlue ? FLinearColor(0.10f, 0.68f, 1.0f) :
		FLinearColor(1.0f, 0.32f, 0.08f)).ToFColor(true));
}

void AHeroArenaRelayJumpPad::OnRep_Team(const FGenericTeamId OldTeamID)
{
	ConditionalBroadcastTeamChanged(this, OldTeamID, MyTeamID);
	RefreshPresentation();
}

void AHeroArenaRelayJumpPad::OnRep_EndTime()
{
	UE_LOG(LogHeroArenaRelayJumpPad, Verbose,
		TEXT("Relay jump pad lifetime replicated: pad=%s end=%.2f"), *GetNameSafe(this), ServerEndTime);
}

void AHeroArenaRelayJumpPad::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, MyTeamID);
	DOREPLIFETIME(ThisClass, ServerEndTime);
}
