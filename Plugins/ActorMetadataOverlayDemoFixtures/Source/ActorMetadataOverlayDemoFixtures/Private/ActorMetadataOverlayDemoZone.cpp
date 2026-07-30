#include "ActorMetadataOverlayDemoZone.h"

#include "Components/BoxComponent.h"

AActorMetadataOverlayDemoZone::AActorMetadataOverlayDemoZone()
{
    PrimaryActorTick.bCanEverTick = false;

    Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
    SetRootComponent(Box);
    Box->SetMobility(EComponentMobility::Static);
    Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Box->SetBoxExtent(FVector(240.0f, 240.0f, 160.0f));
    Box->ShapeColor = FColor(110, 255, 150);
}

void AActorMetadataOverlayDemoZone::GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const
{
    TagContainer = OwnedGameplayTags;
}

void AActorMetadataOverlayDemoZone::SetGameplayTagNames(const TArray<FName>& TagNames)
{
    OwnedGameplayTags.Reset();
    for (const FName& TagName : TagNames)
    {
        const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TagName, false);
        if (Tag.IsValid())
        {
            OwnedGameplayTags.AddTag(Tag);
        }
    }
}
