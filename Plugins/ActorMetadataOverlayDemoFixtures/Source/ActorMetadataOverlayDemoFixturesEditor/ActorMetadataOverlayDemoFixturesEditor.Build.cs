using UnrealBuildTool;

public class ActorMetadataOverlayDemoFixturesEditor : ModuleRules
{
    public ActorMetadataOverlayDemoFixturesEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "GameplayTags",
            "ActorMetadataOverlayDemoFixtures"
        });
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "EditorSubsystem",
            "UnrealEd",
            "DataLayerEditor",
            "Json",
            "Slate",
            "SlateCore"
        });
    }
}
