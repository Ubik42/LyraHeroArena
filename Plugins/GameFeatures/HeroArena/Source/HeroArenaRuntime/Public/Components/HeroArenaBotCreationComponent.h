#pragma once

#include "GameModes/LyraBotCreationComponent.h"

#include "HeroArenaBotCreationComponent.generated.h"

/** Hero Arena control-point population: one local player plus nine objective-aware Lyra bots. */
UCLASS()
class HEROARENARUNTIME_API UHeroArenaBotCreationComponent : public ULyraBotCreationComponent
{
	GENERATED_BODY()

public:
	UHeroArenaBotCreationComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};

/** Hero Arena elimination population using Lyra's standard combat controller. */
UCLASS()
class HEROARENARUNTIME_API UHeroArenaEliminationBotCreationComponent : public UHeroArenaBotCreationComponent
{
	GENERATED_BODY()

public:
	UHeroArenaEliminationBotCreationComponent(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
