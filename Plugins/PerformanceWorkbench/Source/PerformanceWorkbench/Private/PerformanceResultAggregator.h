#pragma once

#include "CoreMinimal.h"

struct FPerformanceMetricComparisonView
{
    FString Label;
    FString Unit;
    double BaselineValue = 0.0;
    double CandidateValue = 0.0;
    double DeltaPercent = 0.0;
    bool bLowerIsBetter = true;
};

struct FPerformanceBatchComparisonView
{
    FString PresetId;
    FString Dimension;
    FString Title;
    FString SourceRevision;
    FString ContractSummary;
    FString QualitySummary;
    FString StatusSummary;
    int32 BaselineRunCount = 0;
    int32 CandidateRunCount = 0;
    bool bComparable = false;
    bool bAllGuardsPassing = false;
    TArray<FString> Problems;
    TArray<FPerformanceMetricComparisonView> Metrics;
};

class FPerformanceResultAggregator
{
public:
    static FPerformanceBatchComparisonView LoadLatestComparison(
        const FString& RunsRoot,
        const FString& PresetId);
};
