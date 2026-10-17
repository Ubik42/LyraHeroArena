#include "Components/HeroArenaControlPointRotationComponent.h"

#include "Components/PrimitiveComponent.h"
#include "Components/TimelineComponent.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "Messages/ControlPointStatusMessage.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "UObject/UnrealType.h"
#include "AbilitySystem/LyraAbilitySystemComponent.h"
#include "AbilitySystem/Phases/LyraGamePhaseAbility.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaControlPointRotationComponent)

DEFINE_LOG_CATEGORY_STATIC(LogHeroArenaControlPointRotation, Log, All);

UHeroArenaControlPointRotationComponent::UHeroArenaControlPointRotationComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = false;
}

void UHeroArenaControlPointRotationComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	CaptureListener = UGameplayMessageSubsystem::Get(this).RegisterListener(
		FGameplayTag::RequestGameplayTag(TEXT("ShooterGame.ControlPoint.Captured.Message")),
		this, &ThisClass::HandlePointCaptured);
	GetWorld()->GetTimerManager().SetTimer(DiscoverTimer, this, &ThisClass::DiscoverControlPoints, 0.5f, true, 1.0f);
}

void UHeroArenaControlPointRotationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(DiscoverTimer);
		GetWorld()->GetTimerManager().ClearTimer(RotationTimer);
	}
	if (CaptureListener.IsValid()) UGameplayMessageSubsystem::Get(this).UnregisterListener(CaptureListener);
	Super::EndPlay(EndPlayReason);
}

void UHeroArenaControlPointRotationComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, ActivePointIndex);
	DOREPLIFETIME(ThisClass, PendingPointIndex);
	DOREPLIFETIME(ThisClass, ActiveOwnerTeam);
	DOREPLIFETIME(ThisClass, bTransitioning);
	DOREPLIFETIME(ThisClass, ChangeAtServerTime);
	DOREPLIFETIME(ThisClass, CurrentPointLocation);
	DOREPLIFETIME(ThisClass, CapturingTeam);
	DOREPLIFETIME(ThisClass, CaptureProgress);
	DOREPLIFETIME(ThisClass, bActivePointContested);
	DOREPLIFETIME(ThisClass, bMatchEnded);
}

float UHeroArenaControlPointRotationComponent::GetServerTime() const
{
	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	return GameState ? GameState->GetServerWorldTimeSeconds() : 0.0f;
}

float UHeroArenaControlPointRotationComponent::GetSecondsUntilChange() const
{
	return ChangeAtServerTime > 0.0f ? FMath::Max(0.0f, ChangeAtServerTime - GetServerTime()) : 0.0f;
}

FText UHeroArenaControlPointRotationComponent::GetCurrentPointLabel() const
{
	const int32 Index = bTransitioning ? PendingPointIndex : ActivePointIndex;
	return Index >= 0 && Index < 26
		? FText::FromString(FString::Chr(static_cast<TCHAR>('A' + Index)))
		: FText::FromString(TEXT("—"));
}

void UHeroArenaControlPointRotationComponent::DiscoverControlPoints()
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	TArray<AActor*> Found;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->GetClass()->GetName().Contains(TEXT("B_ControlPointVolume"))) Found.Add(*It);
	}
	if (Found.Num() < 2) return;
	GetWorld()->GetTimerManager().ClearTimer(DiscoverTimer);
	Found.Sort([](const AActor& A, const AActor& B)
	{
		const FVector LA = A.GetActorLocation();
		const FVector LB = B.GetActorLocation();
		return !FMath::IsNearlyEqual(LA.X, LB.X) ? LA.X < LB.X : LA.Y < LB.Y;
	});
	ControlPoints.Reset();
	for (AActor* Point : Found)
	{
		ControlPoints.Add(Point);
		OriginalTransforms.Add(Point, Point->GetActorTransform());
		SetPointOwner(Point, -1);
		SetPointActive(Point, false);
	}
	PendingPointIndex = 0;
	// Inactive volumes are parked below the map; navigation/UI need the authored location.
	CurrentPointLocation = OriginalTransforms.FindChecked(ControlPoints[PendingPointIndex]).GetLocation();
	bTransitioning = true;
	ChangeAtServerTime = GetServerTime() + TransitionDuration;
	GetWorld()->GetTimerManager().SetTimer(RotationTimer, this, &ThisClass::TickRotation, 0.5f, true);
	UE_LOG(LogHeroArenaControlPointRotation, Log, TEXT("Control point rotation ready: points=%d first=A transition=%.1fs"),
		ControlPoints.Num(), TransitionDuration);
}

void UHeroArenaControlPointRotationComponent::SetPointOwner(AActor* Point, const int32 TeamId)
{
	if (!Point) return;
	if (FIntProperty* Property = FindFProperty<FIntProperty>(Point->GetClass(), TEXT("OwnerTeamID")))
	{
		Property->SetPropertyValue_InContainer(Point, TeamId);
		Point->ForceNetUpdate();
	}
}

void UHeroArenaControlPointRotationComponent::SetPointActive(AActor* Point, const bool bActive)
{
	if (!Point) return;
	if (const FTransform* Original = OriginalTransforms.Find(Point))
	{
		FTransform Target = *Original;
		if (!bActive) Target.AddToTranslation(FVector(0.0f, 0.0f, -100000.0f));
		Point->SetActorTransform(Target, false, nullptr, ETeleportType::TeleportPhysics);
	}
	Point->SetActorHiddenInGame(!bActive);
	Point->SetActorEnableCollision(bActive);
	TArray<UActorComponent*> Components;
	Point->GetComponents(Components);
	for (UActorComponent* Component : Components)
	{
		if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component))
		{
			Primitive->SetGenerateOverlapEvents(bActive);
		}
		if (UNiagaraComponent* Niagara = Cast<UNiagaraComponent>(Component))
		{
			if (bActive) Niagara->Activate(true); else Niagara->DeactivateImmediate();
		}
		if (!bActive)
		{
			if (UTimelineComponent* CaptureTimeline = Cast<UTimelineComponent>(Component))
			{
				CaptureTimeline->Stop();
				CaptureTimeline->SetNewTime(0.0f);
			}
		}
	}
	Point->ForceNetUpdate();
}

void UHeroArenaControlPointRotationComponent::RefreshActiveCaptureState()
{
	CapturingTeam = 0;
	CaptureProgress = 0.0f;
	bActivePointContested = false;
	if (bTransitioning || !ControlPoints.IsValidIndex(ActivePointIndex))
	{
		return;
	}

	const AActor* Point = ControlPoints[ActivePointIndex];
	if (!Point)
	{
		return;
	}

	auto ReadNumber = [Point](const FName PropertyName, double& OutValue)
	{
		const FNumericProperty* Property = FindFProperty<FNumericProperty>(Point->GetClass(), PropertyName);
		if (!Property)
		{
			return false;
		}
		const void* Value = Property->ContainerPtrToValuePtr<void>(Point);
		OutValue = Property->IsFloatingPoint()
			? Property->GetFloatingPointPropertyValue(Value)
			: static_cast<double>(Property->GetSignedIntPropertyValue(Value));
		return true;
	};

	TSet<int32> PresentTeams;
	if (const FArrayProperty* TeamsProperty = FindFProperty<FArrayProperty>(Point->GetClass(), TEXT("OverlappingTeams")))
	{
		if (const FNumericProperty* TeamProperty = CastField<FNumericProperty>(TeamsProperty->Inner))
		{
			FScriptArrayHelper Teams(TeamsProperty, TeamsProperty->ContainerPtrToValuePtr<void>(Point));
			for (int32 Index = 0; Index < Teams.Num(); ++Index)
			{
				const void* Value = Teams.GetRawPtr(Index);
				const int32 TeamId = TeamProperty->IsFloatingPoint()
					? FMath::RoundToInt(TeamProperty->GetFloatingPointPropertyValue(Value))
					: static_cast<int32>(TeamProperty->GetSignedIntPropertyValue(Value));
				if (TeamId > 0)
				{
					PresentTeams.Add(TeamId);
				}
			}
			bActivePointContested = PresentTeams.Num() > 1;
		}
	}
	double PointOwnerValue = -1.0;
	ReadNumber(TEXT("OwnerTeamID"), PointOwnerValue);
	const int32 PointOwnerTeam = FMath::RoundToInt(PointOwnerValue);
	const UTimelineComponent* CaptureTimeline = Point->FindComponentByClass<UTimelineComponent>();

	// Lyra's control-point Blueprint exposes CapturingTeamID but does not write it, and its
	// capture curve is a Timeline output rather than a Blueprint member named "Progress".
	// Derive the team from the authoritative overlap set and read the actual timeline position.
	if (!bActivePointContested && PresentTeams.Num() == 1)
	{
		const int32 PresentTeam = PresentTeams.Array()[0];
		if (PresentTeam != PointOwnerTeam)
		{
			if (CaptureTimeline && CaptureTimeline->IsPlaying())
			{
				CapturingTeam = PresentTeam;
				const float TimelineLength = CaptureTimeline->GetTimelineLength();
				CaptureProgress = TimelineLength > UE_SMALL_NUMBER
					? FMath::Clamp(CaptureTimeline->GetPlaybackPosition() / TimelineLength, 0.0f, 1.0f)
					: 0.0f;
			}
		}
	}

	UE_LOG(LogHeroArenaControlPointRotation, VeryVerbose,
		TEXT("Active point state: point=%s owner=%d teams=%d capturing_team=%d progress=%.2f contested=%s timeline=%s %.2f/%.2f"),
		*GetCurrentPointLabel().ToString(), PointOwnerTeam, PresentTeams.Num(), CapturingTeam, CaptureProgress,
		bActivePointContested ? TEXT("true") : TEXT("false"),
		CaptureTimeline && CaptureTimeline->IsPlaying() ? TEXT("playing") : TEXT("stopped"),
		CaptureTimeline ? CaptureTimeline->GetPlaybackPosition() : 0.0f,
		CaptureTimeline ? CaptureTimeline->GetTimelineLength() : 0.0f);
}

void UHeroArenaControlPointRotationComponent::ActivatePoint(const int32 PointIndex)
{
	if (!ControlPoints.IsValidIndex(PointIndex)) return;
	for (int32 Index = 0; Index < ControlPoints.Num(); ++Index)
	{
		SetPointOwner(ControlPoints[Index], -1);
		SetPointActive(ControlPoints[Index], Index == PointIndex);
	}
	ActivePointIndex = PointIndex;
	PendingPointIndex = INDEX_NONE;
	CurrentPointLocation = ControlPoints[PointIndex]->GetActorLocation();
	ActiveOwnerTeam = 0;
	bTransitioning = false;
	ChangeAtServerTime = 0.0f;
	RefreshActiveCaptureState();
	ClearInactiveScoringOwners();
	UE_LOG(LogHeroArenaControlPointRotation, Log, TEXT("Control point activated: point=%s index=%d"),
		*GetCurrentPointLabel().ToString(), PointIndex);
}

void UHeroArenaControlPointRotationComponent::StartTransition()
{
	if (ControlPoints.Num() == 0 || bTransitioning) return;
	for (AActor* Point : ControlPoints)
	{
		SetPointOwner(Point, -1);
		SetPointActive(Point, false);
	}
	PendingPointIndex = (ActivePointIndex + 1) % ControlPoints.Num();
	CurrentPointLocation = OriginalTransforms.FindChecked(ControlPoints[PendingPointIndex]).GetLocation();
	ActivePointIndex = INDEX_NONE;
	ActiveOwnerTeam = 0;
	CapturingTeam = 0;
	CaptureProgress = 0.0f;
	bActivePointContested = false;
	bTransitioning = true;
	ChangeAtServerTime = GetServerTime() + TransitionDuration;
	ClearInactiveScoringOwners();
	UE_LOG(LogHeroArenaControlPointRotation, Log, TEXT("Control point transition started: next=%s duration=%.1fs"),
		*GetCurrentPointLabel().ToString(), TransitionDuration);
}

void UHeroArenaControlPointRotationComponent::ClearInactiveScoringOwners()
{
	AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (!GameState) return;
	TArray<UActorComponent*> Components;
	GameState->GetComponents(Components);
	for (UActorComponent* Component : Components)
	{
		if (!Component || !Component->GetClass()->GetName().Contains(TEXT("ControlPointScoring"))) continue;
		FArrayProperty* PointsProperty = FindFProperty<FArrayProperty>(Component->GetClass(), TEXT("ControlPoints"));
		FArrayProperty* OwnersProperty = FindFProperty<FArrayProperty>(Component->GetClass(), TEXT("ControlPointOwnerTeams"));
		if (!PointsProperty || !OwnersProperty) continue;
		FScriptArrayHelper Points(PointsProperty, PointsProperty->ContainerPtrToValuePtr<void>(Component));
		FScriptArrayHelper Owners(OwnersProperty, OwnersProperty->ContainerPtrToValuePtr<void>(Component));
		FObjectPropertyBase* ObjectInner = CastField<FObjectPropertyBase>(PointsProperty->Inner);
		FIntProperty* IntInner = CastField<FIntProperty>(OwnersProperty->Inner);
		if (!ObjectInner || !IntInner) continue;
		for (int32 Index = 0; Index < FMath::Min(Points.Num(), Owners.Num()); ++Index)
		{
			AActor* Point = Cast<AActor>(ObjectInner->GetObjectPropertyValue(Points.GetRawPtr(Index)));
			const bool bIsActivePoint = !bTransitioning && ControlPoints.IsValidIndex(ActivePointIndex) && Point == ControlPoints[ActivePointIndex];
			if (!bIsActivePoint) IntInner->SetPropertyValue(Owners.GetRawPtr(Index), -1);
		}
	}
}

void UHeroArenaControlPointRotationComponent::TickRotation()
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	// Lyra stores active phases as abilities on the authoritative GameState ASC.
	// Its phase map is not replicated; publish only the presentation state needed here.
	const ULyraAbilitySystemComponent* ASC = GetOwner()->FindComponentByClass<ULyraAbilitySystemComponent>();
	const FGameplayTag PostGame = FGameplayTag::RequestGameplayTag(TEXT("ShooterGame.GamePhase.PostGame"));
	if (ASC)
	{
		for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
		{
			const ULyraGamePhaseAbility* Phase = Cast<ULyraGamePhaseAbility>(Spec.Ability);
			if (Spec.IsActive() && Phase && Phase->GetGamePhaseTag().MatchesTag(PostGame))
			{
				bMatchEnded = true;
				ChangeAtServerTime = 0.0f;
				GetWorld()->GetTimerManager().ClearTimer(RotationTimer);
				GetOwner()->ForceNetUpdate();
				UE_LOG(LogHeroArenaControlPointRotation, Log,
					TEXT("Objective rotation stopped for PostGame: point=%s world_time=%.3f"),
					*GetCurrentPointLabel().ToString(), GetWorld()->GetTimeSeconds());
				return;
			}
		}
	}
	ClearInactiveScoringOwners();
	RefreshActiveCaptureState();
	if (bTransitioning && GetServerTime() >= ChangeAtServerTime)
	{
		ActivatePoint(PendingPointIndex);
	}
	else if (!bTransitioning && ActiveOwnerTeam > 0 && GetServerTime() >= ChangeAtServerTime)
	{
		StartTransition();
	}
}

void UHeroArenaControlPointRotationComponent::HandlePointCaptured(
	FGameplayTag Channel, const FLyraControlPointStatusMessage& Message)
{
	if (bMatchEnded || bTransitioning || !ControlPoints.IsValidIndex(ActivePointIndex) || Message.ControlPoint != ControlPoints[ActivePointIndex])
	{
		return;
	}
	ActiveOwnerTeam = Message.OwnerTeamID;
	CapturingTeam = 0;
	CaptureProgress = 0.0f;
	bActivePointContested = false;
	ChangeAtServerTime = GetServerTime() + HoldDuration;
	UE_LOG(LogHeroArenaControlPointRotation, Log, TEXT("Active point captured: point=%s team=%d rotate_in=%.1fs"),
		*GetCurrentPointLabel().ToString(), ActiveOwnerTeam, HoldDuration);
}
