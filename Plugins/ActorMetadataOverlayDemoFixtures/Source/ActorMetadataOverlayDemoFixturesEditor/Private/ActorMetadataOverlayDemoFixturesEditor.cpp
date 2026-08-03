#include "Editor.h"
#include "EngineUtils.h"
#include "LocationVolume.h"
#include "Modules/ModuleManager.h"

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
        if (TryLoadDemoRegion())
        {
            RemoveMapOpenedDelegate();
        }
    }

    virtual void ShutdownModule() override
    {
        RemoveMapOpenedDelegate();
    }

private:
    void HandleMapOpened(const FString&, bool)
    {
        if (TryLoadDemoRegion() && MapOpenedHandle.IsValid())
        {
            RemoveMapOpenedDelegate();
        }
    }

    void RemoveMapOpenedDelegate()
    {
        if (MapOpenedHandle.IsValid())
        {
            FEditorDelegates::OnMapOpened.Remove(MapOpenedHandle);
            MapOpenedHandle.Reset();
        }
    }

    bool TryLoadDemoRegion() const
    {
        if (!GEditor)
        {
            return false;
        }

        UWorld* World = GEditor->GetEditorWorldContext().World();
        if (!World || World->GetOutermost()->GetName() != DemoMapPackage)
        {
            return false;
        }

        for (TActorIterator<ALocationVolume> It(World); It; ++It)
        {
            if (It->GetFName() == DemoRegionName)
            {
                It->Load();
                return true;
            }
        }
        return false;
    }

    FDelegateHandle MapOpenedHandle;
};

IMPLEMENT_MODULE(FActorMetadataOverlayDemoFixturesEditorModule, ActorMetadataOverlayDemoFixturesEditor);
