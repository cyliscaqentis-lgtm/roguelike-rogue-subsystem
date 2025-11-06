#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

class ADungeonFloorGenerator;
class URogueFloorConfigData;
class UDungeonRenderComponent;
class AAABB;

#include "URogueDungeonSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGridReady);

UCLASS()
class LYRAGAME_API URogueDungeonSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category="Rogue|Dungeon")
    void RegisterFloorConfig(int32 FloorIndex, URogueFloorConfigData* Config);

    UFUNCTION(BlueprintCallable, Category="Rogue|Dungeon")
    void TransitionToFloor(int32 FloorIndex);

    UFUNCTION(BlueprintCallable, Category="Rogue|Dungeon")
    ADungeonFloorGenerator* GetFloorGenerator() const;

    /** レンダラー取得（必要なら内部で生成） */
    UFUNCTION(BlueprintCallable, Category="Rogue|Dungeon")
    UDungeonRenderComponent* GetRenderComponent();

    UPROPERTY(BlueprintAssignable, Category="Rogue|Dungeon")
    FOnGridReady OnGridReady;

    UPROPERTY(EditAnywhere, Category="Rogue|Dungeon")
    TObjectPtr<URogueFloorConfigData> DefaultConfig = nullptr;

    UPROPERTY(EditAnywhere, Category="Rogue|Dungeon")
    bool bUseRandomSeed = true;

    UPROPERTY(EditAnywhere, Category="Rogue|Dungeon")
    int32 SeedBase = 12345;

    UPROPERTY(EditAnywhere, Category="Rogue|Dungeon")
    FName RenderSingletonTag = FName(TEXT("RogueGridRenderSingleton"));

    UFUNCTION(BlueprintCallable, Category="Rogue|Dungeon|Debug")
    void SmokeTest() const;

    /** 生成済みダンジョンから検出した部屋マーカーを取得（生成されていない場合は空配列） */
    void GetGeneratedRooms(TArray<AAABB*>& OutRooms) const;

    virtual void Deinitialize() override;

protected:
    UPROPERTY(Transient)
    TObjectPtr<ADungeonFloorGenerator> FloorGenerator = nullptr;

    UPROPERTY(Transient)
    TObjectPtr<UDungeonRenderComponent> RenderComponent = nullptr;

    UPROPERTY(Transient)
    TArray<TObjectPtr<AAABB>> RoomMarkers;

    UPROPERTY(Transient)
    TMap<int32, TObjectPtr<URogueFloorConfigData>> FloorConfigMap;

    int32 CurrentFloorIndex = INDEX_NONE;

    void EnsureFloorGenerator();
    void EnsureRenderComponent();
    void EnsureRenderComponentFromPlacedActor();
    URogueFloorConfigData* GetActiveConfig(int32 FloorIndex) const;
    void BuildFloor_Internal(URogueFloorConfigData* Config, int32 FloorIndex);
    void RebuildRoomMarkers();
    void DestroyRoomMarkers();
};
