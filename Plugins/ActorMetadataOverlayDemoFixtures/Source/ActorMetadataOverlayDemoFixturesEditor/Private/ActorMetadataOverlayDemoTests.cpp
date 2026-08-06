#include "ActorMetadataOverlayDemoActor.h"
#include "ActorMetadataOverlayDemoZone.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/TextRenderComponent.h"
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
#include "Materials/Material.h"
#include "Materials/MaterialExpressionParameter.h"
#include "Materials/MaterialInstanceConstant.h"
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

    AActor* FindActorByName(UWorld* World, const FString& ActorName, int32& OutCount)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorMetadataSamplePresentationTest,
    "ActorMetadataOverlay.Sample.Presentation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FActorMetadataSamplePresentationTest::RunTest(const FString& Parameters)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    TestNotNull(TEXT("presentation test editor world"), World);
    if (!World)
    {
        return false;
    }

    TestEqual(TEXT("presentation exact overview map"), World->GetOutermost()->GetName(), FString(ActorMetadataOverlayDemoTests::DemoMapPackage));
    TestNotNull(TEXT("presentation World Partition exists"), World->GetWorldPartition());

    TSharedPtr<FJsonObject> Spec;
    FString Error;
    TestTrue(TEXT("presentation spec loads"), ActorMetadataOverlayDemoTests::LoadSpec(Spec, Error));
    if (!Spec.IsValid())
    {
        AddError(Error);
        return false;
    }

    const TSharedPtr<FJsonObject> Presentation = Spec->GetObjectField(TEXT("presentation"));
    TestEqual(TEXT("presentation style"), Presentation->GetStringField(TEXT("style")), FString(TEXT("polished-technical-showcase")));
    const TSharedPtr<FJsonObject> Materials = Spec->GetObjectField(TEXT("materials"));
    const FString MaterialRoot = Materials->GetStringField(TEXT("root"));
    const TSharedPtr<FJsonObject> MasterSpec = Materials->GetObjectField(TEXT("master"));
    const FString MasterName = MasterSpec->GetStringField(TEXT("name"));
    const FString MasterObjectPath = MasterSpec->GetStringField(TEXT("path")) + TEXT(".") + MasterName;
    UMaterial* Master = Cast<UMaterial>(StaticLoadObject(UMaterial::StaticClass(), nullptr, *MasterObjectPath));
    TestNotNull(TEXT("exactly one Sample master material loads"), Master);
    if (!Master)
    {
        return false;
    }

    TSet<FName> MaterialParameterNames;
    for (UMaterialExpression* Expression : Master->GetExpressions())
    {
        if (const UMaterialExpressionParameter* Parameter = Cast<UMaterialExpressionParameter>(Expression))
        {
            MaterialParameterNames.Add(Parameter->GetParameterName());
        }
    }
    for (const TCHAR* ParameterName : {TEXT("BaseColor"), TEXT("Roughness"), TEXT("Metallic"), TEXT("EmissiveColor"), TEXT("EmissiveStrength")})
    {
        TestTrue(FString::Printf(TEXT("master parameter exists: %s"), ParameterName), MaterialParameterNames.Contains(FName(ParameterName)));
    }

    const TArray<TSharedPtr<FJsonValue>>& InstanceValues = Materials->GetArrayField(TEXT("instances"));
    TestEqual(TEXT("required Material Instance count"), InstanceValues.Num(), 13);
    TSet<FString> InstancePaths;
    for (const TSharedPtr<FJsonValue>& InstanceValue : InstanceValues)
    {
        const TSharedPtr<FJsonObject> InstanceSpec = InstanceValue->AsObject();
        const FString InstanceName = InstanceSpec->GetStringField(TEXT("name"));
        const FString InstancePath = InstanceSpec->GetStringField(TEXT("path"));
        TestTrue(FString::Printf(TEXT("Material Instance name is unique: %s"), *InstanceName), !InstancePaths.Contains(InstanceName));
        InstancePaths.Add(InstanceName);
        UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(StaticLoadObject(
            UMaterialInstanceConstant::StaticClass(), nullptr, *(InstancePath + TEXT(".") + InstanceName)));
        TestNotNull(FString::Printf(TEXT("Material Instance loads: %s"), *InstanceName), Instance);
        if (Instance)
        {
            TestTrue(FString::Printf(TEXT("Material Instance parent: %s"), *InstanceName), Instance->Parent.Get() == Master);
        }
    }

    const TSharedPtr<FJsonObject> Assignments = Materials->GetObjectField(TEXT("fixtureAssignments"));
    TSet<FString> FixtureMaterialPaths;
    TSet<FString> FixtureShapePaths;
    int32 AssignedPointFixtureCount = 0;
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Assignments->Values)
    {
        const FString ActorName = Pair.Key;
        const FString MaterialName = Pair.Value->AsString();
        int32 NameCount = 0;
        AActor* Actor = ActorMetadataOverlayDemoTests::FindActorByName(World, ActorName, NameCount);
        TestEqual(FString::Printf(TEXT("fixture assignment actor is unique: %s"), *ActorName), NameCount, 1);
        TestNotNull(FString::Printf(TEXT("fixture assignment actor exists: %s"), *ActorName), Actor);
        if (!Actor || !Actor->IsA<AActorMetadataOverlayDemoActor>())
        {
            continue;
        }

        ++AssignedPointFixtureCount;
        AActorMetadataOverlayDemoActor* PointActor = CastChecked<AActorMetadataOverlayDemoActor>(Actor);
        TestNotNull(FString::Printf(TEXT("fixture mesh exists: %s"), *ActorName), PointActor->Mesh.Get());
        if (PointActor->Mesh)
        {
            const FString MeshPath = PointActor->Mesh->GetStaticMesh() ? PointActor->Mesh->GetStaticMesh()->GetPathName() : FString();
            const FString MaterialPath = PointActor->Mesh->GetMaterial(0) ? PointActor->Mesh->GetMaterial(0)->GetPathName() : FString();
            TestTrue(FString::Printf(TEXT("fixture mesh is an Engine Basic Shape: %s"), *ActorName), MeshPath.StartsWith(TEXT("/Engine/BasicShapes/")));
            const FString ExpectedMaterialPath = MaterialRoot + TEXT("/") + MaterialName;
            TestTrue(FString::Printf(TEXT("fixture material assignment: %s"), *ActorName),
                MaterialPath == ExpectedMaterialPath || MaterialPath.StartsWith(ExpectedMaterialPath + TEXT(".")));
            TestTrue(FString::Printf(TEXT("fixture has no Default Material: %s"), *ActorName), MaterialPath.StartsWith(MaterialRoot + TEXT("/MI_AMO_")));
            FixtureShapePaths.Add(MeshPath);
            FixtureMaterialPaths.Add(MaterialPath);
        }
    }
    TestEqual(TEXT("six point fixtures have Material assignments"), AssignedPointFixtureCount, 6);
    TestTrue(TEXT("at least four Engine Basic Shapes are used"), FixtureShapePaths.Num() >= 4);
    TestTrue(TEXT("at least six fixture materials are distinct"), FixtureMaterialPaths.Num() >= 6);

    const TArray<TSharedPtr<FJsonValue>>& StationValues = Spec->GetArrayField(TEXT("stations"));
    TestEqual(TEXT("seven station platforms"), StationValues.Num(), 7);
    TSet<FString> PresentationNames;
    auto CheckStaticPresentationActor = [this, World, &PresentationNames, &MaterialRoot](const TSharedPtr<FJsonObject>& ActorSpec, const TCHAR* Description)
    {
        const FString ActorName = ActorSpec->GetStringField(TEXT("actorName"));
        int32 NameCount = 0;
        AActor* Actor = ActorMetadataOverlayDemoTests::FindActorByName(World, ActorName, NameCount);
        TestEqual(FString::Printf(TEXT("%s name unique: %s"), Description, *ActorName), NameCount, 1);
        TestNotNull(FString::Printf(TEXT("%s exists: %s"), Description, *ActorName), Actor);
        TestTrue(FString::Printf(TEXT("%s is not a fixture class: %s"), Description, *ActorName), !Actor ||
            (!Actor->IsA<AActorMetadataOverlayDemoActor>() && !Actor->IsA<AActorMetadataOverlayDemoZone>()));
        TestTrue(FString::Printf(TEXT("%s has a unique presentation name: %s"), Description, *ActorName), !PresentationNames.Contains(ActorName));
        PresentationNames.Add(ActorName);
        if (AStaticMeshActor* StaticMeshActor = Cast<AStaticMeshActor>(Actor))
        {
            UStaticMeshComponent* Mesh = StaticMeshActor->GetStaticMeshComponent();
            TestEqual(FString::Printf(TEXT("%s mesh is Engine Basic Shape: %s"), Description, *ActorName),
                Mesh && Mesh->GetStaticMesh() ? Mesh->GetStaticMesh()->GetPathName() : FString(), ActorSpec->GetStringField(TEXT("mesh")));
            const FString MaterialPath = Mesh && Mesh->GetMaterial(0) ? Mesh->GetMaterial(0)->GetPathName() : FString();
            const FString ExpectedMaterialPath = MaterialRoot + TEXT("/") + ActorSpec->GetStringField(TEXT("material"));
            TestTrue(FString::Printf(TEXT("%s material: %s"), Description, *ActorName),
                MaterialPath == ExpectedMaterialPath || MaterialPath.StartsWith(ExpectedMaterialPath + TEXT(".")));
        }
    };

    for (const TSharedPtr<FJsonValue>& StationValue : StationValues)
    {
        CheckStaticPresentationActor(StationValue->AsObject(), TEXT("station platform"));
    }

    const TSharedPtr<FJsonObject> Plaza = Presentation->GetObjectField(TEXT("plaza"));
    CheckStaticPresentationActor(Plaza->GetObjectField(TEXT("floor")), TEXT("plaza floor"));
    CheckStaticPresentationActor(Plaza->GetObjectField(TEXT("backdrop")), TEXT("title board"));
    for (const TSharedPtr<FJsonValue>& BorderValue : Plaza->GetArrayField(TEXT("borders")))
    {
        CheckStaticPresentationActor(BorderValue->AsObject(), TEXT("plaza border"));
    }
    for (const TSharedPtr<FJsonValue>& AccentValue : Plaza->GetArrayField(TEXT("accentLines")))
    {
        CheckStaticPresentationActor(AccentValue->AsObject(), TEXT("plaza accent"));
    }

    const TSharedPtr<FJsonObject> Lane = Spec->GetObjectField(TEXT("distanceLane"));
    CheckStaticPresentationActor(Lane->GetObjectField(TEXT("floor")), TEXT("distance lane floor"));
    CheckStaticPresentationActor(Lane->GetObjectField(TEXT("boundary")), TEXT("100m boundary"));
    const TArray<TSharedPtr<FJsonValue>>& MarkerValues = Lane->GetArrayField(TEXT("markers"));
    TestEqual(TEXT("distance marker count"), MarkerValues.Num(), 3);
    for (const TSharedPtr<FJsonValue>& MarkerValue : MarkerValues)
    {
        CheckStaticPresentationActor(MarkerValue->AsObject(), TEXT("distance marker"));
    }
    for (const TSharedPtr<FJsonValue>& BorderValue : Lane->GetArrayField(TEXT("borders")))
    {
        CheckStaticPresentationActor(BorderValue->AsObject(), TEXT("distance lane border"));
    }

    auto ReadVector = [](const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName)
    {
        const TArray<TSharedPtr<FJsonValue>>& Values = Object->GetArrayField(FieldName);
        return FVector(Values[0]->AsNumber(), Values[1]->AsNumber(), Values[2]->AsNumber());
    };
    auto ReadRotator = [](const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName)
    {
        const TArray<TSharedPtr<FJsonValue>>& Values = Object->GetArrayField(FieldName);
        return FRotator(Values[0]->AsNumber(), Values[1]->AsNumber(), Values[2]->AsNumber());
    };

    const TArray<TSharedPtr<FJsonValue>>& DistanceLabelValues = Lane->GetArrayField(TEXT("labels"));
    TestEqual(TEXT("distance label count"), DistanceLabelValues.Num(), 3);
    TSet<FString> DistanceLabelNames;
    for (const TSharedPtr<FJsonValue>& TextValue : DistanceLabelValues)
    {
        const TSharedPtr<FJsonObject> TextSpec = TextValue->AsObject();
        const FString ActorName = TextSpec->GetStringField(TEXT("actorName"));
        int32 NameCount = 0;
        AActor* Actor = ActorMetadataOverlayDemoTests::FindActorByName(World, ActorName, NameCount);
        TestEqual(FString::Printf(TEXT("distance label actor unique: %s"), *ActorName), NameCount, 1);
        TestNotNull(FString::Printf(TEXT("distance label actor exists: %s"), *ActorName), Actor);
        TestTrue(FString::Printf(TEXT("distance label name is expected: %s"), *ActorName),
            ActorName == TEXT("AMO_Environment_Text_50m") || ActorName == TEXT("AMO_Environment_Text_100m") ||
            ActorName == TEXT("AMO_Environment_Text_200m"));
        TestTrue(FString::Printf(TEXT("distance label name is unique: %s"), *ActorName), !DistanceLabelNames.Contains(ActorName));
        DistanceLabelNames.Add(ActorName);
        UTextRenderComponent* TextComponent = Actor ? Actor->FindComponentByClass<UTextRenderComponent>() : nullptr;
        TestNotNull(FString::Printf(TEXT("distance label component exists: %s"), *ActorName), TextComponent);
        if (Actor && TextComponent)
        {
            TestEqual(FString::Printf(TEXT("distance label text: %s"), *ActorName), TextComponent->Text.ToString(), TextSpec->GetStringField(TEXT("text")));
            TestTrue(FString::Printf(TEXT("distance label location: %s"), *ActorName),
                Actor->GetActorLocation().Equals(ReadVector(TextSpec, TEXT("location")), 0.1));
            TestTrue(FString::Printf(TEXT("distance label rotation: %s"), *ActorName),
                Actor->GetActorRotation().Equals(ReadRotator(TextSpec, TEXT("rotation")), 0.1));
            TestTrue(FString::Printf(TEXT("distance label size: %s"), *ActorName),
                FMath::IsNearlyEqual(TextComponent->WorldSize, TextSpec->GetNumberField(TEXT("textSize")), 0.1));
        }
    }

    const TSharedPtr<FJsonObject> BoundarySpec = Lane->GetObjectField(TEXT("boundary"));
    int32 BoundaryNameCount = 0;
    AActor* BoundaryActor = ActorMetadataOverlayDemoTests::FindActorByName(
        World, BoundarySpec->GetStringField(TEXT("actorName")), BoundaryNameCount);
    TestEqual(TEXT("100m boundary actor unique"), BoundaryNameCount, 1);
    TestNotNull(TEXT("100m boundary actor exists"), BoundaryActor);
    if (BoundaryActor)
    {
        const FVector ExpectedBoundaryLocation = ReadVector(BoundarySpec, TEXT("location"));
        const FVector ExpectedBoundaryScale = ReadVector(BoundarySpec, TEXT("scale"));
        TestTrue(TEXT("100m boundary location matches spec"), BoundaryActor->GetActorLocation().Equals(ExpectedBoundaryLocation, 0.1));
        TestTrue(TEXT("100m boundary scale matches spec"), BoundaryActor->GetActorScale3D().Equals(ExpectedBoundaryScale, 0.01));
        TestTrue(TEXT("100m boundary has a non-color height landmark"),
            ExpectedBoundaryLocation.Z >= 250.0 && ExpectedBoundaryScale.Z >= 0.2);
    }

    const TSharedPtr<FJsonObject> Signage = Spec->GetObjectField(TEXT("signage"));
    TSet<FString> SignageText;
    TSet<FString> SignageNames;
    const double BoardFrontY = Plaza->GetObjectField(TEXT("backdrop"))->GetArrayField(TEXT("location"))[1]->AsNumber() - 25.0;
    for (const TSharedPtr<FJsonValue>& TextValue : Signage->GetArrayField(TEXT("texts")))
    {
        const TSharedPtr<FJsonObject> TextSpec = TextValue->AsObject();
        const FString ActorName = TextSpec->GetStringField(TEXT("actorName"));
        int32 NameCount = 0;
        AActor* Actor = ActorMetadataOverlayDemoTests::FindActorByName(World, ActorName, NameCount);
        TestEqual(FString::Printf(TEXT("signage actor unique: %s"), *ActorName), NameCount, 1);
        TestNotNull(FString::Printf(TEXT("signage actor exists: %s"), *ActorName), Actor);
        TestTrue(FString::Printf(TEXT("signage actor name is unique: %s"), *ActorName), !SignageNames.Contains(ActorName));
        SignageNames.Add(ActorName);
        UTextRenderComponent* TextComponent = Actor ? Actor->FindComponentByClass<UTextRenderComponent>() : nullptr;
        TestNotNull(FString::Printf(TEXT("signage text component exists: %s"), *ActorName), TextComponent);
        if (Actor && TextComponent)
        {
            SignageText.Add(TextComponent->Text.ToString());
            TestEqual(FString::Printf(TEXT("signage text matches spec: %s"), *ActorName), TextComponent->Text.ToString(), TextSpec->GetStringField(TEXT("text")));
            TestTrue(FString::Printf(TEXT("signage location matches spec: %s"), *ActorName),
                Actor->GetActorLocation().Equals(ReadVector(TextSpec, TEXT("location")), 0.1));
            TestTrue(FString::Printf(TEXT("signage rotation matches spec: %s"), *ActorName),
                Actor->GetActorRotation().Equals(ReadRotator(TextSpec, TEXT("rotation")), 0.1));
            TestTrue(FString::Printf(TEXT("signage scale matches spec: %s"), *ActorName),
                Actor->GetActorScale3D().Equals(ReadVector(TextSpec, TEXT("scale")), 0.01));
            TestTrue(FString::Printf(TEXT("signage size matches spec: %s"), *ActorName),
                FMath::IsNearlyEqual(TextComponent->WorldSize, TextSpec->GetNumberField(TEXT("textSize")), 0.1));
            TestTrue(FString::Printf(TEXT("signage remains in front of board: %s"), *ActorName),
                Actor->GetActorLocation().Y < BoardFrontY && Actor->GetActorLocation().Y >= BoardFrontY - 160.0);
            const TArray<TSharedPtr<FJsonValue>>& ColorValues = TextSpec->GetArrayField(TEXT("textColor"));
            const FColor ExpectedColor(
                ColorValues[0]->AsNumber(), ColorValues[1]->AsNumber(), ColorValues[2]->AsNumber(), ColorValues[3]->AsNumber());
            TestEqual(FString::Printf(TEXT("signage color matches spec: %s"), *ActorName), TextComponent->TextRenderColor, ExpectedColor);
        }
    }
    TestEqual(TEXT("signage actor count"), SignageNames.Num(), 4);
    TestTrue(TEXT("title board text is present"), SignageText.Contains(TEXT("ACTOR METADATA OVERLAY")));
    TestTrue(TEXT("show mode text is present"), SignageText.Contains(TEXT("Viewport Show  /  Actor Metadata Overlay")));

    const TSharedPtr<FJsonObject> CameraSpec = Spec->GetObjectField(TEXT("overviewCamera"));
    const double OverviewCameraFov = CameraSpec->GetNumberField(TEXT("fov"));
    TestTrue(TEXT("overview camera FOV is finite and exactly 50"),
        FMath::IsFinite(OverviewCameraFov) && FMath::IsNearlyEqual(OverviewCameraFov, 50.0, UE_DOUBLE_SMALL_NUMBER));
    TestTrue(TEXT("overview camera FOV is within the supported initial viewport range"),
        OverviewCameraFov >= 35.0 && OverviewCameraFov <= 80.0);
    int32 CameraNameCount = 0;
    AActor* CameraActor = ActorMetadataOverlayDemoTests::FindActorByName(World, CameraSpec->GetStringField(TEXT("actorName")), CameraNameCount);
    TestEqual(TEXT("overview camera is unique"), CameraNameCount, 1);
    TestNotNull(TEXT("overview camera exists"), CameraActor);
    ACameraActor* OverviewCamera = Cast<ACameraActor>(CameraActor);
    TestNotNull(TEXT("overview camera is a CameraActor"), OverviewCamera);
    if (OverviewCamera)
    {
        TestTrue(TEXT("overview camera location matches spec"),
            OverviewCamera->GetActorLocation().Equals(ReadVector(CameraSpec, TEXT("location")), 0.1));
        TestTrue(TEXT("overview camera rotation matches spec"),
            OverviewCamera->GetActorRotation().Equals(ReadRotator(CameraSpec, TEXT("rotation")), 0.1));
        TestTrue(TEXT("overview camera FOV matches spec"),
            FMath::IsNearlyEqual(OverviewCamera->GetCameraComponent()->FieldOfView, CameraSpec->GetNumberField(TEXT("fov")), 0.1));
    }

    const TSharedPtr<FJsonObject> CaptureViews = Spec->GetObjectField(TEXT("captureViews"));
    for (const TCHAR* CaptureViewName : {TEXT("overviewSelected"), TEXT("overviewClean"), TEXT("distanceLane")})
    {
        const TSharedPtr<FJsonObject> CaptureView = CaptureViews->GetObjectField(CaptureViewName);
        TestEqual(FString::Printf(TEXT("capture view location shape: %s"), CaptureViewName),
            CaptureView->GetArrayField(TEXT("location")).Num(), 3);
        TestEqual(FString::Printf(TEXT("capture view rotation shape: %s"), CaptureViewName),
            CaptureView->GetArrayField(TEXT("rotation")).Num(), 3);
        TestTrue(FString::Printf(TEXT("capture view FOV is sane: %s"), CaptureViewName),
            CaptureView->GetNumberField(TEXT("fov")) >= 35.0 && CaptureView->GetNumberField(TEXT("fov")) <= 80.0);
    }

    TestEqual(TEXT("presentation actor count remains 30"),
        PresentationNames.Num() + DistanceLabelNames.Num() + SignageNames.Num() + CameraNameCount, 30);

    ALocationVolume* DemoRegion = nullptr;
    int32 RegionCount = 0;
    for (TActorIterator<ALocationVolume> It(World); It; ++It)
    {
        if (It->GetFName() == ActorMetadataOverlayDemoTests::DemoRegionName)
        {
            ++RegionCount;
            DemoRegion = *It;
        }
    }
    TestEqual(TEXT("presentation region count"), RegionCount, 1);
    TestTrue(TEXT("presentation region loaded"), DemoRegion && DemoRegion->IsLoaded());
    TestTrue(TEXT("presentation region hidden"), DemoRegion && DemoRegion->IsTemporarilyHiddenInEditor());
    TestFalse(TEXT("presentation test does not require startup Python"), FPaths::FileExists(FPaths::ProjectDir() / TEXT("Content/Python/init_unreal.py")));

    FString CaptureModuleSource;
    const FString CaptureModulePath = FPaths::ProjectDir() /
        TEXT("Plugins/ActorMetadataOverlayDemoFixtures/Source/ActorMetadataOverlayDemoFixturesEditor/Private/ActorMetadataOverlayDemoFixturesEditor.cpp");
    TestTrue(TEXT("real overlay capture implementation is readable"),
        FFileHelper::LoadFileToString(CaptureModuleSource, *CaptureModulePath));
    if (!CaptureModuleSource.IsEmpty())
    {
        TestTrue(TEXT("real overlay capture command remains registered"),
            CaptureModuleSource.Contains(TEXT("ActorMetadataOverlayDemo.CaptureSlateDebugCanvas")));
        TestTrue(TEXT("real overlay capture redraws the active viewport synchronously"),
            CaptureModuleSource.Contains(TEXT("ActiveViewport->Draw(false)")));
        TestTrue(TEXT("real overlay capture uses the Slate debug-canvas layer"),
            CaptureModuleSource.Contains(TEXT("FSlateApplication::Get().TakeScreenshot")));
        TestTrue(TEXT("real overlay capture is confined to ignored review evidence"),
            CaptureModuleSource.Contains(TEXT(".verification/user-review")));
        TestTrue(TEXT("initial viewport reads overviewCamera.fov as a number"),
            CaptureModuleSource.Contains(TEXT("TryGetNumberField(TEXT(\"fov\")")));
        TestTrue(TEXT("initial viewport rejects a non-finite FOV"),
            CaptureModuleSource.Contains(TEXT("FMath::IsFinite(Fov)")));
        TestTrue(TEXT("initial viewport enforces the 35 to 80 degree FOV range"),
            CaptureModuleSource.Contains(TEXT("Fov < 35.0")) && CaptureModuleSource.Contains(TEXT("Fov > 80.0")));
        TestTrue(TEXT("initial viewport targets a Perspective Level Editor viewport"),
            CaptureModuleSource.Contains(TEXT("FLevelEditorViewportClient")) &&
            CaptureModuleSource.Contains(TEXT("IsPerspective()")));
        TestFalse(TEXT("initial viewport does not use SetViewLocationForOrbiting"),
            CaptureModuleSource.Contains(TEXT("SetViewLocationForOrbiting")));
        TestFalse(TEXT("initial viewport does not use SetLookAtLocation"),
            CaptureModuleSource.Contains(TEXT("SetLookAtLocation")));
        TestFalse(TEXT("initial viewport does not use ToggleOrbitCamera"),
            CaptureModuleSource.Contains(TEXT("ToggleOrbitCamera")));
        TestFalse(TEXT("initial viewport does not contain OrbitCamera"),
            CaptureModuleSource.Contains(TEXT("OrbitCamera")));
        TestTrue(TEXT("initial viewport applies location and rotation directly"),
            CaptureModuleSource.Contains(TEXT("SetViewLocation(Location)")) &&
            CaptureModuleSource.Contains(TEXT("SetViewRotation(Rotation)")));
        TestTrue(TEXT("initial viewport applies transient ViewFOV rather than persistent FOVAngle"),
            CaptureModuleSource.Contains(TEXT("ViewFOV =")) &&
            !CaptureModuleSource.Contains(TEXT("FOVAngle =")));
        TestTrue(TEXT("initial viewport invalidates and redraws after applying FOV"),
            CaptureModuleSource.Contains(TEXT("ViewportClient->Invalidate")) &&
            CaptureModuleSource.Contains(TEXT("RedrawLevelEditingViewports")));
        TestTrue(TEXT("initial viewport verifies location rotation and FOV together"),
            CaptureModuleSource.Contains(TEXT("bViewportStateMatches")) &&
            CaptureModuleSource.Contains(TEXT("GetViewLocation")) &&
            CaptureModuleSource.Contains(TEXT("GetViewRotation")) &&
            CaptureModuleSource.Contains(TEXT("ViewFOV")));

        const int32 ViewportStateVerification = CaptureModuleSource.Find(TEXT("bViewportStateMatches"));
        const int32 InitialViewportCompletion = CaptureModuleSource.Find(TEXT("bInitialViewportConfigured = true;"));
        TestTrue(TEXT("initial viewport completion is recorded only after full state verification"),
            ViewportStateVerification != INDEX_NONE &&
            InitialViewportCompletion > ViewportStateVerification);
        TestTrue(TEXT("initial viewport retains unattended Automation and commandlet skips"),
            CaptureModuleSource.Contains(TEXT("FApp::IsUnattended()")) &&
            CaptureModuleSource.Contains(TEXT("GIsAutomationTesting")) &&
            CaptureModuleSource.Contains(TEXT("IsRunningCommandlet()")));
        TestTrue(TEXT("same-session Overview reopen retains the one-time guard"),
            CaptureModuleSource.Contains(TEXT("if (bInitialViewportConfigured")));
        TestFalse(TEXT("initial viewport does not mutate persistent viewport preferences"),
            CaptureModuleSource.Contains(TEXT("GetMutableDefault<ULevelEditorViewportSettings>")));
    }

    const FString UserConfig = ActorMetadataOverlayDemoTests::ReadProjectFile(TEXT("Config/DefaultEditorPerProjectUserSettings.ini"));
    TestTrue(TEXT("presentation keeps Selected display mode"), UserConfig.Contains(TEXT("DisplayMode=Selected")));
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
