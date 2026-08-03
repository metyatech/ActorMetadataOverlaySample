#include "Editor.h"
#include "EngineUtils.h"
#include "LocationVolume.h"
#include "Modules/ModuleManager.h"

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
        if (DemoRegion->IsLoaded())
        {
            return true;
        }

        DemoRegion->Load();
        return DemoRegion->IsLoaded();
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
};

IMPLEMENT_MODULE(FActorMetadataOverlayDemoFixturesEditorModule, ActorMetadataOverlayDemoFixturesEditor);
