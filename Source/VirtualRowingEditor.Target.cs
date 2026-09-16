using UnrealBuildTool;

public class VirtualRowingEditorTarget : TargetRules
{
	public VirtualRowingEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("VirtualRowing");
	}
}
