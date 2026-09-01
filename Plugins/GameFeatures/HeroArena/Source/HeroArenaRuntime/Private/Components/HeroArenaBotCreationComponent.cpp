#include "Components/HeroArenaBotCreationComponent.h"

#include "AIController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/ConstructorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HeroArenaBotCreationComponent)

UHeroArenaBotCreationComponent::UHeroArenaBotCreationComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NumBotsToCreate = FParse::Param(FCommandLine::Get(), TEXT("HeroArenaDisableBots")) ? 0 : 9;
	ConstructorHelpers::FClassFinder<AAIController> ControlPointController(
		TEXT("/ShooterCore/ControlPoint/AI/B_AI_Controller_LyraShooter_ControlPoint"));
	if (ControlPointController.Succeeded())
	{
		BotControllerClass = ControlPointController.Class;
	}
	RandomBotNames = {
		TEXT("前卫"), TEXT("火线"), TEXT("侧翼"), TEXT("医疗"), TEXT("工兵"),
		TEXT("岔路"), TEXT("钟摆"), TEXT("引线"), TEXT("回声")
	};
}

UHeroArenaEliminationBotCreationComponent::UHeroArenaEliminationBotCreationComponent(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ConstructorHelpers::FClassFinder<AAIController> CombatController(
		TEXT("/ShooterCore/Bot/B_AI_Controller_LyraShooter"));
	if (CombatController.Succeeded())
	{
		BotControllerClass = CombatController.Class;
	}
}
