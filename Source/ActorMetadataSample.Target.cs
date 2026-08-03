using UnrealBuildTool;

public class ActorMetadataSampleTarget : TargetRules
{
    public ActorMetadataSampleTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V5;
        ExtraModuleNames.Add("ActorMetadataSample");
    }
}
