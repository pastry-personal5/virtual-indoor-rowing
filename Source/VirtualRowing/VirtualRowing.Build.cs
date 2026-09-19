using UnrealBuildTool;
using System;
using System.IO;

public class VirtualRowing : ModuleRules
{
	public VirtualRowing(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "Slate", "SlateCore", "UMG" });
		if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicFrameworks.AddRange(new[] { "CoreBluetooth", "Foundation", "Security", "CoreFoundation" });
			// The archive's Swift CryptoKit shim autolinks libswiftCore/libswiftFoundation and
			// friends. They live in the OS (dyld shared cache); this only lets the linker find
			// their stubs, resolved under the SDK root, so nothing outside the OS is loaded.
			PublicSystemLibraryPaths.Add("/usr/lib/swift");

			// The engine's shared PCH is compiled without ARC, so a module compiled with
			// ARC enabled cannot reuse it; this module is small enough that skipping PCHs
			// entirely on Mac costs nothing worth trading ARC correctness for.
			bEnableObjCAutomaticReferenceCounting = true;
			PCHUsage = PCHUsageMode.NoPCHs;
		}

		// Phase 1 Milestone 5: the engine-independent modules (RowingCore, RowingDevice,
		// LocalData, WorkoutRuntime, the simulator, and static protobuf/abseil) are built by
		// CMake, not UBT, and arrive as one prebuilt Release arm64 archive from
		// `make native-app`. Their headers are includable as-is; nothing here compiles
		// their sources.
		string RepoRoot = Target.ProjectFile.Directory.FullName;
		string NativeArchive = Path.Combine(RepoRoot, "Build", "native-app", "lib", "libVirRowingApp.a");
		if (!File.Exists(NativeArchive))
		{
			throw new BuildException("Missing " + NativeArchive + ". Run `make native-app` (or `make unreal-smoke`, which does) before building the VirtualRowing module.");
		}
		PublicIncludePaths.AddRange(new[]
		{
			Path.Combine(RepoRoot, "Source", "RowingCore", "Public"),
			Path.Combine(RepoRoot, "Source", "RowingDevice", "Public"),
			Path.Combine(RepoRoot, "Source", "LocalData", "Public"),
			Path.Combine(RepoRoot, "Source", "WorkoutRuntime", "Public"),
			Path.Combine(RepoRoot, "Source", "RowingSim", "include"),
			// Phase 1 Milestone 7: the real-device path. Both are pure C++ headers; Apple and
			// Swift types stay inside the prebuilt archive.
			Path.Combine(RepoRoot, "Plugins", "Concept2PM", "Source", "Concept2PMCore", "Public"),
			Path.Combine(RepoRoot, "Plugins", "Concept2PM", "Source", "Concept2PMMac", "Public"),
			Path.Combine(RepoRoot, "Source", "LocalDataMac", "Public"),
		});
		PublicAdditionalLibraries.Add(NativeArchive);
		// UBT does not treat a prebuilt archive as a link input, so a rebuilt archive alone
		// would leave a stale module binary. Baking its identity into the compile
		// definitions makes any archive change recompile and relink this module.
		FileInfo NativeArchiveInfo = new FileInfo(NativeArchive);
		PrivateDefinitions.Add("VIR_NATIVE_ARCHIVE_STAMP=" + NativeArchiveInfo.LastWriteTimeUtc.Ticks + "LL");
		PublicSystemLibraries.Add("sqlite3");
		// The prebuilt libraries are compiled with C++ exceptions (journal errors are caught
		// inside WorkoutRuntime) and RTTI; keep this module compatible with both.
		bEnableExceptions = true;

		string Revision = Environment.GetEnvironmentVariable("VIR_SOURCE_REVISION") ?? "unknown";
		PublicDefinitions.Add($"VIR_SOURCE_REVISION=TEXT(\"{Revision.Replace("\\\"", "")}\")");
		PublicDefinitions.Add("VIR_TOOLCHAIN_FINGERPRINT=TEXT(\"ue-5.8.2;xcode-26.1.1;macos-26.6.2;arm64\")");
	}
}
