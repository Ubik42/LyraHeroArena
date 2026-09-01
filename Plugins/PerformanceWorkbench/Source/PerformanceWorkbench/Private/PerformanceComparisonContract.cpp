#include "PerformanceComparisonContract.h"

#include "Dom/JsonObject.h"

namespace
{
    struct FContractField
    {
        FString Label;
        FString Value;
    };

    FString ReadString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
    {
        FString Value;
        return Object.IsValid() && Object->TryGetStringField(Field, Value) ? Value : FString();
    }

    double ReadNumber(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
    {
        double Value = 0.0;
        return Object.IsValid() && Object->TryGetNumberField(Field, Value) ? Value : 0.0;
    }

    bool ReadBool(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
    {
        bool Value = false;
        return Object.IsValid() && Object->TryGetBoolField(Field, Value) && Value;
    }

    TArray<FContractField> BuildFields(const TSharedPtr<FJsonObject>& Summary)
    {
        const TSharedPtr<FJsonObject>* Environment = nullptr;
        const TSharedPtr<FJsonObject> EmptyEnvironment;
        const TSharedPtr<FJsonObject>& ResolvedEnvironment = Summary.IsValid()
            && Summary->TryGetObjectField(TEXT("environment"), Environment) && Environment && Environment->IsValid()
                ? *Environment : EmptyEnvironment;

        TArray<FContractField> Fields;
		Fields.Reserve(22);
        Fields.Add({ TEXT("实验预设"), ReadString(Summary, TEXT("presetId")) });
        Fields.Add({ TEXT("测量维度"), ReadString(Summary, TEXT("dimension")) });
        Fields.Add({ TEXT("源码版本"), ReadString(Summary, TEXT("sourceRevision")) });
        Fields.Add({ TEXT("引擎版本"), ReadString(Summary, TEXT("engineVersion")) });
        Fields.Add({ TEXT("地图"), ReadString(Summary, TEXT("map")) });
        Fields.Add({ TEXT("进程角色"), ReadString(Summary, TEXT("processRole")) });
        Fields.Add({ TEXT("网络模式"), ReadString(Summary, TEXT("netMode")) });
        Fields.Add({ TEXT("RHI"), ReadString(Summary, TEXT("rhiName")) });
        Fields.Add({ TEXT("随机种子"), FString::Printf(TEXT("%.0f"), ReadNumber(Summary, TEXT("seed"))) });
        Fields.Add({ TEXT("AI 数量"), FString::Printf(TEXT("%.0f"), ReadNumber(Summary, TEXT("targetAgentCount"))) });
        Fields.Add({ TEXT("复制对象数量"), FString::Printf(TEXT("%.0f"), ReadNumber(Summary, TEXT("targetReplicatedFixtureCount"))) });
        Fields.Add({ TEXT("刚体数量"), FString::Printf(TEXT("%.0f"), ReadNumber(Summary, TEXT("targetPhysicsBodyCount"))) });
		Fields.Add({ TEXT("Niagara 状态槽"), FString::Printf(TEXT("%.0f"), ReadNumber(Summary, TEXT("targetNiagaraStatusSlotCount"))) });
		Fields.Add({ TEXT("装备最远绘制距离"), FString::Printf(TEXT("%.0f"), ReadNumber(Summary, TEXT("characterEquipmentMaxDrawDistance"))) });
        Fields.Add({ TEXT("事件频率"), FString::Printf(TEXT("%.3f"), ReadNumber(Summary, TEXT("niagaraBurstsPerSecond"))) });
        Fields.Add({ TEXT("预热时长"), FString::Printf(TEXT("%.3f"), ReadNumber(Summary, TEXT("warmupSeconds"))) });
        Fields.Add({ TEXT("采样时长"), FString::Printf(TEXT("%.3f"), ReadNumber(Summary, TEXT("measurementSeconds"))) });
        Fields.Add({ TEXT("连接数量"), FString::Printf(TEXT("%.0f"), ReadNumber(Summary, TEXT("netConnectionCount"))) });
        Fields.Add({ TEXT("RHI 模式"), ReadBool(Summary, TEXT("normalRHI")) ? TEXT("正常") : TEXT("Null") });
        Fields.Add({ TEXT("固定镜头"), ReadBool(Summary, TEXT("initialViewLocked")) ? TEXT("是") : TEXT("否") });
        Fields.Add({ TEXT("分辨率"), FString::Printf(TEXT("%.0f×%.0f"),
            ReadNumber(ResolvedEnvironment, TEXT("resolutionX")), ReadNumber(ResolvedEnvironment, TEXT("resolutionY"))) });
        Fields.Add({ TEXT("窗口模式"), ReadBool(ResolvedEnvironment, TEXT("windowed")) ? TEXT("窗口") : TEXT("全屏") });
        return Fields;
    }
}

FString PerformanceComparisonContract::BuildKey(const TSharedPtr<FJsonObject>& Summary)
{
    const TArray<FContractField> Fields = BuildFields(Summary);
    TArray<FString> Parts;
    Parts.Reserve(Fields.Num());
    for (const FContractField& Field : Fields)
    {
        Parts.Add(Field.Value);
    }
    return FString::Join(Parts, TEXT("|"));
}

TArray<FString> PerformanceComparisonContract::DescribeDifferences(
    const TSharedPtr<FJsonObject>& A, const TSharedPtr<FJsonObject>& B)
{
    const TArray<FContractField> AFields = BuildFields(A);
    const TArray<FContractField> BFields = BuildFields(B);
    TArray<FString> Differences;
    const int32 FieldCount = FMath::Min(AFields.Num(), BFields.Num());
    for (int32 Index = 0; Index < FieldCount; ++Index)
    {
        if (AFields[Index].Value != BFields[Index].Value)
        {
            Differences.Add(AFields[Index].Label);
        }
    }
    return Differences;
}
