#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

class ADungeonFloorGenerator;
class URogueFloorConfigData; // Use the RogueFloorConfigData Asset
class UDungeonRenderComponent;
class AAABB;

#include "URogueDungeonSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGridReady, URogueDungeonSubsystem*, DungeonSubsystem);

UCLASS()
class LYRAGAME_API URogueDungeonSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    /** The single entry point to start dungeon generation, driven by a config actor placed in the level. */
    UFUNCTION(BlueprintCallable, Category = "Rogue|Dungeon")
    void StartGenerateFromLevel();

    UFUNCTION(BlueprintCallable, Category="Rogue|Dungeon")
    ADungeonFloorGenerator* GetFloorGenerator() const;

    /** レンダラー取得（必要なら内部で生成） */
    UFUNCTION(BlueprintCallable, Category="Rogue|Dungeon")
    UDungeonRenderComponent* GetRenderComponent();

    UPROPERTY(BlueprintAssignable, Category="Rogue|Dungeon")
    FOnGridReady OnGridReady;

    UPROPERTY(EditAnywhere, Category="Rogue|Dungeon")
    FName RenderSingletonTag = FName(TEXT("RogueGridRenderSingleton"));

    UFUNCTION(BlueprintCallable, Category="Rogue|Dungeon|Debug")
    void SmokeTest() const;

    /** 生成済みダンジョンから検出した部屋マーカーを取得（生成されていない場合は空配列） */
    void GetGeneratedRooms(TArray<AAABB*>& OutRooms) const;

    /** 指定されたフロアに遷移（新しいダンジョンを生成） */
    UFUNCTION(BlueprintCallable, Category="Rogue|Dungeon")
    void TransitionToFloor(int32 FloorIndex);

    virtual void Deinitialize() override;

protected:
    UPROPERTY(Transient)
    TObjectPtr<ADungeonFloorGenerator> FloorGenerator = nullptr;

    UPROPERTY(Transient)
    TObjectPtr<class ADungeonConfigActor> CachedConfigActor = nullptr;

    bool bDungeonGenerationStarted = false;

    UPROPERTY(Transient)
    TObjectPtr<UDungeonRenderComponent> RenderComponent = nullptr;

    UPROPERTY(Transient)
    TArray<TObjectPtr<AAABB>> RoomMarkers;

    void EnsureFloorGenerator();
    void EnsureRenderComponent();
    void EnsureRenderComponentFromPlacedActor();
    void EnsureConfigActor();

    /** Starts the actual generation process using a loaded config asset. */
    void StartGenerate(const URogueFloorConfigData* Cfg);

    void RebuildRoomMarkers();
    void DestroyRoomMarkers();
};
