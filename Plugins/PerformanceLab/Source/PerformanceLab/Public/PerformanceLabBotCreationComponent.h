#pragma once

#include "GameModes/LyraBotCreationComponent.h"
#include "PerformanceLabBotCreationComponent.generated.h"

/**
 * Lyra bot spawner specialized for dense, deterministic performance runs.
 * It keeps Lyra's real controller/pawn/GAS initialization, but uses a fixed
 * grid instead of competing for the map's limited player starts.
 */
UCLASS(Blueprintable, ClassGroup = Performance)
class PERFORMANCELAB_API UPerformanceLabBotCreationComponent : public ULyraBotCreationComponent
{
    GENERATED_BODY()

protected:
    virtual void SpawnOneBot() override;

    UPROPERTY(EditDefaultsOnly, Category = "Performance Lab|Spawn")
    FVector GridOrigin = FVector(-2400.0, -1000.0, 200.0);

    UPROPERTY(EditDefaultsOnly, Category = "Performance Lab|Spawn", meta = (ClampMin = "1"))
    int32 GridColumns = 20;

    UPROPERTY(EditDefaultsOnly, Category = "Performance Lab|Spawn", meta = (ClampMin = "80.0"))
    double GridSpacingX = 120.0;

    UPROPERTY(EditDefaultsOnly, Category = "Performance Lab|Spawn", meta = (ClampMin = "80.0"))
    double GridSpacingY = 280.0;
};
