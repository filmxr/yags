using UnrealBuildTool;
using System.IO;

public class YaGS : ModuleRules
{
    public YaGS(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "RHI",
            "RenderCore",
            "Engine",
            "DeveloperSettings",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "CoreUObject",
            "Renderer",
            "Projects",
            "GeometryCore",
        });

        PrivateIncludePaths.Add(Path.Combine(GetModuleDirectory("Renderer"), "Internal"));
    }
}
