#include "Editor.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "LocationVolume.h"
#include "LevelEditor.h"
#include "LevelEditorViewport.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Widgets/SWindow.h"

DEFINE_LOG_CATEGORY_STATIC(LogActorMetadataOverlayDemoFixturesEditor, Log, All);

namespace
{
    const TCHAR* DemoMapPackage = TEXT("/Game/ActorMetadataOverlayDemo/Maps/ActorMetadataOverlayOverview");
    const FName DemoRegionName(TEXT("AMO_DemoRegion"));

    bool TryResolveReviewOutputPath(
        const FString& OutputArgument,
        const TCHAR* RequiredExtension,
        FString& OutOutputPath,
        FString& OutAllowedRoot)
    {
        FString UnquotedArgument = OutputArgument;
        UnquotedArgument.TrimQuotesInline();
        OutOutputPath = FPaths::ConvertRelativePathToFull(UnquotedArgument);
        OutAllowedRoot = FPaths::ConvertRelativePathToFull(
            FPaths::ProjectDir() / TEXT(".verification/user-review"));
        FPaths::MakeStandardFilename(OutOutputPath);
        FPaths::MakeStandardFilename(OutAllowedRoot);
        if (!OutAllowedRoot.EndsWith(TEXT("/"), ESearchCase::CaseSensitive))
        {
            OutAllowedRoot += TEXT("/");
        }

        return OutOutputPath.StartsWith(OutAllowedRoot, ESearchCase::IgnoreCase) &&
               OutOutputPath.EndsWith(RequiredExtension, ESearchCase::IgnoreCase);
    }

    FLevelEditorViewportClient* FindActivePerspectiveLevelViewportClient()
    {
        if (!GEditor)
        {
            return nullptr;
        }

        FViewport* ActiveViewport = GEditor->GetActiveViewport();
        for (FLevelEditorViewportClient* ViewportClient : GEditor->GetLevelViewportClients())
        {
            if (ViewportClient && ViewportClient->Viewport == ActiveViewport && ViewportClient->IsPerspective())
            {
                return ViewportClient;
            }
        }

        return nullptr;
    }

    FLevelEditorViewportClient* FindPreferredPerspectiveLevelViewportClient()
    {
        if (FLevelEditorViewportClient* ActiveViewportClient = FindActivePerspectiveLevelViewportClient())
        {
            return ActiveViewportClient;
        }

        if (!GEditor)
        {
            return nullptr;
        }

        for (FLevelEditorViewportClient* ViewportClient : GEditor->GetLevelViewportClients())
        {
            if (ViewportClient && ViewportClient->IsPerspective())
            {
                return ViewportClient;
            }
        }

        return nullptr;
    }
}

class FActorMetadataOverlayDemoFixturesEditorModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        CaptureDebugCanvasCommand = MakeUnique<FAutoConsoleCommand>(
            TEXT("ActorMetadataOverlayDemo.CaptureSlateDebugCanvas"),
            TEXT("Capture the active editor window, including the real Slate Debug Canvas, to a BMP under .verification/user-review."),
            FConsoleCommandWithArgsDelegate::CreateRaw(this, &FActorMetadataOverlayDemoFixturesEditorModule::CaptureSlateDebugCanvas));
        WriteActiveViewportStateCommand = MakeUnique<FAutoConsoleCommand>(
            TEXT("ActorMetadataOverlayDemo.WriteActiveViewportState"),
            TEXT("Write the active Perspective Level Editor viewport state to a JSON file under .verification/user-review without changing it."),
            FConsoleCommandWithArgsDelegate::CreateRaw(this, &FActorMetadataOverlayDemoFixturesEditorModule::WriteActiveViewportState));
        MapOpenedHandle = FEditorDelegates::OnMapOpened.AddRaw(this, &FActorMetadataOverlayDemoFixturesEditorModule::HandleMapOpened);
        FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
        LevelEditorCreatedHandle = LevelEditorModule.OnLevelEditorCreated().AddRaw(
            this, &FActorMetadataOverlayDemoFixturesEditorModule::HandleLevelEditorCreated);
        TryLoadDemoRegion();
    }

    virtual void ShutdownModule() override
    {
        CaptureDebugCanvasCommand.Reset();
        WriteActiveViewportStateCommand.Reset();
        RemoveMapOpenedDelegate();
        RemoveLevelEditorCreatedDelegate();
    }

private:
    void CaptureSlateDebugCanvas(const TArray<FString>& Arguments)
    {
        if (Arguments.Num() != 1)
        {
            UE_LOG(LogActorMetadataOverlayDemoFixturesEditor, Error,
                TEXT("CaptureSlateDebugCanvas requires exactly one BMP output path under .verification/user-review."));
            return;
        }

        FString OutputPath;
        FString AllowedRoot;
        if (!TryResolveReviewOutputPath(Arguments[0], TEXT(".bmp"), OutputPath, AllowedRoot))
        {
            UE_LOG(LogActorMetadataOverlayDemoFixturesEditor, Error,
                TEXT("CaptureSlateDebugCanvas rejected output path outside %s or without a .bmp extension: %s"),
                *AllowedRoot,
                *OutputPath);
            return;
        }

        FViewport* ActiveViewport = GEditor ? GEditor->GetActiveViewport() : nullptr;
        TSharedPtr<SWindow> ActiveWindow = FSlateApplication::IsInitialized()
            ? FSlateApplication::Get().GetActiveTopLevelWindow()
            : nullptr;
        if (!ActiveViewport || !ActiveWindow.IsValid())
        {
            UE_LOG(LogActorMetadataOverlayDemoFixturesEditor, Error,
                TEXT("CaptureSlateDebugCanvas could not resolve the active viewport and editor window."));
            return;
        }

        IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutputPath), true);
        ActiveViewport->Draw(false);
        TArray<FColor> ImageData;
        FIntVector ImageSize;
        if (!FSlateApplication::Get().TakeScreenshot(ActiveWindow.ToSharedRef(), ImageData, ImageSize) ||
            !FFileHelper::CreateBitmap(*OutputPath, ImageSize.X, ImageSize.Y, ImageData.GetData()))
        {
            UE_LOG(LogActorMetadataOverlayDemoFixturesEditor, Error,
                TEXT("CaptureSlateDebugCanvas failed to save %s."),
                *OutputPath);
            return;
        }

        UE_LOG(LogActorMetadataOverlayDemoFixturesEditor, Display,
            TEXT("CAPTURE_SLATE_DEBUG_CANVAS_SAVED path=%s size=%dx%d"),
            *OutputPath,
            ImageSize.X,
            ImageSize.Y);
    }

    void WriteActiveViewportState(const TArray<FString>& Arguments)
    {
        if (Arguments.Num() != 1)
        {
            UE_LOG(LogActorMetadataOverlayDemoFixturesEditor, Error,
                TEXT("WriteActiveViewportState requires exactly one JSON output path under .verification/user-review."));
            return;
        }

        FString OutputPath;
        FString AllowedRoot;
        if (!TryResolveReviewOutputPath(Arguments[0], TEXT(".json"), OutputPath, AllowedRoot))
        {
            UE_LOG(LogActorMetadataOverlayDemoFixturesEditor, Error,
                TEXT("WriteActiveViewportState rejected output path outside %s or without a .json extension: %s"),
                *AllowedRoot,
                *OutputPath);
            return;
        }

        const FLevelEditorViewportClient* ViewportClient = FindActivePerspectiveLevelViewportClient();
        const UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
        if (!ViewportClient || !World)
        {
            UE_LOG(LogActorMetadataOverlayDemoFixturesEditor, Error,
                TEXT("WriteActiveViewportState could not resolve the active Perspective Level Editor viewport."));
            return;
        }

        const FVector Location = ViewportClient->GetViewLocation();
        const FRotator Rotation = ViewportClient->GetViewRotation();
        TSharedRef<FJsonObject> State = MakeShared<FJsonObject>();
        State->SetStringField(TEXT("map"), World->GetOutermost()->GetName());
        State->SetBoolField(TEXT("perspective"), ViewportClient->IsPerspective());
        State->SetArrayField(TEXT("location"), {
            MakeShared<FJsonValueNumber>(Location.X),
            MakeShared<FJsonValueNumber>(Location.Y),
            MakeShared<FJsonValueNumber>(Location.Z)});
        State->SetArrayField(TEXT("rotation"), {
            MakeShared<FJsonValueNumber>(Rotation.Pitch),
            MakeShared<FJsonValueNumber>(Rotation.Yaw),
            MakeShared<FJsonValueNumber>(Rotation.Roll)});
        State->SetNumberField(TEXT("fov"), ViewportClient->ViewFOV);

        FString SerializedState;
        const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&SerializedState);
        if (!FJsonSerializer::Serialize(State, Writer))
        {
            UE_LOG(LogActorMetadataOverlayDemoFixturesEditor, Error,
                TEXT("WriteActiveViewportState could not serialize viewport state for %s."),
                *OutputPath);
            return;
        }

        IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutputPath), true);
        if (!FFileHelper::SaveStringToFile(SerializedState, *OutputPath))
        {
            UE_LOG(LogActorMetadataOverlayDemoFixturesEditor, Error,
                TEXT("WriteActiveViewportState could not save %s."),
                *OutputPath);
            return;
        }

        UE_LOG(LogActorMetadataOverlayDemoFixturesEditor, Display,
            TEXT("ACTIVE_VIEWPORT_STATE_SAVED path=%s map=%s location=%s rotation=%s fov=%.6f"),
            *OutputPath,
            *World->GetOutermost()->GetName(),
            *Location.ToString(),
            *Rotation.ToString(),
            ViewportClient->ViewFOV);
    }

    void HandleMapOpened(const FString&, bool)
    {
        TryLoadDemoRegion();
    }

    void HandleLevelEditorCreated(TSharedPtr<ILevelEditor>)
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

    void RemoveLevelEditorCreatedDelegate()
    {
        if (LevelEditorCreatedHandle.IsValid() && FModuleManager::Get().IsModuleLoaded(TEXT("LevelEditor")))
        {
            FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
            LevelEditorModule.OnLevelEditorCreated().Remove(LevelEditorCreatedHandle);
            LevelEditorCreatedHandle.Reset();
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
        double Fov = 0.0;
        if (!(*CameraObject)->TryGetArrayField(TEXT("location"), LocationValues) || !LocationValues || LocationValues->Num() != 3 ||
            !(*CameraObject)->TryGetArrayField(TEXT("rotation"), RotationValues) || !RotationValues || RotationValues->Num() != 3 ||
            !(*CameraObject)->TryGetNumberField(TEXT("fov"), Fov))
        {
            UE_LOG(LogActorMetadataOverlayDemoFixturesEditor, Warning,
                TEXT("demo-spec.json overviewCamera must contain three-value location and rotation arrays plus a numeric fov; actual fov is missing or non-numeric."));
            return;
        }
        if (!FMath::IsFinite(Fov) || Fov < 35.0 || Fov > 80.0)
        {
            UE_LOG(LogActorMetadataOverlayDemoFixturesEditor, Warning,
                TEXT("demo-spec.json overviewCamera fov is invalid; actual=%.17g required=finite range=[35,80]."),
                Fov);
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
        FLevelEditorViewportClient* ViewportClient = FindPreferredPerspectiveLevelViewportClient();
        if (!ViewportClient)
        {
            UE_LOG(LogActorMetadataOverlayDemoFixturesEditor, Warning,
                TEXT("Initial Overview Camera composition could not find a Perspective Level Editor viewport; expected location=%s rotation=%s fov=%.6f."),
                *Location.ToString(),
                *Rotation.ToString(),
                Fov);
            return;
        }

        ViewportClient->SetViewLocationForOrbiting(Location);
        ViewportClient->SetViewLocation(Location);
        ViewportClient->SetViewRotation(Rotation);
        ViewportClient->ViewFOV = static_cast<float>(Fov);
        ViewportClient->Invalidate(false, false);
        if (ViewportClient->Viewport)
        {
            ViewportClient->Viewport->InvalidateDisplay();
        }
        GEditor->RedrawLevelEditingViewports(false);

        const FVector ActualLocation = ViewportClient->GetViewLocation();
        const FRotator ActualRotation = ViewportClient->GetViewRotation();
        const float ActualFov = ViewportClient->ViewFOV;
        const bool bViewportStateMatches =
            ActualLocation.Equals(Location, 0.01) &&
            ActualRotation.Equals(Rotation, 0.01) &&
            FMath::IsNearlyEqual(ActualFov, static_cast<float>(Fov), 0.01f);
        if (!bViewportStateMatches)
        {
            UE_LOG(LogActorMetadataOverlayDemoFixturesEditor, Warning,
                TEXT("Initial Overview Camera verification failed; expected location=%s rotation=%s fov=%.6f actual location=%s rotation=%s fov=%.6f."),
                *Location.ToString(),
                *Rotation.ToString(),
                Fov,
                *ActualLocation.ToString(),
                *ActualRotation.ToString(),
                ActualFov);
            return;
        }

        bInitialViewportConfigured = true;
        UE_LOG(LogActorMetadataOverlayDemoFixturesEditor, Display,
            TEXT("Initial Overview Camera composition applied once for the Editor session: location=%s rotation=%s fov=%.6f."),
            *ActualLocation.ToString(),
            *ActualRotation.ToString(),
            ActualFov);
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
    FDelegateHandle LevelEditorCreatedHandle;
    TUniquePtr<FAutoConsoleCommand> CaptureDebugCanvasCommand;
    TUniquePtr<FAutoConsoleCommand> WriteActiveViewportStateCommand;
    bool bRegionWarningIssued = false;
    bool bInitialViewportConfigured = false;
};

IMPLEMENT_MODULE(FActorMetadataOverlayDemoFixturesEditorModule, ActorMetadataOverlayDemoFixturesEditor);
