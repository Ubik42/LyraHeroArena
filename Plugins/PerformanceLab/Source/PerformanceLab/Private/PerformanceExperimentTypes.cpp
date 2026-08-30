#include "PerformanceExperimentTypes.h"

void FPerformanceExperimentPresetData::Normalize()
{
    PresetId = PresetId.IsEmpty() ? TEXT("unnamed") : PresetId;
    Load.AgentCount = FMath::Max(0, Load.AgentCount);
    Capture.WarmupSeconds = FMath::Max(0.0, Capture.WarmupSeconds);
    Capture.DurationSeconds = FMath::Max(1.0, Capture.DurationSeconds);
    Capture.ReadyTimeoutSeconds = FMath::Max(1.0, Capture.ReadyTimeoutSeconds);
    Capture.ResolutionX = FMath::Max(1, Capture.ResolutionX);
    Capture.ResolutionY = FMath::Max(1, Capture.ResolutionY);
}

FPerformanceExperimentPresetData FPerformanceExperimentPresetData::MakeCrowd200()
{
    FPerformanceExperimentPresetData Preset;
    Preset.PresetId = TEXT("crowd-200");
    Preset.DisplayName = NSLOCTEXT("PerformanceLab", "Crowd200Preset", "200 NPC 性能实验");
    Preset.Domains = {
        EPerformanceExperimentDomain::FrameCPU,
        EPerformanceExperimentDomain::AI,
        EPerformanceExperimentDomain::Navigation,
        EPerformanceExperimentDomain::Animation,
        EPerformanceExperimentDomain::Rendering
    };
    Preset.Target.MapPath = TEXT("/ShooterCore/Maps/L_ShooterPerf");
    Preset.Target.ExperienceId = TEXT("B_ShooterGame_Perf");
    Preset.Load.AgentCount = 200;
    Preset.Load.Seed = 1337;

    FPerformanceQualityGuard Controllers;
    Controllers.Id = TEXT("ai-controller-count");
    Controllers.MetricOrPath = TEXT("aiControllerCount");
    Controllers.ExpectedValue = 200.0;
    Preset.Guards.Add(Controllers);

    FPerformanceQualityGuard PossessedPawns;
    PossessedPawns.Id = TEXT("possessed-ai-pawn-count");
    PossessedPawns.MetricOrPath = TEXT("possessedAIPawnCount");
    PossessedPawns.ExpectedValue = 200.0;
    Preset.Guards.Add(PossessedPawns);

    FPerformanceQualityGuard Samples;
    Samples.Id = TEXT("captured-frame-samples");
    Samples.MetricOrPath = TEXT("sampleCount");
    Samples.ExpectedValue = 1.0;
    Preset.Guards.Add(Samples);

    FPerformanceQualityGuard Csv;
    Csv.Id = TEXT("csv-artifact");
    Csv.MetricOrPath = TEXT("capture.csv");
    Csv.Operator = EPerformanceGuardOperator::FileNotEmpty;
    Preset.Guards.Add(Csv);
    return Preset;
}

FPerformanceComparison FPerformanceComparison::Compare(
    const FString& InBaselineRunId,
    const FString& InCandidateRunId,
    const FString& InMetric,
    double InBaselineValue,
    double InCandidateValue)
{
    FPerformanceComparison Result;
    Result.BaselineRunId = InBaselineRunId;
    Result.CandidateRunId = InCandidateRunId;
    Result.Metric = InMetric;
    Result.BaselineValue = InBaselineValue;
    Result.CandidateValue = InCandidateValue;
    Result.bComparable = !InBaselineRunId.IsEmpty() && !InCandidateRunId.IsEmpty() && !FMath::IsNearlyZero(InBaselineValue);
    if (Result.bComparable)
    {
        Result.DeltaPercent = ((InCandidateValue - InBaselineValue) / InBaselineValue) * 100.0;
    }
    return Result;
}

FPerformanceExperimentPresetData FPerformanceExperimentPresetData::MakeHeroArenaRendering()
{
    FPerformanceExperimentPresetData Preset;
    Preset.PresetId = TEXT("hero-arena-character-rendering");
    Preset.DisplayName = NSLOCTEXT("PerformanceLab", "HeroArenaRenderingPreset", "HeroArena 角色渲染");
    Preset.Domains = { EPerformanceExperimentDomain::FrameCPU, EPerformanceExperimentDomain::Rendering };
    Preset.Target.MapPath = TEXT("/ShooterMaps/Maps/L_Convolution_Blockout");
    Preset.Target.ExperienceId = TEXT("B_HeroArena_ControlPoints");
    Preset.Load.AgentCount = 15;
    Preset.Load.Seed = 1337;
    Preset.Capture.WarmupSeconds = 15.0;
    Preset.Capture.DurationSeconds = 30.0;
    Preset.Capture.ReadyTimeoutSeconds = 90.0;
    Preset.Capture.bLockInitialPlayerView = true;

    FPerformanceQualityGuard Agents;
    Agents.Id = TEXT("possessed-ai-pawn-count");
    Agents.MetricOrPath = TEXT("possessedAIPawnCount");
    Agents.ExpectedValue = Preset.Load.AgentCount;
    Preset.Guards.Add(Agents);

    FPerformanceQualityGuard NormalRHI;
    NormalRHI.Id = TEXT("normal-rhi");
    NormalRHI.MetricOrPath = TEXT("normalRHI");
    NormalRHI.Operator = EPerformanceGuardOperator::Equal;
    NormalRHI.ExpectedValue = 1.0;
    Preset.Guards.Add(NormalRHI);

    FPerformanceQualityGuard FixedView;
    FixedView.Id = TEXT("fixed-player-view");
    FixedView.MetricOrPath = TEXT("initialViewLocked");
    FixedView.Operator = EPerformanceGuardOperator::Equal;
    FixedView.ExpectedValue = 1.0;
    Preset.Guards.Add(FixedView);

    FPerformanceQualityGuard Samples;
    Samples.Id = TEXT("captured-frame-samples");
    Samples.MetricOrPath = TEXT("sampleCount");
    Samples.ExpectedValue = 1.0;
    Preset.Guards.Add(Samples);

    FPerformanceQualityGuard Csv;
    Csv.Id = TEXT("csv-artifact");
    Csv.MetricOrPath = TEXT("capture.csv");
    Csv.Operator = EPerformanceGuardOperator::FileNotEmpty;
    Preset.Guards.Add(Csv);
    return Preset;
}

FPerformanceExperimentPresetData FPerformanceExperimentPresetData::MakeHeroArenaNetwork()
{
    FPerformanceExperimentPresetData Preset;
    Preset.PresetId = TEXT("hero-arena-network");
    Preset.DisplayName = NSLOCTEXT("PerformanceLab", "HeroArenaNetworkPreset", "HeroArena 多实例网络");
    Preset.Domains = { EPerformanceExperimentDomain::FrameCPU, EPerformanceExperimentDomain::Network };
    Preset.Target.MapPath = TEXT("/ShooterMaps/Maps/L_Convolution_Blockout");
    Preset.Target.ExperienceId = TEXT("B_HeroArena_ControlPoints");
    Preset.Target.Topology = EPerformanceProcessTopology::DedicatedServer;
    Preset.Load.AgentCount = 15;
    Preset.Load.Seed = 1337;
    Preset.Capture.WarmupSeconds = 10.0;
    Preset.Capture.DurationSeconds = 15.0;
    Preset.Capture.ReadyTimeoutSeconds = 90.0;

    FPerformanceQualityGuard Pawns;
    Pawns.Id = TEXT("replicated-pawn-load");
    Pawns.MetricOrPath = TEXT("pawnCount");
    Pawns.ExpectedValue = Preset.Load.AgentCount;
    Preset.Guards.Add(Pawns);

    FPerformanceQualityGuard Samples;
    Samples.Id = TEXT("captured-frame-samples");
    Samples.MetricOrPath = TEXT("sampleCount");
    Samples.ExpectedValue = 1.0;
    Preset.Guards.Add(Samples);

    FPerformanceQualityGuard Csv;
    Csv.Id = TEXT("csv-artifact");
    Csv.MetricOrPath = TEXT("capture.csv");
    Csv.Operator = EPerformanceGuardOperator::FileNotEmpty;
    Preset.Guards.Add(Csv);
    return Preset;
}
