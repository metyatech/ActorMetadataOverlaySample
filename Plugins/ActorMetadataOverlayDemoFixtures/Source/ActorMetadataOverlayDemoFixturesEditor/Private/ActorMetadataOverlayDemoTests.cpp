#include "ActorMetadataOverlayDemoActor.h"
#include "ActorMetadataOverlayDemoZone.h"

#include "Dom/JsonObject.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "GameplayTagsManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionHelpers.h"

namespace ActorMetadataOverlayDemoTests
{
    bool LoadSpec(TSharedPtr<FJsonObject>& OutSpec, FString& OutError)
    {
        FString Contents;
        const FString SpecPath = FPaths::ProjectDir() / TEXT("Demo/demo-spec.json");
        if (!FFileHelper::LoadFileToString(Contents, *SpecPath))
        {
            OutError = FString::Printf(TEXT("Could not read %s"), *SpecPath);
            return false;
        }

        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Contents);
        if (!FJsonSerializer::Deserialize(Reader, OutSpec) || !OutSpec.IsValid())
        {
            OutError = TEXT("demo-spec.json is not valid JSON");
            return false;
        }
        return true;
    }

    FString ReadProjectFile(const TCHAR* RelativePath)
    {
        FString Contents;
        FFileHelper::LoadFileToString(Contents, *(FPaths::ProjectDir() / RelativePath));
        return Contents;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorMetadataOverlaySampleSpecTest,
    "ActorMetadataOverlay.Sample.Spec",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FActorMetadataOverlaySampleSpecTest::RunTest(const FString& Parameters)
{
    TSharedPtr<FJsonObject> Spec;
    FString Error;
    TestTrue(TEXT("demo-spec.json loads"), ActorMetadataOverlayDemoTests::LoadSpec(Spec, Error));
    if (!Spec.IsValid())
    {
        AddError(Error);
        return false;
    }

    TestEqual(TEXT("schemaVersion"), static_cast<int32>(Spec->GetIntegerField(TEXT("schemaVersion"))), 1);
    TestEqual(TEXT("product"), Spec->GetStringField(TEXT("product")), FString(TEXT("Actor Metadata Overlay")));
    TestEqual(TEXT("actor count"), Spec->GetArrayField(TEXT("actors")).Num(), 7);
    TestEqual(TEXT("rule count"), Spec->GetArrayField(TEXT("rules")).Num(), 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorMetadataOverlaySampleMapTest,
    "ActorMetadataOverlay.Sample.Map",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FActorMetadataOverlaySampleMapTest::RunTest(const FString& Parameters)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    TestNotNull(TEXT("editor world is loaded"), World);
    if (!World)
    {
        return false;
    }

    TestTrue(TEXT("overview map is loaded"), World->GetMapName().Contains(TEXT("ActorMetadataOverlayOverview")));
    const TArray<FString> RequiredLabels = {
        TEXT("Loot Crate A"),
        TEXT("Enemy Spawn North"),
        TEXT("Quest Marker — Gate"),
        TEXT("Audio Zone — Courtyard"),
        TEXT("Navigation Point — East"),
        TEXT("Far Distance Actor"),
        TEXT("Filtered Debug Actor")
    };

    TSet<FString> FoundLabels;
    int32 FixtureCount = 0;
    auto RecordFixture = [&FoundLabels, &FixtureCount](AActor* Actor)
    {
        if (Actor && (Actor->IsA<AActorMetadataOverlayDemoActor>() || Actor->IsA<AActorMetadataOverlayDemoZone>()))
        {
            ++FixtureCount;
            FoundLabels.Add(Actor->GetActorLabel());
        }
    };

    if (UWorldPartition* WorldPartition = World->GetWorldPartition())
    {
        FWorldPartitionHelpers::FForEachActorWithLoadingParams Params;
        Params.bKeepReferences = true;
        Params.ActorClasses = { AActorMetadataOverlayDemoActor::StaticClass(), AActorMetadataOverlayDemoZone::StaticClass() };
        FWorldPartitionHelpers::ForEachActorWithLoading(WorldPartition,
            [&RecordFixture](const FWorldPartitionActorDescInstance* ActorDescInstance)
            {
                RecordFixture(ActorDescInstance ? ActorDescInstance->GetActor() : nullptr);
                return true;
            },
            Params);
    }
    else
    {
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            RecordFixture(*It);
        }
    }
    TestEqual(TEXT("fixture actor count"), FixtureCount, 7);
    for (const FString& Label : RequiredLabels)
    {
        TestTrue(FString::Printf(TEXT("required actor exists: %s"), *Label), FoundLabels.Contains(Label));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorMetadataOverlaySampleRulesTest,
    "ActorMetadataOverlay.Sample.Rules",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FActorMetadataOverlaySampleRulesTest::RunTest(const FString& Parameters)
{
    const FString Config = ActorMetadataOverlayDemoTests::ReadProjectFile(TEXT("Config/DefaultEditor.ini"));
    const int32 PointIndex = Config.Find(TEXT("RuleName=\"Point Actors\""));
    const int32 ZoneIndex = Config.Find(TEXT("RuleName=\"Zone Actors\""));
    TestTrue(TEXT("point rule exists"), PointIndex != INDEX_NONE);
    TestTrue(TEXT("zone rule exists"), ZoneIndex != INDEX_NONE);
    TestTrue(TEXT("point rule precedes zone rule"), PointIndex != INDEX_NONE && ZoneIndex != INDEX_NONE && PointIndex < ZoneIndex);
    TestTrue(TEXT("point class is the fixture actor"), Config.Contains(TEXT("ActorMetadataOverlayDemoFixtures.ActorMetadataOverlayDemoActor")));
    TestTrue(TEXT("zone class is the fixture zone"), Config.Contains(TEXT("ActorMetadataOverlayDemoFixtures.ActorMetadataOverlayDemoZone")));
    TestTrue(TEXT("filtered tag is excluded"), Config.Contains(TEXT("ExcludedActorTags=(\"OverlayHidden\")")));
    TestTrue(TEXT("zone bounding boxes are enabled"), Config.Contains(TEXT("RuleName=\"Zone Actors\"")) && Config.Contains(TEXT("bDrawBoundingBox=True")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorMetadataOverlaySampleGameplayTagsTest,
    "ActorMetadataOverlay.Sample.GameplayTags",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FActorMetadataOverlaySampleGameplayTagsTest::RunTest(const FString& Parameters)
{
    const TArray<FString> RequiredTags = {
        TEXT("Loot.Rare"),
        TEXT("World.Interactable"),
        TEXT("Spawn.Enemy"),
        TEXT("Quest.Main"),
        TEXT("Zone.Audio"),
        TEXT("Demo.Ready"),
        TEXT("Demo.Hidden")
    };
    for (const FString& TagName : RequiredTags)
    {
        const FGameplayTag Tag = UGameplayTagsManager::Get().RequestGameplayTag(FName(*TagName), false);
        TestTrue(FString::Printf(TEXT("Gameplay Tag exists: %s"), *TagName), Tag.IsValid());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorMetadataOverlaySampleSyncSafetyTest,
    "ActorMetadataOverlay.Sample.SyncSafety",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FActorMetadataOverlaySampleSyncSafetyTest::RunTest(const FString& Parameters)
{
    const FString Script = ActorMetadataOverlayDemoTests::ReadProjectFile(TEXT("Scripts/Sync-DemoToCaptureHost.ps1"));
    TestTrue(TEXT("sync defaults to dry run"), Script.Contains(TEXT("DryRun")) && Script.Contains(TEXT("Apply")));
    TestTrue(TEXT("paid plugin is protected"), Script.Contains(TEXT("Plugins/EditorActorTagDisplay")));
    TestTrue(TEXT("Deep Water paths are protected"), Script.Contains(TEXT("DeepWaterStation")));
    TestTrue(TEXT("allow-delete is explicit"), Script.Contains(TEXT("AllowDelete")));
    TestTrue(TEXT("sync is sample to capture only"), !Script.Contains(TEXT("CopyFromCapture")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorMetadataOverlaySampleDisplayModeTest,
    "ActorMetadataOverlay.Sample.DisplayMode",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FActorMetadataOverlaySampleDisplayModeTest::RunTest(const FString& Parameters)
{
    const FString UserConfig = ActorMetadataOverlayDemoTests::ReadProjectFile(TEXT("Config/DefaultEditorPerProjectUserSettings.ini"));
    TestTrue(TEXT("initial display mode matches product default"), UserConfig.Contains(TEXT("DisplayMode=Selected")));
    const FString BuildScript = ActorMetadataOverlayDemoTests::ReadProjectFile(TEXT("Scripts/Build-DemoMap.py"));
    const FString CaptureScript = ActorMetadataOverlayDemoTests::ReadProjectFile(TEXT("Scripts/Capture/Apply-DemoSpec.py"));
    TestTrue(TEXT("map generation does not set display mode"), !BuildScript.Contains(TEXT("DisplayMode")));
    TestTrue(TEXT("capture application does not set display mode"), !CaptureScript.Contains(TEXT("DisplayMode")));
    return true;
}
