using UnrealBuildTool;

public class ActorMetadataOverlaySampleEditorTarget : TargetRules
{
    public ActorMetadataOverlaySampleEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        bForceCompileDevelopmentAutomationTests = true;
        ExtraModuleNames.Add("ActorMetadataOverlaySample");
    }
}
