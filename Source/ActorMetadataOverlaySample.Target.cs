using UnrealBuildTool;

public class ActorMetadataOverlaySampleTarget : TargetRules
{
    public ActorMetadataOverlaySampleTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V5;
        ExtraModuleNames.Add("ActorMetadataOverlaySample");
    }
}
