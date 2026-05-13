using UnrealBuildTool;

public class YaGSEditor : ModuleRules
{
    public YaGSEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "UnrealEd",
            "Engine",
            "AssetDefinition",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "WebP",
            "YaGS",
            "SlateCore",
            "Slate",
            "InputCore",
            "FileUtilities",
            "Json",
        });
    }
}
