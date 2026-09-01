#include "PerformanceTimelineView.h"

#include "PerformanceComparisonContract.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Serialization/Csv/CsvParser.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Styling/AppStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
    class SPerformanceSparkline final : public SLeafWidget
    {
    public:
        SLATE_BEGIN_ARGS(SPerformanceSparkline) {}
            SLATE_ARGUMENT(TArray<float>, Samples)
            SLATE_ARGUMENT(TArray<float>, ComparisonSamples)
            SLATE_ARGUMENT(float, ScaleMax)
            SLATE_ARGUMENT(FLinearColor, Color)
        SLATE_END_ARGS()

        void Construct(const FArguments& Args)
        {
            Samples = Args._Samples;
            ComparisonSamples = Args._ComparisonSamples;
            ScaleMax = FMath::Max(Args._ScaleMax, 0.001f);
            Color = Args._Color;
            SetClipping(EWidgetClipping::ClipToBounds);
        }

        virtual FVector2D ComputeDesiredSize(float) const override
        {
            return FVector2D(720.0f, 42.0f);
        }

        virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
            const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
            int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
        {
            const FVector2D Size = AllottedGeometry.GetLocalSize();
            FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(),
                FAppStyle::GetBrush("WhiteBrush"), ESlateDrawEffect::None,
                FLinearColor(0.035f, 0.04f, 0.045f, 0.72f));

            for (int32 GridIndex = 0; GridIndex <= 4; ++GridIndex)
            {
                const float X = Size.X * GridIndex / 4.0f;
                const TArray<FVector2D> GridLine = { FVector2D(X, 0.0f), FVector2D(X, Size.Y) };
                FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(),
                    GridLine, ESlateDrawEffect::NoPixelSnapping, FLinearColor(0.25f, 0.29f, 0.31f, 0.32f), false, 1.0f);
            }
            const TArray<FVector2D> MidLine = { FVector2D(0.0f, Size.Y * 0.5f), FVector2D(Size.X, Size.Y * 0.5f) };
            FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(),
                MidLine, ESlateDrawEffect::NoPixelSnapping, FLinearColor(0.25f, 0.29f, 0.31f, 0.22f), false, 1.0f);

            DrawSeries(OutDrawElements, AllottedGeometry, Size, LayerId + 2, Samples,
                ComparisonSamples.IsEmpty() ? Color : FLinearColor(0.58f, 0.61f, 0.63f, 0.92f), 1.5f);
            DrawSeries(OutDrawElements, AllottedGeometry, Size, LayerId + 3, ComparisonSamples, Color, 1.8f);
            return LayerId + 3;
        }

    private:
        void DrawSeries(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry,
            const FVector2D& Size, const int32 LayerId, const TArray<float>& Values,
            const FLinearColor& LineColor, const float Thickness) const
        {
            if (Values.Num() > 1)
            {
                TArray<FVector2D> Points;
                Points.Reserve(Values.Num());
                for (int32 Index = 0; Index < Values.Num(); ++Index)
                {
                    const float X = Size.X * Index / static_cast<float>(Values.Num() - 1);
                    const float Normalized = FMath::Clamp(Values[Index] / ScaleMax, 0.0f, 1.0f);
                    Points.Emplace(X, FMath::Lerp(Size.Y - 2.0f, 2.0f, Normalized));
                }
                FSlateDrawElement::MakeLines(OutDrawElements, LayerId, Geometry.ToPaintGeometry(),
                    Points, ESlateDrawEffect::NoPixelSnapping, LineColor, true, Thickness);
            }
        }

        TArray<float> Samples;
        TArray<float> ComparisonSamples;
        float ScaleMax = 1.0f;
        FLinearColor Color = FLinearColor::White;
    };

    FString JsonString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
    {
        FString Value;
        return Object.IsValid() && Object->TryGetStringField(Field, Value) ? Value : FString();
    }

    double JsonNumber(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
    {
        double Value = 0.0;
        return Object.IsValid() && Object->TryGetNumberField(Field, Value) ? Value : 0.0;
    }

    bool JsonBool(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
    {
        bool Value = false;
        return Object.IsValid() && Object->TryGetBoolField(Field, Value) && Value;
    }

    FString DimensionDisplayName(const FString& Dimension)
    {
        if (Dimension == TEXT("Rendering")) return TEXT("角色附加通道 LOD");
        if (Dimension == TEXT("Network")) return TEXT("多人网络");
        if (Dimension == TEXT("AIDecision")) return TEXT("AI 状态机与目标发现");
        if (Dimension == TEXT("Navigation")) return TEXT("寻路重规划");
        if (Dimension == TEXT("AnimationLOD")) return TEXT("动画 LOD");
		if (Dimension == TEXT("RenderingLOD")) return TEXT("角色光追场景 LOD");
        if (Dimension == TEXT("Physics")) return TEXT("物理 LOD");
        if (Dimension == TEXT("Niagara")) return TEXT("Niagara 生命周期");
        if (Dimension == TEXT("WeaponVFX")) return TEXT("武器表现预算");
        if (Dimension == TEXT("LiveWeaponVFX")) return TEXT("真实对局武器表现");
		if (Dimension == TEXT("GunPadVFX")) return TEXT("拾取点常驻特效");
        if (Dimension == TEXT("GameplayTick")) return TEXT("拾取点更新");
		if (Dimension == TEXT("UI")) return TEXT("战术 HUD 更新");
        return Dimension;
    }

    FString VariantDisplayName(const FString& Variant)
    {
        if (Variant == TEXT("Baseline")) return TEXT("改动前");
        if (Variant == TEXT("Optimized")) return TEXT("调整后");
        return Variant;
    }

    FString ProcessRoleDisplayName(const FString& ProcessRole)
    {
        if (ProcessRole == TEXT("Server")) return TEXT("服务器");
        if (ProcessRole == TEXT("Client")) return TEXT("客户端");
        return ProcessRole;
    }

    void Downsample(TArray<float>& Values, const int32 MaxSamples)
    {
        if (Values.Num() <= MaxSamples)
        {
            return;
        }
        const int32 BucketSize = FMath::CeilToInt(static_cast<double>(Values.Num()) / MaxSamples);
        TArray<float> Reduced;
        Reduced.Reserve(MaxSamples);
        for (int32 Start = 0; Start < Values.Num(); Start += BucketSize)
        {
            double Total = 0.0;
            const int32 End = FMath::Min(Start + BucketSize, Values.Num());
            for (int32 Index = Start; Index < End; ++Index)
            {
                Total += Values[Index];
            }
            Reduced.Add(static_cast<float>(Total / FMath::Max(1, End - Start)));
        }
        Values = MoveTemp(Reduced);
    }

    TArray<float> ReadSamples(const FCsvParser::FRows& Rows, const TMap<FString, int32>& Columns,
        const TCHAR* ColumnName)
    {
        TArray<float> Values;
        const int32* Column = Columns.Find(ColumnName);
        if (!Column)
        {
            return Values;
        }
        for (int32 RowIndex = 1; RowIndex < Rows.Num(); ++RowIndex)
        {
            const TArray<const TCHAR*>& Row = Rows[RowIndex];
            if (!Row.IsEmpty() && Row[0]
                && (FCString::Stricmp(Row[0], TEXT("EVENTS")) == 0 || Row[0][0] == TEXT('[')))
            {
                continue;
            }
            if (!Row.IsValidIndex(*Column) || !Row[*Column] || Row[*Column][0] == 0)
            {
                continue;
            }
            double Value = 0.0;
            if (LexTryParseString(Value, Row[*Column]) && FMath::IsFinite(Value))
            {
                Values.Add(static_cast<float>(FMath::Max(0.0, Value)));
            }
        }
        Downsample(Values, 240);
        return Values;
    }

	void ComputeSampleStats(TArray<float> Values, double& OutMean, double& OutP95)
	{
		OutMean = 0.0;
		OutP95 = 0.0;
		if (Values.IsEmpty())
		{
			return;
		}

		double Total = 0.0;
		for (const float Value : Values)
		{
			Total += Value;
		}
		OutMean = Total / Values.Num();
		Values.Sort();
		const int32 P95Index = FMath::Clamp(FMath::FloorToInt((Values.Num() - 1) * 0.95), 0, Values.Num() - 1);
		OutP95 = Values[P95Index];
	}

	void BuildHotspotOverview(const FCsvParser::FRows& Rows, const TMap<FString, int32>& Columns,
		const FCsvParser::FRows* ComparisonRows, const TMap<FString, int32>* ComparisonColumns,
		FPerformanceTimelineView& View)
	{
		struct FHotspotCandidate
		{
			const TCHAR* Section;
			const TCHAR* Label;
			const TCHAR* Column;
		};

		static const FHotspotCandidate Candidates[] = {
			{ TEXT("Game Thread"), TEXT("UI"), TEXT("Exclusive/GameThread/UI") },
			{ TEXT("Game Thread"), TEXT("特效模拟（Effects）"), TEXT("Exclusive/GameThread/Effects") },
			{ TEXT("Game Thread"), TEXT("Actor Tick"), TEXT("Exclusive/GameThread/TickActors") },
			{ TEXT("Game Thread"), TEXT("动画更新"), TEXT("Exclusive/GameThread/Animation") },
			{ TEXT("Game Thread"), TEXT("角色移动"), TEXT("Exclusive/GameThread/CharacterMovement") },
			{ TEXT("Game Thread"), TEXT("物理"), TEXT("Exclusive/GameThread/Physics") },
			{ TEXT("Game Thread"), TEXT("行为树"), TEXT("Exclusive/GameThread/BehaviorTreeTick") },
			{ TEXT("Game Thread"), TEXT("AI 感知"), TEXT("Exclusive/GameThread/AIPerception") },
			{ TEXT("Game Thread"), TEXT("EQS"), TEXT("Exclusive/GameThread/EnvQueryManager") },
			{ TEXT("Game Thread"), TEXT("寻路"), TEXT("Exclusive/GameThread/Pathfinding") },
			{ TEXT("Game Thread"), TEXT("网络接收"), TEXT("Exclusive/GameThread/NetworkIncoming") },
			{ TEXT("工作线程"), TEXT("特效模拟（Effects）"), TEXT("Exclusive/AllWorkers/Effects") },
			{ TEXT("工作线程"), TEXT("Niagara"), TEXT("Exclusive/AllWorkers/Niagara") },
			{ TEXT("工作线程"), TEXT("物理"), TEXT("Exclusive/AllWorkers/Physics") },
			{ TEXT("Render Thread"), TEXT("其他渲染工作"), TEXT("Exclusive/RenderThread/RenderOther") },
			{ TEXT("Render Thread"), TEXT("RDG 资源回收"), TEXT("Exclusive/RenderThread/RDG_CollectResources") },
			{ TEXT("Render Thread"), TEXT("光照"), TEXT("Exclusive/RenderThread/RenderLighting") },
			{ TEXT("Render Thread"), TEXT("阴影"), TEXT("Exclusive/RenderThread/RenderShadows") },
			{ TEXT("Render Thread"), TEXT("Niagara"), TEXT("Exclusive/RenderThread/Niagara") },
			{ TEXT("Render Thread"), TEXT("半透明"), TEXT("Exclusive/RenderThread/RenderTranslucency") },
			{ TEXT("Render Thread"), TEXT("Custom Depth"), TEXT("Exclusive/RenderThread/RenderCustomDepthPass") },
			{ TEXT("GPU Pass"), TEXT("动态光追几何"), TEXT("GPU/RayTracingDynamicGeometry") },
			{ TEXT("GPU Pass"), TEXT("阴影深度"), TEXT("GPU/ShadowDepths") },
			{ TEXT("GPU Pass"), TEXT("Lumen 反射"), TEXT("GPU/LumenReflections") },
			{ TEXT("GPU Pass"), TEXT("TSR"), TEXT("GPU/TemporalSuperResolution") },
			{ TEXT("GPU Pass"), TEXT("Custom Depth"), TEXT("GPU/CustomDepth") }
		};

		for (const FHotspotCandidate& Candidate : Candidates)
		{
			const TArray<float> BaselineSamples = ReadSamples(Rows, Columns, Candidate.Column);
			if (BaselineSamples.IsEmpty())
			{
				continue;
			}

			FPerformanceHotspotView Hotspot;
			Hotspot.Section = Candidate.Section;
			Hotspot.Label = Candidate.Label;
			ComputeSampleStats(BaselineSamples, Hotspot.BaselineMeanMs, Hotspot.BaselineP95Ms);
			if (ComparisonRows && ComparisonColumns)
			{
				const TArray<float> ComparisonSamples = ReadSamples(*ComparisonRows, *ComparisonColumns, Candidate.Column);
				if (!ComparisonSamples.IsEmpty())
				{
					ComputeSampleStats(ComparisonSamples, Hotspot.ComparisonMeanMs, Hotspot.ComparisonP95Ms);
					Hotspot.bHasComparison = true;
				}
			}
			if (FMath::Max(Hotspot.BaselineMeanMs, Hotspot.ComparisonMeanMs) >= 0.02)
			{
				View.Hotspots.Add(MoveTemp(Hotspot));
			}
		}

		View.Hotspots.Sort([](const FPerformanceHotspotView& A, const FPerformanceHotspotView& B)
		{
			return FMath::Max(A.BaselineMeanMs, A.ComparisonMeanMs)
				> FMath::Max(B.BaselineMeanMs, B.ComparisonMeanMs);
		});
		if (View.Hotspots.Num() > 8)
		{
			View.Hotspots.SetNum(8);
		}
	}

	FString ShortObjectName(const FString& ObjectPath)
	{
		FString Result = ObjectPath;
		int32 SlashIndex = INDEX_NONE;
		if (Result.FindLastChar(TEXT('/'), SlashIndex))
		{
			Result.RightChopInline(SlashIndex + 1);
		}
		int32 DotIndex = INDEX_NONE;
		if (Result.FindChar(TEXT('.'), DotIndex))
		{
			Result.RightChopInline(DotIndex + 1);
		}
		return Result;
	}

	TMap<FString, int32> ReadEffectCounts(const TSharedPtr<FJsonObject>& Summary,
		const TCHAR* ArrayField, const TCHAR* LabelField)
	{
		TMap<FString, int32> Counts;
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Summary.IsValid() || !Summary->TryGetArrayField(ArrayField, Values) || !Values)
		{
			return Counts;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			const TSharedPtr<FJsonObject> Entry = Value.IsValid() ? Value->AsObject() : nullptr;
			const FString Label = ShortObjectName(JsonString(Entry, LabelField));
			const int32 Count = FMath::RoundToInt(JsonNumber(Entry, TEXT("activeComponents")));
			if (!Label.IsEmpty() && Count > 0)
			{
				Counts.FindOrAdd(Label) += Count;
			}
		}
		return Counts;
	}

	TArray<FPerformanceEffectSourceView> MergeEffectCounts(const TMap<FString, int32>& Baseline,
		const TMap<FString, int32>& Comparison, const bool bHasComparison, const int32 MaxRows)
	{
		TArray<FPerformanceEffectSourceView> Rows;
		for (const TPair<FString, int32>& Pair : Baseline)
		{
			FPerformanceEffectSourceView Row;
			Row.Label = Pair.Key;
			Row.BaselineCount = Pair.Value;
			Row.ComparisonCount = Comparison.FindRef(Pair.Key);
			Row.bHasComparison = bHasComparison;
			Rows.Add(MoveTemp(Row));
		}
		for (const TPair<FString, int32>& Pair : Comparison)
		{
			if (!Baseline.Contains(Pair.Key))
			{
				FPerformanceEffectSourceView Row;
				Row.Label = Pair.Key;
				Row.ComparisonCount = Pair.Value;
				Row.bHasComparison = bHasComparison;
				Rows.Add(MoveTemp(Row));
			}
		}
		Rows.Sort([](const FPerformanceEffectSourceView& A, const FPerformanceEffectSourceView& B)
		{
			return FMath::Max(A.BaselineCount, A.ComparisonCount)
				> FMath::Max(B.BaselineCount, B.ComparisonCount);
		});
		if (Rows.Num() > MaxRows)
		{
			Rows.SetNum(MaxRows);
		}
		return Rows;
	}

	void AttachDetailedEffectTimings(const FCsvParser::FRows& Rows,
		const TMap<FString, int32>& Columns, const FCsvParser::FRows* ComparisonRows,
		const TMap<FString, int32>* ComparisonColumns, TArray<FPerformanceEffectSourceView>& Sources)
	{
		constexpr const TCHAR* Prefix = TEXT("Particles/Total/");
		for (const TPair<FString, int32>& Column : Columns)
		{
			if (!Column.Key.StartsWith(Prefix))
			{
				continue;
			}
			const FString Label = ShortObjectName(Column.Key.RightChop(FCString::Strlen(Prefix)));
			TArray<float> BaselineSamples = ReadSamples(Rows, Columns, *Column.Key);
			if (Label.IsEmpty() || BaselineSamples.IsEmpty())
			{
				continue;
			}
			FPerformanceEffectSourceView* Source = Sources.FindByPredicate([&Label](const FPerformanceEffectSourceView& Candidate)
			{
				return Candidate.Label == Label;
			});
			if (!Source)
			{
				Source = &Sources.AddDefaulted_GetRef();
				Source->Label = Label;
			}
			ComputeSampleStats(MoveTemp(BaselineSamples), Source->BaselineMeanMs, Source->BaselineP95Ms);
			Source->BaselineMeanMs /= 1000.0;
			Source->BaselineP95Ms /= 1000.0;
			Source->bHasDetailedTiming = true;

			if (ComparisonRows && ComparisonColumns)
			{
				TArray<float> ComparisonSamples = ReadSamples(*ComparisonRows, *ComparisonColumns, *Column.Key);
				if (!ComparisonSamples.IsEmpty())
				{
					ComputeSampleStats(MoveTemp(ComparisonSamples), Source->ComparisonMeanMs, Source->ComparisonP95Ms);
					Source->ComparisonMeanMs /= 1000.0;
					Source->ComparisonP95Ms /= 1000.0;
					Source->bHasComparison = true;
				}
			}
		}

		Sources.Sort([](const FPerformanceEffectSourceView& A, const FPerformanceEffectSourceView& B)
		{
			if (A.bHasDetailedTiming != B.bHasDetailedTiming)
			{
				return A.bHasDetailedTiming;
			}
			const double ACost = FMath::Max(A.BaselineMeanMs, A.ComparisonMeanMs);
			const double BCost = FMath::Max(B.BaselineMeanMs, B.ComparisonMeanMs);
			return A.bHasDetailedTiming && !FMath::IsNearlyEqual(ACost, BCost)
				? ACost > BCost
				: FMath::Max(A.BaselineCount, A.ComparisonCount) > FMath::Max(B.BaselineCount, B.ComparisonCount);
		});
		if (Sources.Num() > 8)
		{
			Sources.SetNum(8);
		}
	}

	void BuildEffectBreakdown(const TSharedPtr<FJsonObject>& BaselineSummary,
		const TSharedPtr<FJsonObject>& ComparisonSummary, FPerformanceTimelineView& View)
	{
		View.bHasEffectBreakdown = View.Hotspots.ContainsByPredicate([](const FPerformanceHotspotView& Hotspot)
		{
			return Hotspot.Label.Contains(TEXT("Effects"));
		});
		if (!View.bHasEffectBreakdown)
		{
			return;
		}

		View.BaselineRegisteredEffects = FMath::RoundToInt(JsonNumber(BaselineSummary, TEXT("worldNiagaraRegisteredAtEnd")));
		View.BaselineActiveEffects = FMath::RoundToInt(JsonNumber(BaselineSummary, TEXT("worldNiagaraActiveAtEnd")));
		View.ComparisonRegisteredEffects = FMath::RoundToInt(JsonNumber(ComparisonSummary, TEXT("worldNiagaraRegisteredAtEnd")));
		View.ComparisonActiveEffects = FMath::RoundToInt(JsonNumber(ComparisonSummary, TEXT("worldNiagaraActiveAtEnd")));

		View.EffectSystems = MergeEffectCounts(
			ReadEffectCounts(BaselineSummary, TEXT("activeNiagaraSystemsAtEnd"), TEXT("system")),
			ReadEffectCounts(ComparisonSummary, TEXT("activeNiagaraSystemsAtEnd"), TEXT("system")),
			View.bHasComparison, 6);
		View.EffectOwners = MergeEffectCounts(
			ReadEffectCounts(BaselineSummary, TEXT("activeNiagaraOwnerClassesAtEnd"), TEXT("ownerClass")),
			ReadEffectCounts(ComparisonSummary, TEXT("activeNiagaraOwnerClassesAtEnd"), TEXT("ownerClass")),
			View.bHasComparison, 5);
	}

    TArray<float> ReadCounterDeltaSamples(const FCsvParser::FRows& Rows, const TMap<FString, int32>& Columns,
        const TCHAR* ColumnName)
    {
        TArray<float> Values;
        const int32* Column = Columns.Find(ColumnName);
        if (!Column)
        {
            return Values;
        }

        bool bHasPrevious = false;
        double Previous = 0.0;
        for (int32 RowIndex = 1; RowIndex < Rows.Num(); ++RowIndex)
        {
            const TArray<const TCHAR*>& Row = Rows[RowIndex];
            if (!Row.IsEmpty() && Row[0]
                && (FCString::Stricmp(Row[0], TEXT("EVENTS")) == 0 || Row[0][0] == TEXT('[')))
            {
                continue;
            }
            if (!Row.IsValidIndex(*Column) || !Row[*Column] || Row[*Column][0] == 0)
            {
                continue;
            }
            double Current = 0.0;
            if (!LexTryParseString(Current, Row[*Column]) || !FMath::IsFinite(Current))
            {
                continue;
            }
            if (bHasPrevious)
            {
                Values.Add(static_cast<float>(FMath::Max(0.0, Current - Previous)));
            }
            Previous = Current;
            bHasPrevious = true;
        }
        Downsample(Values, 240);
        return Values;
    }

    TArray<float> ReadPerActorRateSamples(const FCsvParser::FRows& Rows, const TMap<FString, int32>& Columns,
        const TCHAR* TickColumnName, const TCHAR* ActorCountColumnName, const TCHAR* FrameTimeColumnName)
    {
        TArray<float> Values;
        const int32* TickColumn = Columns.Find(TickColumnName);
        const int32* ActorCountColumn = Columns.Find(ActorCountColumnName);
        const int32* FrameTimeColumn = Columns.Find(FrameTimeColumnName);
        if (!TickColumn || !ActorCountColumn || !FrameTimeColumn)
        {
            return Values;
        }

        for (int32 RowIndex = 1; RowIndex < Rows.Num(); ++RowIndex)
        {
            const TArray<const TCHAR*>& Row = Rows[RowIndex];
            if (!Row.IsEmpty() && Row[0]
                && (FCString::Stricmp(Row[0], TEXT("EVENTS")) == 0 || Row[0][0] == TEXT('[')))
            {
                continue;
            }
            if (!Row.IsValidIndex(*TickColumn) || !Row.IsValidIndex(*ActorCountColumn) || !Row.IsValidIndex(*FrameTimeColumn)
                || !Row[*TickColumn] || !Row[*ActorCountColumn] || !Row[*FrameTimeColumn])
            {
                continue;
            }

            double TickCount = 0.0;
            double ActorCount = 0.0;
            double FrameTimeMs = 0.0;
            if (LexTryParseString(TickCount, Row[*TickColumn])
                && LexTryParseString(ActorCount, Row[*ActorCountColumn])
                && LexTryParseString(FrameTimeMs, Row[*FrameTimeColumn])
                && ActorCount > 0.0 && FrameTimeMs > 0.0)
            {
                Values.Add(static_cast<float>(1000.0 * TickCount / (ActorCount * FrameTimeMs)));
            }
        }
        Downsample(Values, 240);
        return Values;
    }

    void AddTrack(const FCsvParser::FRows& Rows, const TMap<FString, int32>& Columns,
        const FCsvParser::FRows* ComparisonRows, const TMap<FString, int32>* ComparisonColumns,
        const TCHAR* ColumnName, const TCHAR* Label, const TCHAR* Unit, const FLinearColor& Color,
        FPerformanceTimelineView& View, const TCHAR* Section = nullptr, const float ValueScale = 1.0f)
    {
        FPerformanceTimelineTrack Track;
        Track.Section = Section ? Section : TEXT("");
        Track.Label = Label;
        Track.Unit = Unit;
        Track.Color = Color;
        Track.Samples = ReadSamples(Rows, Columns, ColumnName);
        if (ComparisonRows && ComparisonColumns)
        {
            Track.ComparisonSamples = ReadSamples(*ComparisonRows, *ComparisonColumns, ColumnName);
        }
		if (!FMath::IsNearlyEqual(ValueScale, 1.0f))
		{
			for (float& Value : Track.Samples) Value *= ValueScale;
			for (float& Value : Track.ComparisonSamples) Value *= ValueScale;
		}
        if (Track.Samples.IsEmpty())
        {
            return;
        }
        TArray<float> Sorted = Track.Samples;
        Sorted.Append(Track.ComparisonSamples);
        Sorted.Sort();
        const int32 P95Index = FMath::Clamp(FMath::RoundToInt((Sorted.Num() - 1) * 0.95f), 0, Sorted.Num() - 1);
        Track.ScaleMax = FMath::Max(0.001f, Sorted[P95Index] * 1.15f);
        View.Tracks.Add(MoveTemp(Track));
    }

    void AddCounterDeltaTrack(const FCsvParser::FRows& Rows, const TMap<FString, int32>& Columns,
        const FCsvParser::FRows* ComparisonRows, const TMap<FString, int32>* ComparisonColumns,
        const TCHAR* ColumnName, const TCHAR* Label, const TCHAR* Unit, const FLinearColor& Color,
        FPerformanceTimelineView& View)
    {
        FPerformanceTimelineTrack Track;
        Track.Label = Label;
        Track.Unit = Unit;
        Track.Color = Color;
        Track.Samples = ReadCounterDeltaSamples(Rows, Columns, ColumnName);
        if (ComparisonRows && ComparisonColumns)
        {
            Track.ComparisonSamples = ReadCounterDeltaSamples(*ComparisonRows, *ComparisonColumns, ColumnName);
        }
        if (Track.Samples.IsEmpty())
        {
            return;
        }
        TArray<float> Sorted = Track.Samples;
        Sorted.Append(Track.ComparisonSamples);
        Sorted.Sort();
        const int32 P95Index = FMath::Clamp(FMath::RoundToInt((Sorted.Num() - 1) * 0.95f), 0, Sorted.Num() - 1);
        Track.ScaleMax = FMath::Max(0.001f, Sorted[P95Index] * 1.15f);
        View.Tracks.Add(MoveTemp(Track));
    }

    void AddPerActorRateTrack(const FCsvParser::FRows& Rows, const TMap<FString, int32>& Columns,
        const FCsvParser::FRows* ComparisonRows, const TMap<FString, int32>* ComparisonColumns,
        const TCHAR* Label, const FLinearColor& Color, FPerformanceTimelineView& View)
    {
        FPerformanceTimelineTrack Track;
        Track.Label = Label;
        Track.Unit = TEXT("Hz");
        Track.Color = Color;
        Track.Samples = ReadPerActorRateSamples(Rows, Columns, TEXT("Ticks/LyraWeaponSpawner"),
            TEXT("ActorCount/LyraWeaponSpawner"), TEXT("PerformanceLab/FrameTimeMs"));
        if (ComparisonRows && ComparisonColumns)
        {
            Track.ComparisonSamples = ReadPerActorRateSamples(*ComparisonRows, *ComparisonColumns,
                TEXT("Ticks/LyraWeaponSpawner"), TEXT("ActorCount/LyraWeaponSpawner"), TEXT("PerformanceLab/FrameTimeMs"));
        }
        if (Track.Samples.IsEmpty())
        {
            return;
        }
        TArray<float> Sorted = Track.Samples;
        Sorted.Append(Track.ComparisonSamples);
        Sorted.Sort();
        const int32 P95Index = FMath::Clamp(FMath::RoundToInt((Sorted.Num() - 1) * 0.95f), 0, Sorted.Num() - 1);
        Track.ScaleMax = FMath::Max(0.001f, Sorted[P95Index] * 1.15f);
        View.Tracks.Add(MoveTemp(Track));
    }

    struct FTimelineRunRecord
    {
        FString Directory;
        FString StartedUtc;
        FString GroupId;
        FString PresetId;
        FString Dimension;
        FString Variant;
        FString ProcessRole;
        FString ContractKey;
        TSharedPtr<FJsonObject> Summary;
    };

    struct FTimelineRunIndex
    {
        FString RunsRoot;
        TArray<FTimelineRunRecord> Records;
    };

    TUniquePtr<FTimelineRunIndex> TimelineRunIndex;

    FString NormalizeTimelineBatchId(FString Value)
    {
        const int32 RunMarker = Value.Find(TEXT("-run-"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
        if (RunMarker != INDEX_NONE)
        {
            Value.LeftInline(RunMarker);
        }
        return Value;
    }

    const TArray<FTimelineRunRecord>& GetTimelineRunRecords(const FString& RunsRoot)
    {
        if (TimelineRunIndex && TimelineRunIndex->RunsRoot == RunsRoot)
        {
            return TimelineRunIndex->Records;
        }

        TimelineRunIndex = MakeUnique<FTimelineRunIndex>();
        TimelineRunIndex->RunsRoot = RunsRoot;

        TArray<FString> Directories;
        IFileManager::Get().FindFiles(Directories, *FPaths::Combine(RunsRoot, TEXT("*")), false, true);
        TimelineRunIndex->Records.Reserve(Directories.Num());
        for (const FString& Directory : Directories)
        {
            FString Json;
            TSharedPtr<FJsonObject> Summary;
            const FString SummaryPath = FPaths::Combine(RunsRoot, Directory, TEXT("summary.json"));
            const FString CapturePath = FPaths::Combine(RunsRoot, Directory, TEXT("capture.csv"));
            if (!FFileHelper::LoadFileToString(Json, *SummaryPath)
                || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Summary)
                || !Summary.IsValid() || !JsonBool(Summary, TEXT("valid"))
                || IFileManager::Get().FileSize(*CapturePath) <= 0)
            {
                continue;
            }

            FTimelineRunRecord& Record = TimelineRunIndex->Records.AddDefaulted_GetRef();
            Record.Directory = Directory;
            Record.StartedUtc = JsonString(Summary, TEXT("startedUtc"));
            Record.GroupId = JsonString(Summary, TEXT("sessionId"));
            if (Record.GroupId.IsEmpty())
            {
                Record.GroupId = JsonString(Summary, TEXT("runId"));
            }
            Record.PresetId = JsonString(Summary, TEXT("presetId"));
            Record.Dimension = JsonString(Summary, TEXT("dimension"));
            Record.Variant = JsonString(Summary, TEXT("variant"));
            Record.ProcessRole = JsonString(Summary, TEXT("processRole"));
            Record.ContractKey = PerformanceComparisonContract::BuildKey(Summary);
            Record.Summary = MoveTemp(Summary);
        }
        return TimelineRunIndex->Records;
    }
}

void PerformanceWorkbenchTimeline::InvalidateIndex()
{
    TimelineRunIndex.Reset();
}

TArray<FString> PerformanceWorkbenchTimeline::ListRecentGroups(const FString& RunsRoot, const int32 MaxGroups)
{
	return ListRecentGroups(RunsRoot, TConstArrayView<FPerformanceComparisonQuery>(), MaxGroups);
}

TArray<FString> PerformanceWorkbenchTimeline::ListRecentGroups(const FString& RunsRoot,
	const TConstArrayView<FPerformanceComparisonQuery> AllowedQueries, const int32 MaxGroups)
{
    TMap<FString, FString> LatestByGroup;
    TMap<FString, FString> DimensionByGroup;
    for (const FTimelineRunRecord& Record : GetTimelineRunRecords(RunsRoot))
    {
		if (!AllowedQueries.IsEmpty() && !AllowedQueries.ContainsByPredicate([&Record](const FPerformanceComparisonQuery& Query)
		{
			return Query.PresetId == Record.PresetId && Query.Dimension == Record.Dimension;
		}))
		{
			continue;
		}

        FString& Latest = LatestByGroup.FindOrAdd(Record.GroupId);
        DimensionByGroup.FindOrAdd(Record.GroupId) = Record.Dimension;
        if (Record.StartedUtc > Latest)
        {
            Latest = Record.StartedUtc;
        }
    }

    TArray<FString> Groups;
    LatestByGroup.GetKeys(Groups);
    Groups.Sort([&LatestByGroup](const FString& A, const FString& B)
    {
        return LatestByGroup.FindChecked(A) > LatestByGroup.FindChecked(B);
    });
    TSet<FString> IncludedGroups;
    for (int32 Index = 0; Index < FMath::Min(MaxGroups, Groups.Num()); ++Index)
    {
        IncludedGroups.Add(Groups[Index]);
    }
    TMap<FString, int32> IncludedPerDimension;
    for (const FString& Group : Groups)
    {
        const FString& Dimension = DimensionByGroup.FindChecked(Group);
        int32& Count = IncludedPerDimension.FindOrAdd(Dimension);
        if (Count < 3)
        {
            IncludedGroups.Add(Group);
            ++Count;
        }
    }

    TArray<FPerformanceComparisonQuery> Queries;
    Queries.Append(AllowedQueries.GetData(), AllowedQueries.Num());
    if (Queries.IsEmpty())
    {
        for (const FTimelineRunRecord& Record : GetTimelineRunRecords(RunsRoot))
        {
            if (!Queries.ContainsByPredicate([&Record](const FPerformanceComparisonQuery& Query)
            {
                return Query.PresetId == Record.PresetId && Query.Dimension == Record.Dimension;
            }))
            {
                Queries.Add({ Record.PresetId, Record.Dimension });
            }
        }
    }
    for (const FPerformanceComparisonQuery& Query : Queries)
    {
        const FString CompleteGroup = FindLatestCompleteGroup(RunsRoot, Query.PresetId, Query.Dimension);
        if (!CompleteGroup.IsEmpty())
        {
            IncludedGroups.Add(CompleteGroup);
        }
    }

    TArray<FString> Result;
    for (const FString& Group : Groups)
    {
        if (IncludedGroups.Contains(Group))
        {
            Result.Add(Group);
        }
    }
    return Result;
}

TArray<FString> PerformanceWorkbenchTimeline::ListRoles(const FString& RunsRoot, const FString& GroupId)
{
    TSet<FString> UniqueRoles;
    for (const FTimelineRunRecord& Record : GetTimelineRunRecords(RunsRoot))
    {
        if (Record.GroupId == GroupId)
        {
            UniqueRoles.Add(Record.ProcessRole);
        }
    }
    TArray<FString> Roles = UniqueRoles.Array();
    Roles.Sort([](const FString& A, const FString& B)
    {
        if (A == B) return false;
        if (A == TEXT("Server")) return true;
        if (B == TEXT("Server")) return false;
        return A < B;
    });
    return Roles;
}

FString PerformanceWorkbenchTimeline::FindLatestGroup(const FString& RunsRoot, const FString& PresetId, const FString& Dimension)
{
	FString LatestGroup;
	FString LatestStartedUtc;
	for (const FTimelineRunRecord& Record : GetTimelineRunRecords(RunsRoot))
	{
		if (Record.PresetId != PresetId || Record.Dimension != Dimension)
		{
			continue;
		}

		if (Record.StartedUtc > LatestStartedUtc)
		{
			LatestStartedUtc = Record.StartedUtc;
			LatestGroup = Record.GroupId;
		}
	}
	return LatestGroup;
}

FString PerformanceWorkbenchTimeline::FindLatestCompleteGroup(
	const FString& RunsRoot, const FString& PresetId, const FString& Dimension)
{
	struct FBatchCounts
	{
		int32 Baseline = 0;
		int32 Optimized = 0;
		FString LatestGroup;
		FString LatestStartedUtc;
	};

	TMap<FString, FBatchCounts> Batches;
	for (const FTimelineRunRecord& Record : GetTimelineRunRecords(RunsRoot))
	{
		const bool bComparisonRole = Record.ProcessRole.IsEmpty()
			|| Record.ProcessRole == TEXT("Standalone") || Record.ProcessRole == TEXT("Server");
		if (Record.PresetId != PresetId || Record.Dimension != Dimension || !bComparisonRole)
		{
			continue;
		}

		const FString BatchKey = Record.ContractKey + TEXT("\n") + NormalizeTimelineBatchId(Record.GroupId);
		FBatchCounts& Counts = Batches.FindOrAdd(BatchKey);
		Counts.Baseline += Record.Variant == TEXT("Baseline") ? 1 : 0;
		Counts.Optimized += Record.Variant == TEXT("Optimized") ? 1 : 0;
		if (Record.StartedUtc > Counts.LatestStartedUtc)
		{
			Counts.LatestStartedUtc = Record.StartedUtc;
			Counts.LatestGroup = Record.GroupId;
		}
	}

	FString SelectedGroup;
	FString SelectedStartedUtc;
	for (const TPair<FString, FBatchCounts>& Pair : Batches)
	{
		const FBatchCounts& Counts = Pair.Value;
		if (Counts.Baseline >= 3 && Counts.Optimized >= 3 && Counts.LatestStartedUtc > SelectedStartedUtc)
		{
			SelectedStartedUtc = Counts.LatestStartedUtc;
			SelectedGroup = Counts.LatestGroup;
		}
	}
	return SelectedGroup;
}

FPerformanceTimelineView PerformanceWorkbenchTimeline::LoadGroup(const FString& RunsRoot, const FString& GroupId, const FString& PreferredRole)
{
    const TArray<FTimelineRunRecord>& Records = GetTimelineRunRecords(RunsRoot);
    int32 LatestRoleScore = -1;
    FString LatestStartedUtc;
    FString LatestDirectory;
    TSharedPtr<FJsonObject> LatestSummary;
    for (const FTimelineRunRecord& Record : Records)
    {
        const bool bRequestedRole = PreferredRole.IsEmpty() || Record.ProcessRole == PreferredRole;
        const int32 RoleScore = !PreferredRole.IsEmpty() ? 2
            : Record.Dimension == TEXT("Network") && Record.ProcessRole == TEXT("Server") ? 1 : 0;
        if (Record.GroupId == GroupId
            && bRequestedRole
            && (RoleScore > LatestRoleScore || (RoleScore == LatestRoleScore && Record.StartedUtc > LatestStartedUtc)))
        {
            LatestStartedUtc = Record.StartedUtc;
            LatestRoleScore = RoleScore;
            LatestDirectory = Record.Directory;
            LatestSummary = Record.Summary;
        }
    }

    FPerformanceTimelineView View;
    if (!LatestSummary.IsValid())
    {
        return View;
    }

    const FTimelineRunRecord* LatestRecord = Records.FindByPredicate([&LatestDirectory](const FTimelineRunRecord& Record)
    {
        return Record.Directory == LatestDirectory;
    });
    const FTimelineRunRecord* ComparisonRecord = nullptr;
    if (LatestRecord && !LatestRecord->GroupId.IsEmpty())
    {
        for (const FTimelineRunRecord& Record : Records)
        {
            const bool bOppositeVariant = (LatestRecord->Variant == TEXT("Baseline") && Record.Variant == TEXT("Optimized"))
                || (LatestRecord->Variant == TEXT("Optimized") && Record.Variant == TEXT("Baseline"));
            if (Record.GroupId == LatestRecord->GroupId && Record.ContractKey == LatestRecord->ContractKey
                && bOppositeVariant
                && Record.ProcessRole == LatestRecord->ProcessRole
                && (!ComparisonRecord || Record.StartedUtc > ComparisonRecord->StartedUtc))
            {
                ComparisonRecord = &Record;
            }
        }
    }
    if (LatestRecord && !ComparisonRecord)
    {
        const FString SelectedBatchId = NormalizeTimelineBatchId(LatestRecord->GroupId);
        for (const FTimelineRunRecord& Record : Records)
        {
            const bool bOppositeVariant = (LatestRecord->Variant == TEXT("Baseline") && Record.Variant == TEXT("Optimized"))
                || (LatestRecord->Variant == TEXT("Optimized") && Record.Variant == TEXT("Baseline"));
            if (NormalizeTimelineBatchId(Record.GroupId) == SelectedBatchId
                && Record.ContractKey == LatestRecord->ContractKey && bOppositeVariant
                && Record.ProcessRole == LatestRecord->ProcessRole
                && (!ComparisonRecord || Record.StartedUtc > ComparisonRecord->StartedUtc))
            {
                ComparisonRecord = &Record;
            }
        }
    }

    const FTimelineRunRecord* BaselineRecord = LatestRecord;
    const FTimelineRunRecord* OptimizedRecord = ComparisonRecord;
    if (LatestRecord && LatestRecord->Variant == TEXT("Optimized") && ComparisonRecord)
    {
        BaselineRecord = ComparisonRecord;
        OptimizedRecord = LatestRecord;
    }

    FString BaselineCsvText;
    if (!BaselineRecord || !FFileHelper::LoadFileToString(BaselineCsvText,
        *FPaths::Combine(RunsRoot, BaselineRecord->Directory, TEXT("capture.csv"))))
    {
        return View;
    }
    const FCsvParser Parser(MoveTemp(BaselineCsvText));
    const FCsvParser::FRows& Rows = Parser.GetRows();
    if (Rows.Num() < 2)
    {
        return View;
    }

    TMap<FString, int32> Columns;
    for (int32 Index = 0; Index < Rows[0].Num(); ++Index)
    {
        Columns.Add(Rows[0][Index], Index);
    }

    FString OptimizedCsvText;
    TUniquePtr<FCsvParser> ComparisonParser;
    const FCsvParser::FRows* ComparisonRows = nullptr;
    TMap<FString, int32> ComparisonColumns;
    if (OptimizedRecord && FFileHelper::LoadFileToString(OptimizedCsvText,
        *FPaths::Combine(RunsRoot, OptimizedRecord->Directory, TEXT("capture.csv"))))
    {
        ComparisonParser = MakeUnique<FCsvParser>(MoveTemp(OptimizedCsvText));
        ComparisonRows = &ComparisonParser->GetRows();
        if (ComparisonRows->Num() > 1)
        {
            for (int32 Index = 0; Index < (*ComparisonRows)[0].Num(); ++Index)
            {
                ComparisonColumns.Add((*ComparisonRows)[0][Index], Index);
            }
        }
        else
        {
            ComparisonRows = nullptr;
        }
    }

    View.RunId = JsonString(BaselineRecord->Summary, TEXT("runId"));
    View.ComparisonRunId = OptimizedRecord ? JsonString(OptimizedRecord->Summary, TEXT("runId")) : FString();
    View.GroupId = BaselineRecord->GroupId;
	View.PresetId = BaselineRecord->PresetId;
	View.Dimension = BaselineRecord->Dimension;
    View.bHasComparison = OptimizedRecord && ComparisonRows;
    View.DurationSeconds = JsonNumber(LatestSummary, TEXT("measurementSeconds"));
    const FString Dimension = JsonString(BaselineRecord->Summary, TEXT("dimension"));
    const bool bHeroArenaRendering = BaselineRecord->PresetId == TEXT("hero-arena-character-rendering")
        && Columns.Contains(TEXT("GPU/ShadowDepths"));
    const FString DimensionLabel = bHeroArenaRendering ? TEXT("角色附加通道 LOD") : DimensionDisplayName(Dimension);
    const FString Variant = View.bHasComparison ? TEXT("改动前 / 调整后") : VariantDisplayName(JsonString(BaselineRecord->Summary, TEXT("variant")));
    const FString ProcessRole = BaselineRecord->ProcessRole;
    View.Detail = ProcessRole.IsEmpty() || ProcessRole == TEXT("Standalone")
        ? FString::Printf(TEXT("%s · %s · %s · %.0f 秒"), *DimensionLabel, *Variant,
            *JsonString(LatestSummary, TEXT("rhiName")), View.DurationSeconds)
        : FString::Printf(TEXT("%s · %s · %s · %s · %.0f 秒"), *DimensionLabel, *Variant, *ProcessRoleDisplayName(ProcessRole),
            *JsonString(LatestSummary, TEXT("rhiName")), View.DurationSeconds);

    TArray<FString> ConditionParts;
    const FString MapName = JsonString(BaselineRecord->Summary, TEXT("map"));
    if (!MapName.IsEmpty())
    {
        ConditionParts.Add(FString::Printf(TEXT("地图 %s"), *MapName));
    }
    const int32 TargetAgents = FMath::RoundToInt(JsonNumber(BaselineRecord->Summary, TEXT("targetAgentCount")));
    if (TargetAgents > 0)
    {
        ConditionParts.Add(FString::Printf(TEXT("%d AI"), TargetAgents));
    }
    if (Dimension == TEXT("Network"))
    {
        const int32 Connections = FMath::RoundToInt(JsonNumber(BaselineRecord->Summary, TEXT("netConnectionCount")));
        if (Connections > 0)
        {
            ConditionParts.Add(FString::Printf(TEXT("%d 客户端"), Connections));
        }
    }
    else if (Dimension == TEXT("Physics"))
    {
        const int32 PhysicsBodies = FMath::RoundToInt(JsonNumber(BaselineRecord->Summary, TEXT("targetPhysicsBodyCount")));
        if (PhysicsBodies > 0)
        {
            ConditionParts.Add(FString::Printf(TEXT("%d 刚体"), PhysicsBodies));
        }
    }
    else if (Dimension == TEXT("Niagara"))
    {
        const int32 StatusSlots = FMath::RoundToInt(JsonNumber(BaselineRecord->Summary, TEXT("targetNiagaraStatusSlotCount")));
        if (StatusSlots > 0)
        {
            ConditionParts.Add(FString::Printf(TEXT("%d 状态槽"), StatusSlots));
        }
    }
    ConditionParts.Add(FString::Printf(TEXT("Seed %.0f"), JsonNumber(BaselineRecord->Summary, TEXT("seed"))));
    const TSharedPtr<FJsonObject>* Environment = nullptr;
    if (BaselineRecord->Summary->TryGetObjectField(TEXT("environment"), Environment) && Environment && Environment->IsValid())
    {
        const int32 ResolutionX = FMath::RoundToInt(JsonNumber(*Environment, TEXT("resolutionX")));
        const int32 ResolutionY = FMath::RoundToInt(JsonNumber(*Environment, TEXT("resolutionY")));
        if (ResolutionX > 0 && ResolutionY > 0)
        {
            ConditionParts.Add(FString::Printf(TEXT("%d×%d"), ResolutionX, ResolutionY));
        }
    }
    ConditionParts.Add(FString::Printf(TEXT("预热 %.0f 秒"), JsonNumber(BaselineRecord->Summary, TEXT("warmupSeconds"))));
    FString Revision = JsonString(BaselineRecord->Summary, TEXT("sourceRevision"));
    if (!Revision.IsEmpty())
    {
        Revision.LeftInline(8);
        ConditionParts.Add(FString::Printf(TEXT("源码 %s"), *Revision));
    }
    View.Conditions = FString::Join(ConditionParts, TEXT("  ·  "));
	BuildHotspotOverview(Rows, Columns, ComparisonRows,
		ComparisonRows ? &ComparisonColumns : nullptr, View);
	BuildEffectBreakdown(BaselineRecord->Summary,
		OptimizedRecord ? OptimizedRecord->Summary : nullptr, View);
	if (View.bHasEffectBreakdown)
	{
		AttachDetailedEffectTimings(Rows, Columns, ComparisonRows,
			ComparisonRows ? &ComparisonColumns : nullptr, View.EffectSystems);
	}

    if (Dimension == TEXT("Network") && ProcessRole == TEXT("Server"))
    {
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("PerformanceLab/NetConnections"), TEXT("客户端连接"), TEXT("个"), FLinearColor(0.43f, 0.75f, 1.0f), View);
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("Replication/NumberOfActiveActors"), TEXT("活动复制 Actor"), TEXT("个"), FLinearColor(0.40f, 0.82f, 0.67f), View);
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("PerformanceLab/NetOutBytesPerSecond"), TEXT("出站带宽"), TEXT("B/s"), FLinearColor(0.30f, 0.67f, 0.92f), View);
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("Replication/ServerReplicateActorTimeMS"), TEXT("复制阶段"), TEXT("ms"), FLinearColor(0.95f, 0.59f, 0.22f), View);
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("Replication/GatherPrioritizeTimeMS"), TEXT("收集与排序"), TEXT("ms"), FLinearColor(0.70f, 0.52f, 0.92f), View);
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("Replication/OutPacketsPerFrame"), TEXT("出站数据包"), TEXT("包/帧"), FLinearColor(0.88f, 0.72f, 0.26f), View);
    }
    else if (Dimension == TEXT("Network"))
    {
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("PerformanceLab/NetConnections"), TEXT("服务器连接"), TEXT("个"), FLinearColor(0.43f, 0.75f, 1.0f), View);
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("PerformanceLab/NetInBytesPerSecond"), TEXT("入站带宽"), TEXT("B/s"), FLinearColor(0.30f, 0.67f, 0.92f), View);
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("Replication/Ping"), TEXT("网络延迟"), TEXT("ms"), FLinearColor(0.95f, 0.59f, 0.22f), View);
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("Replication/InPacketsLost"), TEXT("入站丢包"), TEXT("包"), FLinearColor(0.93f, 0.40f, 0.38f), View);
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("Replication/InPacketsPerFrame"), TEXT("入站数据包"), TEXT("包/帧"), FLinearColor(0.88f, 0.72f, 0.26f), View);
    }
    else if (Dimension == TEXT("GameplayTick"))
    {
        AddPerActorRateTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("单点更新频率"), FLinearColor(0.91f, 0.68f, 0.24f), View);
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("Exclusive/GameThread/TickActors"), TEXT("Actor Tick"), TEXT("ms"), FLinearColor(0.95f, 0.59f, 0.22f), View);
    }
	else if (Dimension == TEXT("UI"))
	{
		AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("HeroArenaHUD/RefreshCount"), TEXT("HUD 刷新"), TEXT("次/帧"), FLinearColor(0.32f, 0.78f, 0.74f), View);
		AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("HeroArenaHUD/GameThread/Refresh"), TEXT("战术 HUD 更新"), TEXT("ms"), FLinearColor(0.95f, 0.59f, 0.22f), View);
		AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("Exclusive/GameThread/UI"), TEXT("UI 总时间"), TEXT("ms"), FLinearColor(0.70f, 0.52f, 0.92f), View);
	}
    else if (Dimension == TEXT("Niagara"))
    {
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("PerformanceLab/NiagaraStatusActivationCount"), TEXT("状态特效激活"), TEXT("次"), FLinearColor(0.95f, 0.59f, 0.22f), View);
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("PerformanceLab/NiagaraActiveComponentCount"), TEXT("活动特效"), TEXT("个"), FLinearColor(0.88f, 0.40f, 0.72f), View);
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("Exclusive/AllWorkers/Niagara"), TEXT("Niagara 工作线程"), TEXT("ms"), FLinearColor(0.70f, 0.52f, 0.92f), View);
    }
    else if (Dimension == TEXT("WeaponVFX"))
    {
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("PerformanceLab/NiagaraRequestedBurstCount"), TEXT("请求事件"), TEXT("次"), FLinearColor(0.82f, 0.72f, 0.48f), View);
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("PerformanceLab/NiagaraBurstSpawnCount"), TEXT("生成事件"), TEXT("次"), FLinearColor(0.95f, 0.59f, 0.22f), View);
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("PerformanceLab/NiagaraCulledBurstCount"), TEXT("省略远场"), TEXT("次"), FLinearColor(0.93f, 0.40f, 0.38f), View);
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("Exclusive/RenderThread/Niagara"), TEXT("Niagara 渲染线程"), TEXT("ms"), FLinearColor(0.88f, 0.40f, 0.72f), View);
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("Exclusive/AllWorkers/Niagara"), TEXT("Niagara 工作线程"), TEXT("ms"), FLinearColor(0.70f, 0.52f, 0.92f), View);
    }
	else if (Dimension == TEXT("LiveWeaponVFX"))
	{
		AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("LyraWeaponVFX/PresentationEvents"), TEXT("武器表现事件"), TEXT("次/帧"), FLinearColor(0.82f, 0.72f, 0.48f), View);
		AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("LyraWeaponVFX/FullDetailEvents"), TEXT("完整表现"), TEXT("次/帧"), FLinearColor(0.95f, 0.59f, 0.22f), View);
		AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("LyraWeaponVFX/ReducedDetailEvents"), TEXT("远距降级"), TEXT("次/帧"), FLinearColor(0.32f, 0.78f, 0.74f), View);
		AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("LyraWeaponVFX/ShellEjectSuppressed"), TEXT("省略弹壳"), TEXT("次/帧"), FLinearColor(0.70f, 0.52f, 0.92f), View);
		AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("LyraWeaponVFX/MuzzleFlashSuppressed"), TEXT("省略枪口焰"), TEXT("次/帧"), FLinearColor(0.88f, 0.40f, 0.72f), View);
		AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("Exclusive/AllWorkers/Niagara"), TEXT("Niagara 工作线程"), TEXT("ms"), FLinearColor(0.70f, 0.52f, 0.92f), View);
	}
	else if (Dimension == TEXT("GunPadVFX"))
	{
		AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("Particles/Count/NS_GunPad"), TEXT("GunPad 活跃实例"), TEXT("个"), FLinearColor(0.32f, 0.78f, 0.74f), View, TEXT("拾取点特效"));
		AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("Particles/Total/NS_GunPad"), TEXT("GunPad CPU 累计"), TEXT("ms"), FLinearColor(0.95f, 0.59f, 0.22f), View, TEXT("拾取点特效"), 0.001f);
		AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("Exclusive/RenderThread/Niagara"), TEXT("Niagara 渲染线程"), TEXT("ms"), FLinearColor(0.88f, 0.40f, 0.72f), View);
		AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("Exclusive/AllWorkers/Niagara"), TEXT("Niagara 工作线程"), TEXT("ms"), FLinearColor(0.70f, 0.52f, 0.92f), View);
	}
    else if (Dimension == TEXT("Physics"))
    {
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("PerformanceLab/PhysicsSimulatedBodyCount"), TEXT("模拟刚体"), TEXT("个"), FLinearColor(0.24f, 0.76f, 0.82f), View);
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("PerformanceLab/PhysicsLODDisabledBodyCount"), TEXT("冻结刚体"), TEXT("个"), FLinearColor(0.43f, 0.75f, 1.0f), View);
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("Exclusive/AllWorkers/Physics"), TEXT("物理工作线程"), TEXT("ms"), FLinearColor(0.70f, 0.52f, 0.92f), View);
    }
    else if (Dimension == TEXT("AIDecision"))
    {
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("PerformanceLab/CrowdTargetedAgents"), TEXT("已获取目标"), TEXT("个"), FLinearColor(0.95f, 0.59f, 0.22f), View);
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("PerformanceLab/CrowdChasingAgents"), TEXT("追击状态"), TEXT("个"), FLinearColor(0.32f, 0.78f, 0.74f), View);
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("PerformanceLab/CrowdEngagingAgents"), TEXT("交战状态"), TEXT("个"), FLinearColor(0.93f, 0.40f, 0.38f), View);
        AddCounterDeltaTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("PerformanceLab/CrowdDecisionCount"), TEXT("状态机决策"), TEXT("次/帧"), FLinearColor(0.46f, 0.78f, 0.47f), View);
        AddCounterDeltaTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("PerformanceLab/CrowdCandidateChecks"), TEXT("候选检查"), TEXT("次/帧"), FLinearColor(0.91f, 0.68f, 0.24f), View);
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("PerformanceLab/CrowdDecisionWorkTimeMs"), TEXT("目标发现开销"), TEXT("ms"), FLinearColor(0.70f, 0.52f, 0.92f), View);
    }
    else if (Dimension == TEXT("Navigation"))
    {
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("PerformanceLab/NavMovingAgents"), TEXT("移动角色"), TEXT("个"), FLinearColor(0.46f, 0.78f, 0.47f), View);
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("PerformanceLab/NavStuckAgents"), TEXT("持续卡住"), TEXT("个"), FLinearColor(0.91f, 0.68f, 0.24f), View);
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("PerformanceLab/NavDecisionWorkTimeMs"), TEXT("重规划开销"), TEXT("ms"), FLinearColor(0.70f, 0.52f, 0.92f), View);
    }
    else if (Dimension == TEXT("AnimationLOD"))
    {
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("PerformanceLab/AnimationUROEnabled"), TEXT("URO 角色"), TEXT("个"), FLinearColor(0.70f, 0.52f, 0.92f), View);
		AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("PerformanceLab/AnimationPostProcessDisabled"), TEXT("省略后处理动画"), TEXT("个"), FLinearColor(0.32f, 0.78f, 0.74f), View);
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("Exclusive/GameThread/Animation"), TEXT("动画更新"), TEXT("ms"), FLinearColor(0.95f, 0.59f, 0.22f), View);
    }
    else if (Dimension == TEXT("Rendering") || Dimension == TEXT("RenderingLOD"))
    {
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("PerformanceLab/ShadowCastingCharacters"), TEXT("投影角色"), TEXT("个"), FLinearColor(0.93f, 0.40f, 0.38f), View, TEXT("场景负载"));
		if (Dimension == TEXT("RenderingLOD"))
		{
			AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("PerformanceLab/ReducedAttachedShadowPrimitives"), TEXT("省略阴影的附件"), TEXT("个"), FLinearColor(0.96f, 0.62f, 0.28f), View);
			AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("PerformanceLab/RayTracingExcludedCharacters"), TEXT("退出光追场景的角色"), TEXT("个"), FLinearColor(0.38f, 0.78f, 0.68f), View);
			AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("PerformanceLab/RayTracingExcludedAttachedPrimitives"), TEXT("退出光追场景的外观"), TEXT("个"), FLinearColor(0.42f, 0.66f, 0.92f), View);
		}
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("PerformanceLab/CustomDepthCharacters"), TEXT("Custom Depth 角色"), TEXT("个"), FLinearColor(0.70f, 0.52f, 0.92f), View, TEXT("场景负载"));
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("PerformanceLab/DrawnPrimitives"), TEXT("绘制图元"), TEXT("个"), FLinearColor(0.91f, 0.68f, 0.24f), View, TEXT("场景负载"));
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("Exclusive/AllWorkers/RenderShadows"), TEXT("阴影工作线程"), TEXT("ms"), FLinearColor(0.70f, 0.52f, 0.92f), View, TEXT("场景负载"));
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("GPU/ShadowDepths"), TEXT("GPU 阴影深度"), TEXT("ms"), FLinearColor(0.95f, 0.52f, 0.30f), View, TEXT("GPU 分阶段"));
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("GPU/TemporalSuperResolution"), TEXT("GPU TSR"), TEXT("ms"), FLinearColor(0.91f, 0.68f, 0.24f), View, TEXT("GPU 分阶段"));
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("GPU/LumenReflections"), TEXT("GPU Lumen 反射"), TEXT("ms"), FLinearColor(0.24f, 0.76f, 0.82f), View, TEXT("GPU 分阶段"));
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("GPU/NaniteVisBuffer"), TEXT("GPU Nanite 可见性"), TEXT("ms"), FLinearColor(0.43f, 0.75f, 1.0f), View, TEXT("GPU 分阶段"));
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("GPU/CustomDepth"), TEXT("GPU Custom Depth"), TEXT("ms"), FLinearColor(0.70f, 0.52f, 0.92f), View, TEXT("GPU 分阶段"));
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("GPU/RayTracingDynamicGeometry"), TEXT("GPU 光追动态几何"), TEXT("ms"), FLinearColor(0.88f, 0.40f, 0.72f), View, TEXT("GPU 分阶段"));
    }
    else
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("PerformanceLab/LyraCharacters"), TEXT("场景角色"), TEXT("个"), FLinearColor(0.43f, 0.75f, 1.0f), View);

    AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("PerformanceLab/FrameTimeMs"), TEXT("Frame"), TEXT("ms"), FLinearColor(0.34f, 0.88f, 0.78f), View, TEXT("整帧与线程"));
    AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("PerformanceLab/GameThreadTimeMs"), TEXT("Game"), TEXT("ms"), FLinearColor(0.95f, 0.59f, 0.22f), View, TEXT("整帧与线程"));
    const bool bRenderingProcess = JsonString(BaselineRecord->Summary, TEXT("rhiName")) != TEXT("Null");
    if (Dimension != TEXT("Network") || (ProcessRole != TEXT("Server") && bRenderingProcess))
    {
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("PerformanceLab/RenderThreadTimeMs"), TEXT("Render"), TEXT("ms"), FLinearColor(0.70f, 0.52f, 0.92f), View, TEXT("整帧与线程"));
        AddTrack(Rows, Columns, ComparisonRows, &ComparisonColumns, TEXT("PerformanceLab/GPUTimeMs"), TEXT("GPU 总计"), TEXT("ms"), FLinearColor(0.93f, 0.40f, 0.38f), View, TEXT("整帧与线程"));
    }
    return View;
}

FPerformanceTimelineView PerformanceWorkbenchTimeline::LoadLatest(const FString& RunsRoot)
{
    const TArray<FString> Groups = ListRecentGroups(RunsRoot, 1);
    return Groups.IsEmpty() ? FPerformanceTimelineView() : LoadGroup(RunsRoot, Groups[0]);
}

TSharedRef<SWidget> PerformanceWorkbenchTimeline::BuildPanel(const FPerformanceTimelineView& View)
{
    TSharedRef<SHorizontalBox> TimeTicks = SNew(SHorizontalBox);
    constexpr int32 TimeDivisionCount = 4;
    for (int32 Index = 0; Index < TimeDivisionCount; ++Index)
    {
        const double Seconds = View.DurationSeconds * Index / TimeDivisionCount;
        TimeTicks->AddSlot().FillWidth(1.0f).HAlign(HAlign_Left)
        [
            SNew(STextBlock)
            .Text(FText::FromString(FString::Printf(TEXT("%.1f s"), Seconds)))
            .Font(FAppStyle::GetFontStyle("SmallText"))
            .ColorAndOpacity(FSlateColor::UseSubduedForeground())
        ];
    }
    TimeTicks->AddSlot().AutoWidth().HAlign(HAlign_Right)
    [
        SNew(STextBlock)
        .Text(FText::FromString(FString::Printf(TEXT("%.1f s"), View.DurationSeconds)))
        .Font(FAppStyle::GetFontStyle("SmallText"))
        .ColorAndOpacity(FSlateColor::UseSubduedForeground())
    ];

    TSharedRef<SHorizontalBox> SeriesLegend = SNew(SHorizontalBox);
    if (View.bHasComparison)
    {
        SeriesLegend->AddSlot().AutoWidth().VAlign(VAlign_Center)
        [
            SNew(SBox).WidthOverride(22.0f).HeightOverride(2.0f)
            [
                SNew(SBorder)
                .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
                .BorderBackgroundColor(FLinearColor(0.58f, 0.61f, 0.63f, 0.92f))
            ]
        ];
        SeriesLegend->AddSlot().AutoWidth().Padding(7, 0, 18, 0)
        [
            SNew(STextBlock).Text(FText::FromString(TEXT("改动前"))).Font(FAppStyle::GetFontStyle("SmallText"))
                .ColorAndOpacity(FSlateColor::UseSubduedForeground())
        ];
        SeriesLegend->AddSlot().AutoWidth().VAlign(VAlign_Center)
        [
            SNew(SBox).WidthOverride(22.0f).HeightOverride(2.0f)
            [
                SNew(SBorder)
                .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
                .BorderBackgroundColor(FLinearColor(0.34f, 0.88f, 0.78f, 0.95f))
            ]
        ];
        SeriesLegend->AddSlot().AutoWidth().Padding(7, 0, 0, 0)
        [
            SNew(STextBlock).Text(FText::FromString(TEXT("调整后（轨道色）"))).Font(FAppStyle::GetFontStyle("SmallText"))
                .ColorAndOpacity(FSlateColor::UseSubduedForeground())
        ];
    }
    else
    {
        SeriesLegend->AddSlot().AutoWidth()
        [
            SNew(STextBlock).Text(FText::FromString(TEXT("当前仅有一条有效采集"))).Font(FAppStyle::GetFontStyle("SmallText"))
                .ColorAndOpacity(FSlateColor::UseSubduedForeground())
        ];
    }

    TSharedRef<SVerticalBox> Tracks = SNew(SVerticalBox);
    FString CurrentSection;
    for (const FPerformanceTimelineTrack& Track : View.Tracks)
    {
        if (!Track.Section.IsEmpty() && Track.Section != CurrentSection)
        {
            const bool bFirstSection = CurrentSection.IsEmpty();
            CurrentSection = Track.Section;
            Tracks->AddSlot().AutoHeight().Padding(0, bFirstSection ? 2.0f : 12.0f, 0, 4)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
                [
                    SNew(SBox).WidthOverride(24.0f).HeightOverride(2.0f)
                    [
                        SNew(SBorder)
                        .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
                        .BorderBackgroundColor(FLinearColor(0.34f, 0.88f, 0.78f, 0.75f))
                    ]
                ]
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(CurrentSection))
                    .Font(FAppStyle::GetFontStyle("SmallText"))
                    .ColorAndOpacity(FLinearColor(0.66f, 0.84f, 0.82f))
                ]
            ];
        }
        const FString ScaleText = Track.Unit == TEXT("ms")
            ? FString::Printf(TEXT("P95 标尺 %.2f %s"), Track.ScaleMax / 1.15f, *Track.Unit)
            : FString::Printf(TEXT("P95 标尺 %.1f %s"), Track.ScaleMax / 1.15f, *Track.Unit);
        Tracks->AddSlot().AutoHeight().Padding(0, 2)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [
                SNew(SBox).WidthOverride(150.0f)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(Track.Label)).ColorAndOpacity(Track.Color)]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)[SNew(STextBlock).Text(FText::FromString(ScaleText)).Font(FAppStyle::GetFontStyle("SmallText")).ColorAndOpacity(FSlateColor::UseSubduedForeground())]
                ]
            ]
            + SHorizontalBox::Slot().FillWidth(1.0f)
            [
                SNew(SBox).HeightOverride(42.0f)
                [
                    SNew(SPerformanceSparkline).Samples(Track.Samples).ComparisonSamples(Track.ComparisonSamples).ScaleMax(Track.ScaleMax).Color(Track.Color)
                ]
            ]
        ];
    }

	TSharedRef<SVerticalBox> HotspotPanel = SNew(SVerticalBox);
	if (!View.Hotspots.IsEmpty())
	{
		HotspotPanel->AddSlot().AutoHeight().Padding(0, 0, 0, 6)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 9, 0)
			[
				SNew(SBox).WidthOverride(28.0f).HeightOverride(2.0f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
					.BorderBackgroundColor(FLinearColor(0.95f, 0.59f, 0.22f, 0.85f))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("当前批次热点")))
				.Font(FAppStyle::GetFontStyle("HeadingSmall"))
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("按均值排序 · 前 8 项")))
				.Font(FAppStyle::GetFontStyle("SmallText"))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
		];

		TSharedRef<SGridPanel> HotspotGrid = SNew(SGridPanel).FillColumn(0, 1.0f);
		const FSlateFontInfo HeaderFont = FAppStyle::GetFontStyle("SmallText");
		HotspotGrid->AddSlot(0, 0).Padding(4, 4)[SNew(STextBlock).Text(FText::FromString(TEXT("子系统"))).Font(HeaderFont).ColorAndOpacity(FSlateColor::UseSubduedForeground())];
		HotspotGrid->AddSlot(1, 0).Padding(12, 4)[SNew(STextBlock).Text(FText::FromString(View.bHasComparison ? TEXT("改动前 均值 / P95") : TEXT("均值 / P95"))).Font(HeaderFont).ColorAndOpacity(FSlateColor::UseSubduedForeground())];
		if (View.bHasComparison)
		{
			HotspotGrid->AddSlot(2, 0).Padding(12, 4)[SNew(STextBlock).Text(FText::FromString(TEXT("调整后 均值 / P95"))).Font(HeaderFont).ColorAndOpacity(FSlateColor::UseSubduedForeground())];
			HotspotGrid->AddSlot(3, 0).Padding(12, 4)[SNew(STextBlock).Text(FText::FromString(TEXT("均值变化"))).Font(HeaderFont).ColorAndOpacity(FSlateColor::UseSubduedForeground())];
		}

		int32 RowIndex = 1;
		for (const FPerformanceHotspotView& Hotspot : View.Hotspots)
		{
			HotspotGrid->AddSlot(0, RowIndex).Padding(4, 5)
			[
				SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%s · %s"), *Hotspot.Section, *Hotspot.Label)))
			];
			HotspotGrid->AddSlot(1, RowIndex).Padding(12, 5)
			[
				SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%.2f / %.2f ms"), Hotspot.BaselineMeanMs, Hotspot.BaselineP95Ms)))
			];
			if (View.bHasComparison && Hotspot.bHasComparison)
			{
				const double DeltaPercent = Hotspot.BaselineMeanMs > UE_SMALL_NUMBER
					? 100.0 * (Hotspot.ComparisonMeanMs - Hotspot.BaselineMeanMs) / Hotspot.BaselineMeanMs : 0.0;
				const FLinearColor DeltaColor = DeltaPercent <= -2.0 ? FLinearColor(0.20f, 0.82f, 0.46f)
					: DeltaPercent >= 2.0 ? FLinearColor(1.0f, 0.32f, 0.28f) : FLinearColor(0.95f, 0.72f, 0.25f);
				HotspotGrid->AddSlot(2, RowIndex).Padding(12, 5)
				[
					SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%.2f / %.2f ms"), Hotspot.ComparisonMeanMs, Hotspot.ComparisonP95Ms)))
				];
				HotspotGrid->AddSlot(3, RowIndex).Padding(12, 5)
				[
					SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%+.1f%%"), DeltaPercent))).ColorAndOpacity(DeltaColor)
				];
			}
			++RowIndex;
		}
		HotspotPanel->AddSlot().AutoHeight()[HotspotGrid];
	}

	TSharedRef<SVerticalBox> EffectBreakdownPanel = SNew(SVerticalBox);
	if (View.bHasEffectBreakdown)
	{
		const FString SnapshotText = View.bHasComparison
			? FString::Printf(TEXT("改动前：活动 %d / 注册 %d    调整后：活动 %d / 注册 %d"),
				View.BaselineActiveEffects, View.BaselineRegisteredEffects,
				View.ComparisonActiveEffects, View.ComparisonRegisteredEffects)
			: FString::Printf(TEXT("测量结束：活动 %d / 注册 %d"),
				View.BaselineActiveEffects, View.BaselineRegisteredEffects);

		auto BuildSourceList = [&View](const FString& Title,
			const TArray<FPerformanceEffectSourceView>& Sources) -> TSharedRef<SWidget>
		{
			TSharedRef<SVerticalBox> List = SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Title))
					.Font(FAppStyle::GetFontStyle("SmallText"))
					.ColorAndOpacity(FLinearColor(0.66f, 0.84f, 0.82f))
				];
			if (Sources.IsEmpty())
			{
				List->AddSlot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("本次采集没有活动组件明细")))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				];
			}
			for (const FPerformanceEffectSourceView& Source : Sources)
			{
				const FString CountText = View.bHasComparison
					? FString::Printf(TEXT("%d  →  %d"), Source.BaselineCount, Source.ComparisonCount)
					: FString::FromInt(Source.BaselineCount);
				const FString TimingText = Source.bHasDetailedTiming
					? (View.bHasComparison
						? FString::Printf(TEXT("CPU %.3f / %.3f  →  %.3f / %.3f ms"),
							Source.BaselineMeanMs, Source.BaselineP95Ms,
							Source.ComparisonMeanMs, Source.ComparisonP95Ms)
						: FString::Printf(TEXT("CPU %.3f / %.3f ms"), Source.BaselineMeanMs, Source.BaselineP95Ms))
					: FString();
				List->AddSlot().AutoHeight().Padding(0, 2)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.0f)
						[
							SNew(STextBlock).Text(FText::FromString(Source.Label))
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(12, 0, 0, 0)
						[
							SNew(STextBlock)
							.Text(FText::FromString(CountText))
							.Font(FAppStyle::GetFontStyle("SmallText"))
							.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 0)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TimingText))
						.Font(FAppStyle::GetFontStyle("SmallText"))
						.ColorAndOpacity(FLinearColor(0.88f, 0.67f, 0.36f))
						.Visibility(TimingText.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
					]
				];
			}
			return SNew(SBorder)
				.Padding(10.0f)
				.BorderImage(FAppStyle::GetBrush("Brushes.Header"))
				[
					List
				];
		};

		EffectBreakdownPanel->AddSlot().AutoHeight().Padding(0, 0, 0, 5)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 9, 0)
			[
				SNew(SBox).WidthOverride(28.0f).HeightOverride(2.0f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
					.BorderBackgroundColor(FLinearColor(0.88f, 0.40f, 0.72f, 0.85f))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Effects 负载来源")))
				.Font(FAppStyle::GetFontStyle("HeadingSmall"))
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(STextBlock)
				.Text(FText::FromString(SnapshotText))
				.Font(FAppStyle::GetFontStyle("SmallText"))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
		];
		EffectBreakdownPanel->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("UE 的 Effects CPU 分类覆盖 Niagara World Manager、System Simulation 与组件更新。下表是测量结束时的活动组件快照，用于定位下一次实验对象，不代表各资产的耗时占比。")))
			.Font(FAppStyle::GetFontStyle("SmallText"))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.AutoWrapText(true)
		];
		EffectBreakdownPanel->AddSlot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 5, 0)
			[
				BuildSourceList(TEXT("活动 Niagara 系统（组件数）"), View.EffectSystems)
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(5, 0, 0, 0)
			[
				BuildSourceList(TEXT("Owner 类型（组件数）"), View.EffectOwners)
			]
		];
	}

    return SNew(SBorder)
        .Padding(14.0f)
        .BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.0f)[SNew(STextBlock).Text(FText::FromString(
                    View.bHasComparison ? TEXT("A/B 采集时间对齐") : TEXT("单次采集时间轨道"))).Font(FAppStyle::GetFontStyle("HeadingSmall"))]
                + SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).Text(FText::FromString(View.Detail)).ColorAndOpacity(FSlateColor::UseSubduedForeground())]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 0)
            [
                SeriesLegend
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 9, 0, 0)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 9, 0)
                [
                    SNew(SBox).WidthOverride(3.0f).HeightOverride(16.0f)
                    [
                        SNew(SBorder)
                        .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
                        .BorderBackgroundColor(FLinearColor(0.95f, 0.59f, 0.22f, 0.9f))
                    ]
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("实验条件")))
                    .Font(FAppStyle::GetFontStyle("SmallText"))
                    .ColorAndOpacity(FLinearColor(0.95f, 0.72f, 0.38f))
                ]
                + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(View.Conditions))
                    .Font(FAppStyle::GetFontStyle("SmallText"))
                    .ColorAndOpacity(FSlateColor::UseSubduedForeground())
                    .AutoWrapText(true)
                ]
            ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 14, 0, 0)
			[
				HotspotPanel
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 14, 0, 0)
			[
				EffectBreakdownPanel
			]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 2)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [
                    SNew(SBox).WidthOverride(150.0f)
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(TEXT("共享采样时间")))
                        .Font(FAppStyle::GetFontStyle("SmallText"))
                        .ColorAndOpacity(FLinearColor(0.66f, 0.84f, 0.82f))
                    ]
                ]
                + SHorizontalBox::Slot().FillWidth(1.0f)
                [
                    TimeTicks
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)[Tracks]
        ];
}
