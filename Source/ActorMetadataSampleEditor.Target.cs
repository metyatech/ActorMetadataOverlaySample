using UnrealBuildTool;

public class ActorMetadataSampleEditorTarget : TargetRules
{
    public ActorMetadataSampleEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        bForceCompileDevelopmentAutomationTests = true;
        ExtraModuleNames.Add("ActorMetadataSample");
    }
}
