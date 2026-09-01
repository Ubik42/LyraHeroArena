#pragma once

#include "CoreMinimal.h"

class FJsonObject;

namespace PerformanceComparisonContract
{
    FString BuildKey(const TSharedPtr<FJsonObject>& Summary);
    TArray<FString> DescribeDifferences(const TSharedPtr<FJsonObject>& A, const TSharedPtr<FJsonObject>& B);
}
