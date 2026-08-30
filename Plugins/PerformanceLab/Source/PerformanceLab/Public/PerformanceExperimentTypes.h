#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PerformanceExperimentTypes.generated.h"

UENUM(BlueprintType)
enum class EPerformanceExperimentDomain : uint8
{
    FrameCPU,
    Rendering,
    Network,
    Memory,
    AI,
    Navigation,
    Animation,
    Niagara,
    Physics
};

UENUM(BlueprintType)
enum class EPerformanceProcessTopology : uint8
{
    Standalone,
    ListenServer,
    DedicatedServer
};

UENUM(BlueprintType)
enum class EPerformanceGuardOperator : uint8
{
    Equal,
    GreaterOrEqual,
    LessOrEqual,
    FileNotEmpty
};

USTRUCT(BlueprintType)
struct PERFORMANCELAB_API FPerformanceExperimentTarget
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Experiment")
    FString MapPath;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Experiment")
    FString ExperienceId;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Experiment")
    EPerformanceProcessTopology Topology = EPerformanceProcessTopology::Standalone;
};

USTRUCT(BlueprintType)
struct PERFORMANCELAB_API FPerformanceCaptureContract
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Capture", meta=(ClampMin="0.0"))
    double WarmupSeconds = 10.0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Capture", meta=(ClampMin="1.0"))
    double DurationSeconds = 30.0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Capture", meta=(ClampMin="1.0"))
    double ReadyTimeoutSeconds = 60.0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Capture", meta=(ClampMin="1"))
    int32 ResolutionX = 1280;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Capture", meta=(ClampMin="1"))
    int32 ResolutionY = 720;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Capture")
    bool bWindowed = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Capture")
    bool bCaptureCsv = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Capture")
    bool bLockInitialPlayerView = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Capture")
    FString CsvCategories = TEXT("PerformanceLab,LyraPerformance");
};

USTRUCT(BlueprintType)
struct PERFORMANCELAB_API FPerformanceLoadContract
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Load", meta=(ClampMin="0"))
    int32 AgentCount = 0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Load")
    int32 Seed = 1337;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Load")
    FString MapOptions;
};

USTRUCT(BlueprintType)
struct PERFORMANCELAB_API FPerformanceQualityGuard
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Guard")
    FString Id;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Guard")
    FString MetricOrPath;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Guard")
    EPerformanceGuardOperator Operator = EPerformanceGuardOperator::GreaterOrEqual;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Guard")
    double ExpectedValue = 0.0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Guard")
    bool bRequired = true;
};

USTRUCT(BlueprintType)
struct PERFORMANCELAB_API FPerformanceGuardResult
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Guard")
    FString Id;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Guard")
    bool bPassed = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Guard")
    double ActualValue = 0.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Guard")
    FString Detail;
};

USTRUCT(BlueprintType)
struct PERFORMANCELAB_API FPerformanceArtifact
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Artifact")
    FString Kind;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Artifact")
    FString RelativePath;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Artifact")
    int64 SizeBytes = 0;
};

USTRUCT(BlueprintType)
struct PERFORMANCELAB_API FPerformanceExperimentPresetData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Experiment")
    FString PresetId = TEXT("unnamed");

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Experiment")
    FText DisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Experiment")
    TArray<EPerformanceExperimentDomain> Domains;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Experiment")
    FPerformanceExperimentTarget Target;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Experiment")
    FPerformanceLoadContract Load;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Experiment")
    FPerformanceCaptureContract Capture;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Experiment")
    TArray<FPerformanceQualityGuard> Guards;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Experiment")
    FString ExtraCommandLine;

    void Normalize();
    static FPerformanceExperimentPresetData MakeCrowd200();
    static FPerformanceExperimentPresetData MakeHeroArenaRendering();
    static FPerformanceExperimentPresetData MakeHeroArenaNetwork();
};

UCLASS(BlueprintType)
class PERFORMANCELAB_API UPerformanceExperimentPreset final : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Experiment")
    FPerformanceExperimentPresetData Definition;
};

USTRUCT(BlueprintType)
struct PERFORMANCELAB_API FPerformanceExperimentSession
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Session")
    FString RunId;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Session")
    FString SessionId;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Session")
    FString ProcessRole = TEXT("Standalone");

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Session")
    FString PresetId;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Session")
    FString Dimension;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Session")
    FString Variant;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Session")
    FString SourceRevision;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Session")
    FString StartedUtc;
};

USTRUCT(BlueprintType)
struct PERFORMANCELAB_API FPerformanceComparison
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Comparison")
    FString BaselineRunId;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Comparison")
    FString CandidateRunId;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Comparison")
    FString Metric;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Comparison")
    double BaselineValue = 0.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Comparison")
    double CandidateValue = 0.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Comparison")
    double DeltaPercent = 0.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Comparison")
    bool bComparable = false;

    static FPerformanceComparison Compare(
        const FString& InBaselineRunId,
        const FString& InCandidateRunId,
        const FString& InMetric,
        double InBaselineValue,
        double InCandidateValue);
};
