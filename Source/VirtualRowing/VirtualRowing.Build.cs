using UnrealBuildTool;
using System;

public class VirtualRowing : ModuleRules
{
	public VirtualRowing(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
		if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicFrameworks.AddRange(new[] { "CoreBluetooth", "Foundation" });

			// The engine's shared PCH is compiled without ARC, so a module compiled with
			// ARC enabled cannot reuse it; this module is small enough that skipping PCHs
			// entirely on Mac costs nothing worth trading ARC correctness for.
			bEnableObjCAutomaticReferenceCounting = true;
			PCHUsage = PCHUsageMode.NoPCHs;
		}

		string Revision = Environment.GetEnvironmentVariable("VIR_SOURCE_REVISION") ?? "unknown";
		PublicDefinitions.Add($"VIR_SOURCE_REVISION=TEXT(\"{Revision.Replace("\\\"", "")}\")");
		PublicDefinitions.Add("VIR_TOOLCHAIN_FINGERPRINT=TEXT(\"ue-5.8.2;xcode-26.1.1;macos-26.6.2;arm64\")");
	}
}
