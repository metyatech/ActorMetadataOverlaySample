using UnrealBuildTool;

public class ActorMetadataOverlaySample : ModuleRules
{
    public ActorMetadataOverlaySample(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine"
        });
    }
}
