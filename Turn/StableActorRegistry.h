// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "StableActorRegistry.generated.h"

/**
 * StableActorID: 決定性保証のための複合キー（v2.2 第16条）
 */
USTRUCT(BlueprintType)
struct LYRAGAME_API FStableActorID
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "StableID")
    FGuid PersistentGUID;

    UPROPERTY(BlueprintReadWrite, Category = "StableID")
    int32 GenerationOrder = -1;

    bool operator<(const FStableActorID& Other) const
    {
        if (PersistentGUID != Other.PersistentGUID)
            return PersistentGUID < Other.PersistentGUID;
        return GenerationOrder < Other.GenerationOrder;
    }

    bool operator==(const FStableActorID& Other) const
    {
        return PersistentGUID == Other.PersistentGUID && GenerationOrder == Other.GenerationOrder;
    }

    bool IsValid() const
    {
        return PersistentGUID.IsValid() && GenerationOrder >= 0;
    }

    FString ToString() const
    {
        return FString::Printf(TEXT("<%s,%d>"), *PersistentGUID.ToString(), GenerationOrder);
    }
};

FORCEINLINE uint32 GetTypeHash(const FStableActorID& ID)
{
    return HashCombine(GetTypeHash(ID.PersistentGUID), GetTypeHash(ID.GenerationOrder));
}

/**
 * UStableActorRegistry: Actor登録と安定ID管理
 */
UCLASS()
class LYRAGAME_API UStableActorRegistry : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    UFUNCTION(BlueprintCallable, Category = "Turn|StableID")
    FStableActorID RegisterActor(AActor* Actor);

    UFUNCTION(BlueprintCallable, Category = "Turn|StableID")
    void RestoreFromSave(AActor* Actor, const FStableActorID& SavedID);

    UFUNCTION(BlueprintPure, Category = "Turn|StableID")
    FStableActorID GetStableID(AActor* Actor) const;

    UFUNCTION(BlueprintPure, Category = "Turn|StableID")
    AActor* GetActorByID(const FStableActorID& StableID) const;

    UFUNCTION(BlueprintCallable, Category = "Turn|StableID")
    void UnregisterActor(AActor* Actor);

private:
    UPROPERTY()
    TMap<TObjectPtr<AActor>, FStableActorID> ActorToID;

    UPROPERTY()
    TMap<FStableActorID, TWeakObjectPtr<AActor>> IDToActor;

    int32 NextGenerationOrder = 0;
    TSet<FGuid> UsedGUIDs;
};
