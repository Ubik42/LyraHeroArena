#pragma once

#include "CoreMinimal.h"
#include "PerformanceResultAggregator.h"

struct FPerformanceTimelineTrack
{
    FString Section;
    FString Label;
    FString Unit;
    FLinearColor Color = FLinearColor::White;
    float ScaleMax = 1.0f;
    TArray<float> Samples;
    TArray<float> ComparisonSamples;
};

struct FPerformanceHotspotView
{
	FString Section;
	FString Label;
	double BaselineMeanMs = 0.0;
	double BaselineP95Ms = 0.0;
	double ComparisonMeanMs = 0.0;
	double ComparisonP95Ms = 0.0;
	bool bHasComparison = false;
};

struct FPerformanceEffectSourceView
{
	FString Label;
	int32 BaselineCount = 0;
	int32 ComparisonCount = 0;
	bool bHasComparison = false;
	double BaselineMeanMs = 0.0;
	double BaselineP95Ms = 0.0;
	double ComparisonMeanMs = 0.0;
	double ComparisonP95Ms = 0.0;
	bool bHasDetailedTiming = false;
};

struct FPerformanceTimelineView
{
    FString RunId;
    FString ComparisonRunId;
    FString GroupId;
	FString PresetId;
	FString Dimension;
    FString Detail;
    FString Conditions;
    double DurationSeconds = 0.0;
	bool bHasComparison = false;
	TArray<FPerformanceHotspotView> Hotspots;
	bool bHasEffectBreakdown = false;
	int32 BaselineRegisteredEffects = 0;
	int32 BaselineActiveEffects = 0;
	int32 ComparisonRegisteredEffects = 0;
	int32 ComparisonActiveEffects = 0;
	TArray<FPerformanceEffectSourceView> EffectSystems;
	TArray<FPerformanceEffectSourceView> EffectOwners;
    TArray<FPerformanceTimelineTrack> Tracks;

    bool IsValid() const { return !RunId.IsEmpty() && !Tracks.IsEmpty(); }
};

namespace PerformanceWorkbenchTimeline
{
    void InvalidateIndex();
    TArray<FString> ListRecentGroups(const FString& RunsRoot, int32 MaxGroups = 12);
	TArray<FString> ListRecentGroups(const FString& RunsRoot,
		TConstArrayView<FPerformanceComparisonQuery> AllowedQueries, int32 MaxGroups = 12);
	FString FindLatestGroup(const FString& RunsRoot, const FString& PresetId, const FString& Dimension);
	FString FindLatestCompleteGroup(const FString& RunsRoot, const FString& PresetId, const FString& Dimension);
    TArray<FString> ListRoles(const FString& RunsRoot, const FString& GroupId);
    FPerformanceTimelineView LoadGroup(const FString& RunsRoot, const FString& GroupId, const FString& PreferredRole = FString());
    FPerformanceTimelineView LoadLatest(const FString& RunsRoot);
    TSharedRef<SWidget> BuildPanel(const FPerformanceTimelineView& View);
}
