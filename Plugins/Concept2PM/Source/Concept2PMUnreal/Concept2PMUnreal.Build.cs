using UnrealBuildTool;

public class Concept2PMUnreal : ModuleRules
{
	public Concept2PMUnreal(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateDependencyModuleNames.AddRange(new[] { "Core" });
	}
}
