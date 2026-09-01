using UnrealBuildTool;

public class PerformanceLab : ModuleRules
{
    public PerformanceLab(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new[]
        {
            "AIModule",
            "Core",
            "CoreUObject",
            "Engine",
            "LyraGame",
            "ModularGameplayActors",
            "NavigationSystem",
            "RenderCore",
			"RHI",
			"SignificanceManager"
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "Json",
            "JsonUtilities",
            "Niagara"
        });
    }
}
