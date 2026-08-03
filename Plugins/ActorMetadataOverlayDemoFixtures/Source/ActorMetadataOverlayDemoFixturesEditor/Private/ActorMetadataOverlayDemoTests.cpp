#include "ActorMetadataOverlayDemoActor.h"
#include "ActorMetadataOverlayDemoZone.h"

#include "Dom/JsonObject.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "GameplayTagsManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Tests/AutomationEditorCommon.h"
#include "LocationVolume.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogActorMetadataOverlayDemoTests, Log, All);

namespace ActorMetadataOverlayDemoTests
{
    const TCHAR* DemoMapPackage = TEXT("/Game/ActorMetadataOverlayDemo/Maps/ActorMetadataOverlayOverview");
    const FName DemoRegionName(TEXT("AMO_DemoRegion"));

    const TArray<FString>& RequiredFixtureLabels()
    {
        static const TArray<FString> Labels = {
            TEXT("Loot Crate A"),
            TEXT("Enemy Spawn North"),
            TEXT("Quest Marker — Gate"),
            TEXT("Audio Zone — Courtyard"),
            TEXT("Navigation Point — East"),
            TEXT("Far Distance Actor"),
            TEXT("Filtered Debug Actor")
        };
        return Labels;
    }

    struct FDemoRegionSnapshot
    {
        bool bHasWorldPartition = false;
        int32 RegionCount = 0;
        bool bRegionLoaded = false;
        int32 FixtureCount = 0;
        TSet<FString> FixtureLabels;
        bool bFixturesWithinRegion = false;
    };

    FDemoRegionSnapshot CaptureDemoRegionSnapshot(UWorld* World)
    {
        FDemoRegionSnapshot Snapshot;
        if (!World)
        {
            return Snapshot;
        }

        Snapshot.bHasWorldPartition = World->GetWorldPartition() != nullptr;

        ALocationVolume* DemoRegion = nullptr;
        for (TActorIterator<ALocationVolume> It(World); It; ++It)
        {
            if (It->GetFName() == DemoRegionName)
            {
                ++Snapshot.RegionCount;
                if (Snapshot.RegionCount == 1)
                {
                    DemoRegion = *It;
                }
            }
        }

        Snapshot.bRegionLoaded = Snapshot.RegionCount == 1 && DemoRegion && DemoRegion->IsLoaded();
        Snapshot.bFixturesWithinRegion = Snapshot.RegionCount == 1 && DemoRegion;
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            AActor* Actor = *It;
            if (!Actor || (!Actor->IsA<AActorMetadataOverlayDemoActor>() && !Actor->IsA<AActorMetadataOverlayDemoZone>()))
            {
                continue;
            }

            ++Snapshot.FixtureCount;
            Snapshot.FixtureLabels.Add(Actor->GetActorLabel());
            if (DemoRegion && !DemoRegion->EncompassesPoint(Actor->GetActorLocation()))
            {
                Snapshot.bFixturesWithinRegion = false;
            }
        }

        return Snapshot;
    }

    FString JoinFixtureLabels(const TSet<FString>& Labels)
    {
        TArray<FString> SortedLabels = Labels.Array();
        SortedLabels.Sort();
        return FString::Join(SortedLabels, TEXT(";"));
    }

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

class FCreateTransientEditorMapCommand final : public IAutomationLatentCommand
{
public:
    virtual bool Update() override
    {
        FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
        if (!GEditor)
        {
            if (CurrentTest)
            {
                CurrentTest->AddError(TEXT("GEditor is unavailable while creating the transient alternate map."));
            }
            return true;
        }

        GEditor->CreateNewMapForEditing(false, false);
        UWorld* World = GEditor->GetEditorWorldContext().World();
        const bool bTransientMapReady = World && World->GetOutermost()->GetName() != ActorMetadataOverlayDemoTests::DemoMapPackage;
        if (CurrentTest)
        {
            CurrentTest->TestTrue(TEXT("transient alternate map is created without a save prompt"), bTransientMapReady);
        }
        if (bTransientMapReady)
        {
            UE_LOG(LogActorMetadataOverlayDemoTests, Display, TEXT("RegionReopenTransition alternateMap=%s"), *World->GetOutermost()->GetName());
        }
        return true;
    }
};

class FWaitForDemoRegionStateCommand final : public IAutomationLatentCommand
{
public:
    explicit FWaitForDemoRegionStateCommand(int32 InOverviewOpenCount, int32 InOverviewReopenCount)
        : OverviewOpenCount(InOverviewOpenCount)
        , OverviewReopenCount(InOverviewReopenCount)
    {
    }

    virtual bool Update() override
    {
        if (StartTimeSeconds <= 0.0)
        {
            StartTimeSeconds = FPlatformTime::Seconds();
        }

        FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
        UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
        if (!World || World->WorldType != EWorldType::Editor || World->GetOutermost()->GetName() != ActorMetadataOverlayDemoTests::DemoMapPackage)
        {
            return FinishOrTimeout(CurrentTest, TEXT("the exact Overview Map is not ready"));
        }

        const ActorMetadataOverlayDemoTests::FDemoRegionSnapshot Snapshot = ActorMetadataOverlayDemoTests::CaptureDemoRegionSnapshot(World);
        const bool bAllLabelsMatched = Snapshot.FixtureLabels.Num() == ActorMetadataOverlayDemoTests::RequiredFixtureLabels().Num() &&
            ActorMetadataOverlayDemoTests::RequiredFixtureLabels().ContainsByPredicate([&Snapshot](const FString& Label)
            {
                return Snapshot.FixtureLabels.Contains(Label);
            });
        const bool bReady = Snapshot.bHasWorldPartition && Snapshot.RegionCount == 1 && Snapshot.bRegionLoaded &&
            Snapshot.FixtureCount == 7 && bAllLabelsMatched && Snapshot.bFixturesWithinRegion;

        if (!bReady)
        {
            if (Snapshot.RegionCount > 1)
            {
                if (CurrentTest)
                {
                    CurrentTest->AddError(FString::Printf(TEXT("RegionReopen found %d AMO_DemoRegion actors; expected exactly one."), Snapshot.RegionCount));
                }
                return true;
            }
            return FinishOrTimeout(CurrentTest, FString::Printf(TEXT("region state is not ready: worldPartition=%s regionCount=%d loaded=%s fixtureCount=%d labels=%s withinBounds=%s"),
                Snapshot.bHasWorldPartition ? TEXT("true") : TEXT("false"),
                Snapshot.RegionCount,
                Snapshot.bRegionLoaded ? TEXT("true") : TEXT("false"),
                Snapshot.FixtureCount,
                *ActorMetadataOverlayDemoTests::JoinFixtureLabels(Snapshot.FixtureLabels),
                Snapshot.bFixturesWithinRegion ? TEXT("true") : TEXT("false")));
        }

        if (CurrentTest)
        {
            CurrentTest->TestEqual(TEXT("exact Overview Map after reopen"), World->GetOutermost()->GetName(), FString(ActorMetadataOverlayDemoTests::DemoMapPackage));
            CurrentTest->TestTrue(TEXT("World Partition is present after reopen"), Snapshot.bHasWorldPartition);
            CurrentTest->TestEqual(TEXT("exactly one demo region after reopen"), Snapshot.RegionCount, 1);
            CurrentTest->TestTrue(TEXT("demo region is loaded after reopen"), Snapshot.bRegionLoaded);
            CurrentTest->TestEqual(TEXT("normal actor iteration finds seven fixtures after reopen"), Snapshot.FixtureCount, 7);
            for (const FString& Label : ActorMetadataOverlayDemoTests::RequiredFixtureLabels())
            {
                CurrentTest->TestTrue(FString::Printf(TEXT("reopened fixture label exists: %s"), *Label), Snapshot.FixtureLabels.Contains(Label));
            }
            CurrentTest->TestTrue(TEXT("all reopened fixtures are within the demo region"), Snapshot.bFixturesWithinRegion);
        }

        const FString UserConfig = ActorMetadataOverlayDemoTests::ReadProjectFile(TEXT("Config/DefaultEditorPerProjectUserSettings.ini"));
        UE_LOG(LogActorMetadataOverlayDemoTests, Display, TEXT("RegionReopenObservation stage=%s mapOpenCount=%d alternateMapCount=%d overviewReopenCount=%d regionCount=%d regionLoaded=%s fixtureCount=%d labels=%s displayMode=%s"),
            OverviewReopenCount == 0 ? TEXT("initial") : TEXT("reopen"),
            OverviewOpenCount,
            OverviewReopenCount,
            OverviewReopenCount,
            Snapshot.RegionCount,
            Snapshot.bRegionLoaded ? TEXT("true") : TEXT("false"),
            Snapshot.FixtureCount,
            *ActorMetadataOverlayDemoTests::JoinFixtureLabels(Snapshot.FixtureLabels),
            UserConfig.Contains(TEXT("DisplayMode=Selected")) ? TEXT("Selected") : TEXT("Unexpected"));
        return true;
    }

private:
    bool FinishOrTimeout(FAutomationTestBase* CurrentTest, const FString& Reason)
    {
        if (FPlatformTime::Seconds() - StartTimeSeconds < TimeoutSeconds)
        {
            return false;
        }

        if (CurrentTest)
        {
            CurrentTest->AddError(FString::Printf(TEXT("Timed out waiting for RegionReopen state after %.1f seconds: %s"), TimeoutSeconds, *Reason));
        }
        return true;
    }

    int32 OverviewOpenCount = 0;
    int32 OverviewReopenCount = 0;
    double StartTimeSeconds = 0.0;
    static constexpr double TimeoutSeconds = 30.0;
};

class FVerifyRegionReopenFinalStateCommand final : public IAutomationLatentCommand
{
public:
    virtual bool Update() override
    {
        FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
        UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
        if (CurrentTest)
        {
            CurrentTest->TestNotNull(TEXT("Overview Map remains open after RegionReopen"), World);
            CurrentTest->TestFalse(TEXT("sample startup Python is absent after RegionReopen"), FPaths::FileExists(FPaths::ProjectDir() / TEXT("Content/Python/init_unreal.py")));
        }

        const FString UserConfig = ActorMetadataOverlayDemoTests::ReadProjectFile(TEXT("Config/DefaultEditorPerProjectUserSettings.ini"));
        const bool bDisplayModeSelected = UserConfig.Contains(TEXT("DisplayMode=Selected"));
        if (CurrentTest)
        {
            CurrentTest->TestTrue(TEXT("Display Mode remains Selected after RegionReopen"), bDisplayModeSelected);
        }
        UE_LOG(LogActorMetadataOverlayDemoTests, Display, TEXT("RegionReopenCompleted displayModeBefore=Selected displayModeAfter=%s pythonStartup=false finalMap=%s"),
            bDisplayModeSelected ? TEXT("Selected") : TEXT("Unexpected"),
            World ? *World->GetOutermost()->GetName() : TEXT("<none>"));
        return true;
    }
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorMetadataSampleSpecTest,
    "ActorMetadataOverlay.Sample.Spec",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FActorMetadataSampleSpecTest::RunTest(const FString& Parameters)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorMetadataSampleMapTest,
    "ActorMetadataOverlay.Sample.Map",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FActorMetadataSampleMapTest::RunTest(const FString& Parameters)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorMetadataSampleRulesTest,
    "ActorMetadataOverlay.Sample.Rules",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FActorMetadataSampleRulesTest::RunTest(const FString& Parameters)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorMetadataSampleGameplayTagsTest,
    "ActorMetadataOverlay.Sample.GameplayTags",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FActorMetadataSampleGameplayTagsTest::RunTest(const FString& Parameters)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorMetadataSampleSyncSafetyTest,
    "ActorMetadataOverlay.Sample.SyncSafety",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FActorMetadataSampleSyncSafetyTest::RunTest(const FString& Parameters)
{
    const FString Script = ActorMetadataOverlayDemoTests::ReadProjectFile(TEXT("Scripts/Sync-DemoToCaptureHost.ps1"));
    TestTrue(TEXT("sync defaults to dry run"), Script.Contains(TEXT("DryRun")) && Script.Contains(TEXT("Apply")));
    TestTrue(TEXT("paid plugin is protected"), Script.Contains(TEXT("Plugins/EditorActorTagDisplay")));
    TestTrue(TEXT("Deep Water paths are protected"), Script.Contains(TEXT("DeepWaterStation")));
    TestTrue(TEXT("allow-delete is explicit"), Script.Contains(TEXT("AllowDelete")));
    TestTrue(TEXT("sync is sample to capture only"), !Script.Contains(TEXT("CopyFromCapture")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorMetadataSampleDisplayModeTest,
    "ActorMetadataOverlay.Sample.DisplayMode",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FActorMetadataSampleDisplayModeTest::RunTest(const FString& Parameters)
{
    const FString UserConfig = ActorMetadataOverlayDemoTests::ReadProjectFile(TEXT("Config/DefaultEditorPerProjectUserSettings.ini"));
    TestTrue(TEXT("initial display mode matches product default"), UserConfig.Contains(TEXT("DisplayMode=Selected")));
    const FString BuildScript = ActorMetadataOverlayDemoTests::ReadProjectFile(TEXT("Scripts/Build-DemoMap.py"));
    const FString CaptureScript = ActorMetadataOverlayDemoTests::ReadProjectFile(TEXT("Scripts/Capture/Apply-DemoSpec.py"));
    TestTrue(TEXT("map generation does not set display mode"), !BuildScript.Contains(TEXT("DisplayMode")));
    TestTrue(TEXT("capture application does not set display mode"), !CaptureScript.Contains(TEXT("DisplayMode")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorMetadataSampleInitialRegionTest,
    "ActorMetadataOverlay.Sample.InitialRegion",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FActorMetadataSampleInitialRegionTest::RunTest(const FString& Parameters)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    TestNotNull(TEXT("editor world is loaded"), World);
    if (!World)
    {
        return false;
    }

    TestEqual(TEXT("exact overview map is loaded"), World->GetOutermost()->GetName(), FString(TEXT("/Game/ActorMetadataOverlayDemo/Maps/ActorMetadataOverlayOverview")));

    ALocationVolume* DemoRegion = nullptr;
    int32 RegionCount = 0;
    for (TActorIterator<ALocationVolume> It(World); It; ++It)
    {
        if (It->GetFName() == FName(TEXT("AMO_DemoRegion")))
        {
            DemoRegion = *It;
            ++RegionCount;
        }
    }

    TestEqual(TEXT("exactly one demo region exists"), RegionCount, 1);
    TestNotNull(TEXT("demo region is present"), DemoRegion);
    if (!DemoRegion)
    {
        return false;
    }

    TestEqual(TEXT("demo region label"), DemoRegion->GetActorLabel(), FString(TEXT("Actor Metadata Overlay Demo Region")));
    TestTrue(TEXT("demo region is loaded by the editor startup path"), DemoRegion->IsLoaded());

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
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;
        if (Actor && (Actor->IsA<AActorMetadataOverlayDemoActor>() || Actor->IsA<AActorMetadataOverlayDemoZone>()))
        {
            ++FixtureCount;
            FoundLabels.Add(Actor->GetActorLabel());
            TestTrue(FString::Printf(TEXT("demo region contains %s"), *Actor->GetActorLabel()), DemoRegion->EncompassesPoint(Actor->GetActorLocation()));
        }
    }
    TestEqual(TEXT("normal actor iteration finds all fixtures"), FixtureCount, 7);
    for (const FString& Label : RequiredLabels)
    {
        TestTrue(FString::Printf(TEXT("initially loaded actor exists: %s"), *Label), FoundLabels.Contains(Label));
    }

    const FString UserConfig = ActorMetadataOverlayDemoTests::ReadProjectFile(TEXT("Config/DefaultEditorPerProjectUserSettings.ini"));
    TestTrue(TEXT("initial display mode remains Selected"), UserConfig.Contains(TEXT("DisplayMode=Selected")));
    TestFalse(TEXT("sample startup Python is absent"), FPaths::FileExists(FPaths::ProjectDir() / TEXT("Content/Python/init_unreal.py")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorMetadataSampleRegionReopenTest,
    "ActorMetadataOverlay.Sample.RegionReopen",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FActorMetadataSampleRegionReopenTest::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForDemoRegionStateCommand(/* OverviewOpenCount = */ 1, /* OverviewReopenCount = */ 0));

    for (int32 ReopenIndex = 1; ReopenIndex <= 2; ++ReopenIndex)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCreateTransientEditorMapCommand());
        ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(FString(ActorMetadataOverlayDemoTests::DemoMapPackage)));
        ADD_LATENT_AUTOMATION_COMMAND(FWaitForDemoRegionStateCommand(/* OverviewOpenCount = */ ReopenIndex + 1, ReopenIndex));
    }

    ADD_LATENT_AUTOMATION_COMMAND(FVerifyRegionReopenFinalStateCommand());
    return true;
}
