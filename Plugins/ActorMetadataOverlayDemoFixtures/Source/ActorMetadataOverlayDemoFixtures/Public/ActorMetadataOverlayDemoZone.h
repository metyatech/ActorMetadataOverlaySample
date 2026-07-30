#pragma once

#include "CoreMinimal.h"
#include "GameplayTagAssetInterface.h"
#include "GameplayTagContainer.h"
#include "GameFramework/Actor.h"
#include "ActorMetadataOverlayDemoZone.generated.h"

class UBoxComponent;

UCLASS(BlueprintType, Blueprintable)
class ACTORMETADATAOVERLAYDEMOFIXTURES_API AActorMetadataOverlayDemoZone : public AActor, public IGameplayTagAssetInterface
{
    GENERATED_BODY()

public:
    AActorMetadataOverlayDemoZone();

    virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override;

    UFUNCTION(BlueprintCallable, Category = "Actor Metadata Overlay Demo")
    void SetGameplayTagNames(const TArray<FName>& TagNames);

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Actor Metadata Overlay Demo")
    TObjectPtr<UBoxComponent> Box;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actor Metadata Overlay Demo")
    FString State;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actor Metadata Overlay Demo")
    int32 Priority = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actor Metadata Overlay Demo")
    float Radius = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actor Metadata Overlay Demo")
    FGameplayTagContainer OwnedGameplayTags;
};
