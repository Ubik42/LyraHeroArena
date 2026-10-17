using UnrealBuildTool;

public class HeroArenaRuntime : ModuleRules
{
	public HeroArenaRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"LyraGame"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"GameplayAbilities",
				"GameplayTags",
				"GameplayTasks",
				"GameplayMessageRuntime",
				"ModularGameplay",
				"EnhancedInput",
				"InputCore",
				"Niagara",
				"SignificanceManager",
				"ShooterCoreRuntime",
				"CommonGame",
				"CommonUI",
				"Slate",
				"SlateCore",
				"UMG",
				"AIModule",
				"NavigationSystem"
			}
		);
	}
}
