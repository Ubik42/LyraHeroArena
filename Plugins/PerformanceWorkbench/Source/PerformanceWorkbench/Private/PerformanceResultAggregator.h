#pragma once

#include "CoreMinimal.h"

struct FPerformanceMetricComparisonView
{
    FString Label;
    FString Unit;
    double BaselineValue = 0.0;
    double CandidateValue = 0.0;
    double BaselineMinValue = 0.0;
    double BaselineMaxValue = 0.0;
    double CandidateMinValue = 0.0;
    double CandidateMaxValue = 0.0;
    double DeltaPercent = 0.0;
    bool bLowerIsBetter = true;
    bool bSampleRangesOverlap = false;
};

struct FPerformanceBatchComparisonView
{
    FString PresetId;
    FString Dimension;
    FString Title;
    FString SourceRevision;
    FString EngineVersion;
    FString ContractSummary;
    FString QualitySummary;
    FString StatusSummary;
    int32 BaselineRunCount = 0;
    int32 CandidateRunCount = 0;
    bool bComparable = false;
    bool bSingleBatch = false;
    bool bAllGuardsPassing = false;
    TArray<FString> Problems;
    TArray<FPerformanceMetricComparisonView> Metrics;
};

struct FPerformanceComparisonQuery
{
    FString PresetId;
    FString Dimension;
};

class FPerformanceResultAggregator
{
public:
    static void InvalidateRunCache();

    static FPerformanceBatchComparisonView LoadLatestComparison(
        const FString& RunsRoot,
        const FString& PresetId,
        const FString& Dimension = FString());

    static FPerformanceBatchComparisonView LoadComparisonForGroup(
        const FString& RunsRoot,
        const FString& PresetId,
        const FString& Dimension,
        const FString& GroupId);

    static TArray<FPerformanceBatchComparisonView> LoadLatestComparisons(
        const FString& RunsRoot,
        TConstArrayView<FPerformanceComparisonQuery> Queries);
};
