using System;
using System.IO;
using UnrealBuildTool;

public class Audio2Blendshapes : ModuleRules
{
    public Audio2Blendshapes(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.Add("SignalProcessing");

        string sdk = Path.GetFullPath(Environment.GetEnvironmentVariable("AUDIO2FACE_SDK_ROOT")
            ?? "D:/Code/Audio2Face-3D-SDK");
        string build = Path.Combine(sdk, "_build", "release", "audio2x-sdk");
        string library = Path.Combine(build, "lib", "audio2x.lib");
        if (!File.Exists(library))
        {
            throw new BuildException("Audio2Blendshapes requires the built NVIDIA SDK. Set AUDIO2FACE_SDK_ROOT. See README.md.");
        }
        PrivateDefinitions.Add("A2B_SDK_ROOT=\"" + sdk.Replace('\\', '/') + "\"");
        PublicSystemIncludePaths.Add(Path.Combine(build, "include"));
        PublicAdditionalLibraries.Add(library);
        PublicDelayLoadDLLs.Add("audio2x.dll");
        PublicSystemLibraries.AddRange(new[] { "mfplat.lib", "mfreadwrite.lib", "mfuuid.lib", "ole32.lib" });
    }
}
