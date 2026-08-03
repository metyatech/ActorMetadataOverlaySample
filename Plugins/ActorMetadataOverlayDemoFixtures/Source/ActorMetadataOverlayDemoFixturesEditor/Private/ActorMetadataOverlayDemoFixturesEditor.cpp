#include "Editor.h"
#include "EngineUtils.h"
#include "LocationVolume.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Subsystems/UnrealEditorSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogActorMetadataOverlayDemoFixturesEditor, Log, All);

namespace
{
    const TCHAR* DemoMapPackage = TEXT("/Game/ActorMetadataOverlayDemo/Maps/ActorMetadataOverlayOverview");
    const FName DemoRegionName(TEXT("AMO_DemoRegion"));
}

class FActorMetadataOverlayDemoFixturesEditorModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        MapOpenedHandle = FEditorDelegates::OnMapOpened.AddRaw(this, &FActorMetadataOverlayDemoFixturesEditorModule::HandleMapOpened);
        TryLoadDemoRegion();
    }

    virtual void ShutdownModule() override
    {
        RemoveMapOpenedDelegate();
    }

private:
    void HandleMapOpened(const FString&, bool)
    {
        TryLoadDemoRegion();
    }

    void RemoveMapOpenedDelegate()
    {
        if (MapOpenedHandle.IsValid())
        {
            FEditorDelegates::OnMapOpened.Remove(MapOpenedHandle);
            MapOpenedHandle.Reset();
        }
    }

    bool TryLoadDemoRegion()
    {
        if (!GEditor)
        {
            return false;
        }

        UWorld* World = GEditor->GetEditorWorldContext().World();
        if (!World || World->WorldType != EWorldType::Editor || World->GetOutermost()->GetName() != DemoMapPackage)
        {
            return false;
        }

        ALocationVolume* DemoRegion = nullptr;
        int32 RegionCount = 0;
        for (TActorIterator<ALocationVolume> It(World); It; ++It)
        {
            if (It->GetFName() == DemoRegionName)
            {
                ++RegionCount;
                if (RegionCount == 1)
                {
                    DemoRegion = *It;
                }
            }
        }

        if (RegionCount == 0)
        {
            WarnRegionProblem(TEXT("the actor was not found"));
            return false;
        }
        if (RegionCount > 1)
        {
            WarnRegionProblem(TEXT("multiple actors with the same name were found"));
            return false;
        }
        if (!DemoRegion->IsLoaded())
        {
            DemoRegion->Load();
            if (!DemoRegion->IsLoaded())
            {
                WarnRegionProblem(TEXT("the actor did not become loaded"));
                return false;
            }
        }

        DemoRegion->SetIsTemporarilyHiddenInEditor(true);
        if (!DemoRegion->IsTemporarilyHiddenInEditor())
        {
            WarnRegionProblem(TEXT("the actor could not be temporarily hidden in the editor"));
            return false;
        }
        ConfigureInitialOverviewViewport();
        return true;
    }

    void ConfigureInitialOverviewViewport()
    {
        if (bInitialViewportConfigured || FApp::IsUnattended() || GIsAutomationTesting || IsRunningCommandlet())
        {
            return;
        }

        FString Contents;
        if (!FFileHelper::LoadFileToString(Contents, *(FPaths::ProjectDir() / TEXT("Demo/demo-spec.json"))))
        {
            UE_LOG(LogActorMetadataOverlayDemoFixturesEditor, Warning,
                TEXT("Could not read Demo/demo-spec.json for the initial Overview Camera composition."));
            return;
        }

        TSharedPtr<FJsonObject> Spec;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Contents);
        if (!FJsonSerializer::Deserialize(Reader, Spec) || !Spec.IsValid())
        {
            UE_LOG(LogActorMetadataOverlayDemoFixturesEditor, Warning,
                TEXT("Could not parse Demo/demo-spec.json for the initial Overview Camera composition."));
            return;
        }

        const TSharedPtr<FJsonObject>* CameraObject = nullptr;
        if (!Spec->TryGetObjectField(TEXT("overviewCamera"), CameraObject) || !CameraObject || !CameraObject->IsValid())
        {
            UE_LOG(LogActorMetadataOverlayDemoFixturesEditor, Warning,
                TEXT("demo-spec.json has no overviewCamera object for the initial viewport composition."));
            return;
        }

        const TArray<TSharedPtr<FJsonValue>>* LocationValues = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* RotationValues = nullptr;
        if (!(*CameraObject)->TryGetArrayField(TEXT("location"), LocationValues) || !LocationValues || LocationValues->Num() != 3 ||
            !(*CameraObject)->TryGetArrayField(TEXT("rotation"), RotationValues) || !RotationValues || RotationValues->Num() != 3)
        {
            UE_LOG(LogActorMetadataOverlayDemoFixturesEditor, Warning,
                TEXT("demo-spec.json overviewCamera must contain three-value location and rotation arrays."));
            return;
        }

        const FVector Location(
            (*LocationValues)[0]->AsNumber(),
            (*LocationValues)[1]->AsNumber(),
            (*LocationValues)[2]->AsNumber());
        const FRotator Rotation(
            (*RotationValues)[0]->AsNumber(),
            (*RotationValues)[1]->AsNumber(),
            (*RotationValues)[2]->AsNumber());
        if (UUnrealEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UUnrealEditorSubsystem>())
        {
            EditorSubsystem->SetLevelViewportCameraInfo(Location, Rotation);
            bInitialViewportConfigured = true;
            UE_LOG(LogActorMetadataOverlayDemoFixturesEditor, Display,
                TEXT("Initial Overview Camera composition applied once for the Editor session."));
        }
    }

    void WarnRegionProblem(const TCHAR* Reason)
    {
        if (bRegionWarningIssued)
        {
            return;
        }

        bRegionWarningIssued = true;
        const FString RegionName = DemoRegionName.ToString();
        UE_LOG(LogActorMetadataOverlayDemoFixturesEditor, Warning,
            TEXT("Demo region problem: map=%s actor=%s reason=%s"),
            DemoMapPackage,
            *RegionName,
            Reason);
    }

    FDelegateHandle MapOpenedHandle;
    bool bRegionWarningIssued = false;
    bool bInitialViewportConfigured = false;
};

IMPLEMENT_MODULE(FActorMetadataOverlayDemoFixturesEditorModule, ActorMetadataOverlayDemoFixturesEditor);
