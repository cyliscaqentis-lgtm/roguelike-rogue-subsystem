#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "DungeonFloorGenerator.h"
#include "EDebugDrawMode.h"
#include "DungeonRenderComponent.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;

USTRUCT(BlueprintType)
struct FDungeonMeshSet
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Meshes") TObjectPtr<UStaticMesh> WallMesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Meshes") TObjectPtr<UStaticMesh> FloorMesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Meshes") TObjectPtr<UStaticMesh> CorridorMesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Meshes") TObjectPtr<UStaticMesh> RoomMesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Meshes") TObjectPtr<UStaticMesh> DoorMesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Meshes") TObjectPtr<UStaticMesh> StairMesh;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Materials") TObjectPtr<UMaterialInterface> WallMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Materials") TObjectPtr<UMaterialInterface> FloorMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Materials") TObjectPtr<UMaterialInterface> CorridorMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Materials") TObjectPtr<UMaterialInterface> RoomMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Materials") TObjectPtr<UMaterialInterface> DoorMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Materials") TObjectPtr<UMaterialInterface> StairMaterial;
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class LYRAGAME_API UDungeonRenderComponent : public USceneComponent
{
    GENERATED_BODY()

public:
    UDungeonRenderComponent();

    virtual void BeginPlay() override;
    virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

    UFUNCTION(BlueprintCallable, Category="Dungeon|Rendering")
    void RenderDungeonFromFloor(ADungeonFloorGenerator* FloorGenerator);

    UFUNCTION(BlueprintCallable, Category="Dungeon|Rendering")
    void ClearAllInstances();

    UFUNCTION(BlueprintCallable, Category="Dungeon|Debug")
    void SetDebugDrawMode(EDebugDrawMode Mode);

    UFUNCTION(BlueprintPure, Category="Dungeon|Debug")
    EDebugDrawMode GetDebugDrawMode() const;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rendering") FDungeonMeshSet MeshSet;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rendering") float CellSizeUU = 100.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rendering") float CellHeightUU = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rendering") bool bRenderInEditor = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rendering") bool bRenderInGame = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rendering") bool bBuildInstancesImmediately = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Debug") EDebugDrawMode DebugDrawMode = EDebugDrawMode::None;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Debug") float DebugDrawScale = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Debug") FLinearColor DebugGridColor = FLinearColor(0.5f, 0.5f, 0.5f, 0.3f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Debug") FLinearColor DebugRoomCenterColor = FLinearColor(1.0f, 0.0f, 0.0f, 1.0f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Debug") FLinearColor DebugCorridorColor = FLinearColor(0.0f, 1.0f, 0.0f, 0.5f);

private:
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> WallISM;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> FloorISM;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> CorridorISM;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> RoomISM;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> DoorISM;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> StairISM;

    TArray<TObjectPtr<UInstancedStaticMeshComponent>> AllISMs;

    UPROPERTY() TObjectPtr<ADungeonFloorGenerator> CachedFloorGenerator;
    TArray<FIntPoint> CachedRoomCenters;

    void CreateISMComponents();
    void DestroyISMComponents();

    UInstancedStaticMeshComponent* CreateISMComponent(const FString& ComponentName, UStaticMesh* Mesh, UMaterialInterface* Material);
    void AddInstanceToISM(UInstancedStaticMeshComponent* ISM, int32 GridX, int32 GridY, float Height=0.0f);

    void BuildAllInstances(ADungeonFloorGenerator* FloorGenerator);
    void ExtractDungeonFeatures(ADungeonFloorGenerator* FloorGenerator);

    void DrawDebugGrid();
    void DrawDebugRoomCenters();
    void DrawDebugCorridorPaths();

    FVector GridToWorldPosition(int32 GridX, int32 GridY, float HeightOffset=0.0f) const;
};
