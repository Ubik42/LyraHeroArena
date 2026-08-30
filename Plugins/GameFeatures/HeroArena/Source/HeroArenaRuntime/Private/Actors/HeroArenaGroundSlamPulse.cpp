#include "Actors/HeroArenaGroundSlamPulse.h"

#include "Components/SceneComponent.h"
#include "Components/TextRenderComponent.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "UObject/ConstructorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaGroundSlamPulse)

AHeroArenaGroundSlamPulse::AHeroArenaGroundSlamPulse()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(true);
	SetCanBeDamaged(false);
	InitialLifeSpan = 1.25f;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	PulseFx = CreateDefaultSubobject<UNiagaraComponent>(TEXT("PulseFx"));
	PulseFx->SetupAttachment(SceneRoot);
	PulseFx->SetAutoActivate(true);
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> ExplosionFx(
		TEXT("/Game/Effects/Particles/Explosion/NS_Grenade_Explosion.NS_Grenade_Explosion"));
	if (ExplosionFx.Succeeded())
	{
		PulseFx->SetAsset(ExplosionFx.Object);
	}

	StatusText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("StatusText"));
	StatusText->SetupAttachment(SceneRoot);
	StatusText->SetHorizontalAlignment(EHTA_Center);
	StatusText->SetVerticalAlignment(EVRTA_TextCenter);
	StatusText->SetRelativeLocation(FVector(180.0f, 0.0f, 12.0f));
	StatusText->SetWorldSize(16.0f);
	StatusText->SetCastShadow(false);
	StatusText->SetText(FText::FromString(TEXT("GROUND SLAM")));
}

void AHeroArenaGroundSlamPulse::InitializePulse(const int32 InTeamId, const float InRadius)
{
	check(HasAuthority());
	TeamId = InTeamId;
	PulseRadius = InRadius;
	RefreshPresentation();
	ForceNetUpdate();
}

void AHeroArenaGroundSlamPulse::BeginPlay()
{
	Super::BeginPlay();
	RefreshPresentation();
}

void AHeroArenaGroundSlamPulse::OnRep_State()
{
	RefreshPresentation();
}

void AHeroArenaGroundSlamPulse::RefreshPresentation()
{
	const FLinearColor TeamColor = TeamId == 1
		? FLinearColor(0.05f, 0.45f, 1.0f)
		: FLinearColor(1.0f, 0.12f, 0.08f);
	StatusText->SetTextRenderColor(TeamColor.ToFColor(true));
	const float Scale = FMath::Clamp(PulseRadius / 650.0f, 0.5f, 2.0f);
	PulseFx->SetWorldScale3D(FVector(Scale));
}

void AHeroArenaGroundSlamPulse::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, TeamId);
	DOREPLIFETIME(ThisClass, PulseRadius);
}
