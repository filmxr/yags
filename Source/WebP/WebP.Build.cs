using System.IO;
using UnrealBuildTool;

public class WebP : ModuleRules {
    protected string ThirdPartyPath { get => Path.Combine(ModuleDirectory, "../ThirdParty/libwebp"); }

    public WebP(ReadOnlyTargetRules Target) : base(Target) {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PrivateIncludePaths.Add(Path.Combine(ThirdPartyPath, "include"));
        string DllPath = Path.Combine(ThirdPartyPath, "bin");
        // PublicRuntimeLibraryPaths.Add(DllPath);
        string LibFilename = null;
        string DllFilename;
        if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows)) {
            LibFilename = "libwebpdecoder_dll.lib";
            DllFilename = "libwebpdecoder.dll";
        } else {
            throw new BuildException($"Platform is not supported: {Target.Platform}.");
        }
        if (LibFilename != null) {
            string LibPath = Path.Combine(ThirdPartyPath, "lib");
            string LibFilepath = Path.Combine(LibPath, LibFilename);
            PublicAdditionalLibraries.Add(LibFilepath);
        }
        string DllFilepath = Path.Combine(DllPath, DllFilename);
        if (!File.Exists(DllFilepath)) {
            throw new BuildException($"Required DLL missing: {DllFilepath}. Please ensure the file exists.");
        }
        string RuntimeDllFilepath = Path.Combine(PluginDirectory, "Binaries", Target.Platform.ToString(), DllFilename);
        RuntimeDependencies.Add(RuntimeDllFilepath, DllFilepath);
        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "FileUtilities",
        });
    }
}
