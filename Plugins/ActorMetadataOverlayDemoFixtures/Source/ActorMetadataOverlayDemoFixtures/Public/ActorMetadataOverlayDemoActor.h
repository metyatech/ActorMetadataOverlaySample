#pragma once

#include "CoreMinimal.h"
#include "GameplayTagAssetInterface.h"
#include "GameplayTagContainer.h"
#include "GameFramework/Actor.h"
#include "ActorMetadataOverlayDemoActor.generated.h"

class UStaticMeshComponent;

UCLASS(BlueprintType, Blueprintable)
class ACTORMETADATAOVERLAYDEMOFIXTURES_API AActorMetadataOverlayDemoActor : public AActor, public IGameplayTagAssetInterface
{
    GENERATED_BODY()

public:
    AActorMetadataOverlayDemoActor();

    virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override;

    UFUNCTION(BlueprintCallable, Category = "Actor Metadata Overlay Demo")
    void SetGameplayTagNames(const TArray<FName>& TagNames);

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Actor Metadata Overlay Demo")
    TObjectPtr<UStaticMeshComponent> Mesh;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actor Metadata Overlay Demo")
    FString State;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actor Metadata Overlay Demo")
    int32 Priority = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actor Metadata Overlay Demo")
    float Radius = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actor Metadata Overlay Demo")
    FText Description;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actor Metadata Overlay Demo")
    FGameplayTagContainer OwnedGameplayTags;
};
