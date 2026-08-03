#include "ActorMetadataOverlayDemoActor.h"
#include "ActorMetadataOverlayDemoZone.h"

#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/DirectionalLight.h"
#include "EngineUtils.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMeshActor.h"
#include "GameplayTagsManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
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
        bool bRegionTemporarilyHidden = false;
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
        Snapshot.bRegionTemporarilyHidden = Snapshot.RegionCount == 1 && DemoRegion && DemoRegion->IsTemporarilyHiddenInEditor();
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
            Snapshot.bRegionTemporarilyHidden && Snapshot.FixtureCount == 7 && bAllLabelsMatched && Snapshot.bFixturesWithinRegion;

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
            return FinishOrTimeout(CurrentTest, FString::Printf(TEXT("region state is not ready: worldPartition=%s regionCount=%d loaded=%s hidden=%s fixtureCount=%d labels=%s withinBounds=%s"),
                Snapshot.bHasWorldPartition ? TEXT("true") : TEXT("false"),
                Snapshot.RegionCount,
                Snapshot.bRegionLoaded ? TEXT("true") : TEXT("false"),
                Snapshot.bRegionTemporarilyHidden ? TEXT("true") : TEXT("false"),
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
            CurrentTest->TestTrue(TEXT("demo region is temporarily hidden after reopen"), Snapshot.bRegionTemporarilyHidden);
            CurrentTest->TestEqual(TEXT("normal actor iteration finds seven fixtures after reopen"), Snapshot.FixtureCount, 7);
            for (const FString& Label : ActorMetadataOverlayDemoTests::RequiredFixtureLabels())
            {
                CurrentTest->TestTrue(FString::Printf(TEXT("reopened fixture label exists: %s"), *Label), Snapshot.FixtureLabels.Contains(Label));
            }
            CurrentTest->TestTrue(TEXT("all reopened fixtures are within the demo region"), Snapshot.bFixturesWithinRegion);
        }

        const FString UserConfig = ActorMetadataOverlayDemoTests::ReadProjectFile(TEXT("Config/DefaultEditorPerProjectUserSettings.ini"));
        UE_LOG(LogActorMetadataOverlayDemoTests, Display, TEXT("RegionReopenObservation stage=%s mapOpenCount=%d alternateMapCount=%d overviewReopenCount=%d regionCount=%d regionLoaded=%s regionHidden=%s fixtureCount=%d labels=%s displayMode=%s"),
            OverviewReopenCount == 0 ? TEXT("initial") : TEXT("reopen"),
            OverviewOpenCount,
            OverviewReopenCount,
            OverviewReopenCount,
            Snapshot.RegionCount,
            Snapshot.bRegionLoaded ? TEXT("true") : TEXT("false"),
            Snapshot.bRegionTemporarilyHidden ? TEXT("true") : TEXT("false"),
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
    TestTrue(TEXT("demo region is temporarily hidden in the editor viewport"), DemoRegion->IsTemporarilyHiddenInEditor());

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorMetadataSampleVisualEnvironmentTest,
    "ActorMetadataOverlay.Sample.VisualEnvironment",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FActorMetadataSampleVisualEnvironmentTest::RunTest(const FString& Parameters)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    TestNotNull(TEXT("editor world is loaded for the visual environment"), World);
    if (!World)
    {
        return false;
    }

    TestEqual(TEXT("visual environment exact overview map"), World->GetOutermost()->GetName(), FString(ActorMetadataOverlayDemoTests::DemoMapPackage));
    TestNotNull(TEXT("visual environment has World Partition"), World->GetWorldPartition());

    TSharedPtr<FJsonObject> Spec;
    FString Error;
    TestTrue(TEXT("visual environment spec loads"), ActorMetadataOverlayDemoTests::LoadSpec(Spec, Error));
    if (!Spec.IsValid())
    {
        AddError(Error);
        return false;
    }

    const TSharedPtr<FJsonObject> Environment = Spec->GetObjectField(TEXT("visualEnvironment"));
    const FString EnvironmentFolder = Environment->GetStringField(TEXT("folder"));
    TArray<AActor*> EnvironmentActors;
    auto FindNamedActor = [&World](const FString& ActorName, int32& OutCount) -> AActor*
    {
        AActor* FoundActor = nullptr;
        OutCount = 0;
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            if (It->GetFName() == FName(*ActorName))
            {
                FoundActor = *It;
                ++OutCount;
            }
        }
        return FoundActor;
    };
    const TArray<FString> EnvironmentKeys = {TEXT("floor"), TEXT("directionalLight"), TEXT("skyLight"), TEXT("skyAtmosphere")};
    const TArray<UClass*> EnvironmentClasses = {AStaticMeshActor::StaticClass(), ADirectionalLight::StaticClass(), ASkyLight::StaticClass(), ASkyAtmosphere::StaticClass()};
    for (int32 Index = 0; Index < EnvironmentKeys.Num(); ++Index)
    {
        const TSharedPtr<FJsonObject> ActorSpec = Environment->GetObjectField(EnvironmentKeys[Index]);
        const FString ActorName = ActorSpec->GetStringField(TEXT("actorName"));
        const FString ActorLabel = ActorSpec->GetStringField(TEXT("actorLabel"));
        int32 NameCount = 0;
        AActor* Actor = FindNamedActor(ActorName, NameCount);
        TestEqual(FString::Printf(TEXT("%s actor name is unique"), *EnvironmentKeys[Index]), NameCount, 1);
        TestNotNull(FString::Printf(TEXT("%s actor exists"), *EnvironmentKeys[Index]), Actor);
        if (!Actor)
        {
            continue;
        }
        EnvironmentActors.Add(Actor);
        TestEqual(FString::Printf(TEXT("%s actor label"), *EnvironmentKeys[Index]), Actor->GetActorLabel(), ActorLabel);
        TestEqual(FString::Printf(TEXT("%s actor folder"), *EnvironmentKeys[Index]), Actor->GetFolderPath().ToString(), EnvironmentFolder);
        TestTrue(FString::Printf(TEXT("%s uses the expected Engine actor class"), *EnvironmentKeys[Index]), Actor->IsA(EnvironmentClasses[Index]));
        TestFalse(FString::Printf(TEXT("%s is not a fixture actor"), *EnvironmentKeys[Index]), Actor->IsA<AActorMetadataOverlayDemoActor>() || Actor->IsA<AActorMetadataOverlayDemoZone>());
    }

    AActor* FloorActor = EnvironmentActors.Num() > 0 ? EnvironmentActors[0] : nullptr;
    AStaticMeshActor* Floor = Cast<AStaticMeshActor>(FloorActor);
    UStaticMeshComponent* FloorMesh = Floor ? Floor->GetStaticMeshComponent() : nullptr;
    TestNotNull(TEXT("floor static mesh component exists"), FloorMesh);
    const FString FloorMeshPath = FloorMesh && FloorMesh->GetStaticMesh() ? FloorMesh->GetStaticMesh()->GetPathName() : FString();
    TestEqual(TEXT("floor uses Engine Cube"), FloorMeshPath, FString(TEXT("/Engine/BasicShapes/Cube.Cube")));
    const FBox FloorBounds = Floor ? Floor->GetComponentsBoundingBox(true) : FBox(ForceInit);
    TestTrue(TEXT("floor top is at approximately Z=0"), FMath::IsNearlyZero(FloorBounds.Max.Z, 1.0f));

    AActor* SunActor = EnvironmentActors.Num() > 1 ? EnvironmentActors[1] : nullptr;
    ADirectionalLight* Sun = Cast<ADirectionalLight>(SunActor);
    UDirectionalLightComponent* SunComponent = Sun ? Sun->GetComponent() : nullptr;
    TestNotNull(TEXT("directional light component exists"), SunComponent);
    if (SunComponent)
    {
        TestTrue(TEXT("directional light affects the world"), SunComponent->bAffectsWorld);
        TestTrue(TEXT("directional light casts shadows"), SunComponent->CastShadows);
        TestTrue(TEXT("directional light is an atmosphere sun light"), SunComponent->bAtmosphereSunLight);
        TestTrue(TEXT("directional light intensity is positive"), SunComponent->Intensity > 0.0f);
    }

    AActor* SkyLightActor = EnvironmentActors.Num() > 2 ? EnvironmentActors[2] : nullptr;
    ASkyLight* SkyLight = Cast<ASkyLight>(SkyLightActor);
    USkyLightComponent* SkyLightComponent = SkyLight ? SkyLight->GetLightComponent() : nullptr;
    TestNotNull(TEXT("sky light component exists"), SkyLightComponent);
    if (SkyLightComponent)
    {
        TestTrue(TEXT("sky light affects the world"), SkyLightComponent->bAffectsWorld);
        TestTrue(TEXT("sky light real-time capture is enabled"), SkyLightComponent->IsRealTimeCaptureEnabled());
        TestTrue(TEXT("sky light intensity is positive"), SkyLightComponent->Intensity > 0.0f);
    }

    AActor* SkyAtmosphereActor = EnvironmentActors.Num() > 3 ? EnvironmentActors[3] : nullptr;
    ASkyAtmosphere* SkyAtmosphere = Cast<ASkyAtmosphere>(SkyAtmosphereActor);
    TestNotNull(TEXT("sky atmosphere component exists"), SkyAtmosphere ? SkyAtmosphere->GetComponent() : nullptr);

    TMap<FString, FString> ExpectedPointMeshes = {
        {TEXT("Loot Crate A"), TEXT("/Engine/BasicShapes/Cube.Cube")},
        {TEXT("Enemy Spawn North"), TEXT("/Engine/BasicShapes/Cylinder.Cylinder")},
        {TEXT("Quest Marker — Gate"), TEXT("/Engine/BasicShapes/Cone.Cone")},
        {TEXT("Navigation Point — East"), TEXT("/Engine/BasicShapes/Sphere.Sphere")},
        {TEXT("Far Distance Actor"), TEXT("/Engine/BasicShapes/Cube.Cube")},
        {TEXT("Filtered Debug Actor"), TEXT("/Engine/BasicShapes/Sphere.Sphere")}
    };
    TSet<FString> ShapePaths;
    int32 FixtureCount = 0;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;
        if (!Actor || (!Actor->IsA<AActorMetadataOverlayDemoActor>() && !Actor->IsA<AActorMetadataOverlayDemoZone>()))
        {
            continue;
        }

        ++FixtureCount;
        TestTrue(FString::Printf(TEXT("floor covers fixture %s on X/Y"), *Actor->GetActorLabel()),
            FloorBounds.Min.X <= Actor->GetActorLocation().X && FloorBounds.Max.X >= Actor->GetActorLocation().X &&
            FloorBounds.Min.Y <= Actor->GetActorLocation().Y && FloorBounds.Max.Y >= Actor->GetActorLocation().Y);
        if (AActorMetadataOverlayDemoActor* PointActor = Cast<AActorMetadataOverlayDemoActor>(Actor))
        {
            const FString* ExpectedMeshPath = ExpectedPointMeshes.Find(Actor->GetActorLabel());
            TestNotNull(FString::Printf(TEXT("visual mesh spec exists for %s"), *Actor->GetActorLabel()), ExpectedMeshPath);
            if (ExpectedMeshPath && PointActor->Mesh)
            {
                const FString ActualMeshPath = PointActor->Mesh->GetStaticMesh() ? PointActor->Mesh->GetStaticMesh()->GetPathName() : FString();
                TestEqual(FString::Printf(TEXT("visual mesh assignment for %s"), *Actor->GetActorLabel()), ActualMeshPath, *ExpectedMeshPath);
                TestTrue(FString::Printf(TEXT("visual mesh for %s is an Engine Basic Shape"), *Actor->GetActorLabel()), ActualMeshPath.StartsWith(TEXT("/Engine/BasicShapes/")));
                ShapePaths.Add(ActualMeshPath);
            }
            else
            {
                TestNotNull(FString::Printf(TEXT("static mesh component for %s"), *Actor->GetActorLabel()), PointActor->Mesh.Get());
            }
        }
        else if (AActorMetadataOverlayDemoZone* ZoneActor = Cast<AActorMetadataOverlayDemoZone>(Actor))
        {
            TestNotNull(TEXT("audio zone keeps its Box component"), ZoneActor->Box.Get());
        }
    }
    TestEqual(TEXT("normal actor iteration finds seven fixtures"), FixtureCount, 7);
    TestTrue(TEXT("at least four Basic Shape meshes are used"), ShapePaths.Num() >= 4);

    const FString UserConfig = ActorMetadataOverlayDemoTests::ReadProjectFile(TEXT("Config/DefaultEditorPerProjectUserSettings.ini"));
    TestTrue(TEXT("visual environment does not change Display Mode"), UserConfig.Contains(TEXT("DisplayMode=Selected")));
    TestFalse(TEXT("visual environment has no Python startup hook"), FPaths::FileExists(FPaths::ProjectDir() / TEXT("Content/Python/init_unreal.py")));
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
