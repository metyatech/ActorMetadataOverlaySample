#include "ActorMetadataOverlayDemoActor.h"

#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

AActorMetadataOverlayDemoActor::AActorMetadataOverlayDemoActor()
{
    PrimaryActorTick.bCanEverTick = false;

    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
    SetRootComponent(Mesh);
    Mesh->SetMobility(EComponentMobility::Static);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (CubeMesh.Succeeded())
    {
        Mesh->SetStaticMesh(CubeMesh.Object);
    }
}

void AActorMetadataOverlayDemoActor::GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const
{
    TagContainer = OwnedGameplayTags;
}

void AActorMetadataOverlayDemoActor::SetGameplayTagNames(const TArray<FName>& TagNames)
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
