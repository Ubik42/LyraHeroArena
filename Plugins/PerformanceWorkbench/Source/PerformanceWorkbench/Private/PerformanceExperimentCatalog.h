#pragma once

#include "CoreMinimal.h"
#include "PerformanceResultAggregator.h"

enum class EPerformanceExperimentSection : uint8
{
	HeroArena,
	ControlledLoad,
	Crowd
};

struct FPerformanceExperimentDefinition
{
	int32 TrackNumber = 0;
	EPerformanceExperimentSection Section = EPerformanceExperimentSection::HeroArena;
	FString LaunchDimension;
	FString PresetId;
	FString ResultDimension;
	FString Title;
	FString Detail;
};

namespace PerformanceExperimentCatalog
{
	TConstArrayView<FPerformanceExperimentDefinition> All();
	const FPerformanceExperimentDefinition* FindByLaunchDimension(const FString& LaunchDimension);
	const FPerformanceExperimentDefinition* FindByResult(const FString& PresetId, const FString& ResultDimension);
	bool IsRegisteredResult(const FString& PresetId, const FString& ResultDimension);
	TArray<FPerformanceComparisonQuery> BuildComparisonQueries();
}
