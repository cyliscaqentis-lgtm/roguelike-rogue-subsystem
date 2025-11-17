// =============================================================================
// GridPathfindingSubsystem.h
// CodeRevision: INC-2025-00027-R1 (Create UGridPathfindingSubsystem - Phase 2) (2025-11-16 00:00)
// Grid pathfinding functionality migrated from AGridPathfindingLibrary to UWorldSubsystem
// =============================================================================

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "../Utility/ProjectDiagnostics.h"
#include "GridPathfindingSubsystem.generated.h"

// CodeRevision: INC-2025-00030-R2 (Add missing type definitions) (2025-11-17 00:40)
// Type definitions moved from GridPathfindingLibrary.h (which no longer exists)

UENUM(BlueprintType)
enum class EGridHeuristic : uint8
{
	Manhattan UMETA(DisplayName = "Manhattan"),
	Euclidean UMETA(DisplayName = "Euclidean"),
	Chebyshev UMETA(DisplayName = "Chebyshev")
};

USTRUCT(BlueprintType)
struct FGridInitParams
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	TArray<int32> GridCostArray;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FVector MapSize = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	int32 TileSizeCM = 100;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FVector Origin = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct FGridVisionResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite)
	TArray<TObjectPtr<AActor>> DetectedActors;

	UPROPERTY(BlueprintReadWrite)
	TArray<TObjectPtr<AActor>> VisibleActors;

	UPROPERTY(BlueprintReadWrite)
	TArray<FIntPoint> VisibleCells;

	UPROPERTY(BlueprintReadWrite)
	TArray<FIntPoint> VisibleTiles;

	UPROPERTY(BlueprintReadWrite)
	int32 TotalTilesScanned = 0;
};

USTRUCT(BlueprintType)
struct FGridSurroundResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite)
	TArray<TObjectPtr<AActor>> AdjacentActors;

	UPROPERTY(BlueprintReadWrite)
	TArray<FIntPoint> AdjacentCells;

	UPROPERTY(BlueprintReadWrite)
	TArray<FIntPoint> BlockedTiles;

	UPROPERTY(BlueprintReadWrite)
	TArray<FIntPoint> EmptyTiles;
};

// Forward declarations
class AActor;
class AController;

/**
 * UGridPathfindingSubsystem
 * 
 * Grid pathfinding functionality as a WorldSubsystem.
 * Migrated from AGridPathfindingLibrary to provide singleton access without Actor spawning.
 */
UCLASS()
class LYRAGAME_API UGridPathfindingSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    // ========== Subsystem Lifecycle ==========
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // ========== Exec Commands ==========
    UFUNCTION(Exec)
    void GridSmokeTest();

    UFUNCTION(Exec)
    void GridAuditEnable(int32 bEnable);

    UFUNCTION(Exec)
    void GridAuditProbe(int32 X, int32 Y);

    // ========== Grid Initialization ==========

    /** Initialize grid with cost array, map size, and tile size */
    UFUNCTION(BlueprintCallable, Category = "Pathfinding|Setup")
    void InitializeGrid(const TArray<int32>& InGridCost, const FVector& InMapSize, int32 InTileSizeCM = 100);

    /** Initialize grid from parameters struct */
    UFUNCTION(BlueprintCallable, Category = "Pathfinding|Setup")
    void InitializeFromParams(const FGridInitParams& Params);

    /** Get grid dimensions and tile size */
    UFUNCTION(BlueprintPure, Category = "Pathfinding|Setup")
    void GetGridInfo(int32& OutWidth, int32& OutHeight, int32& OutTileSize) const
    {
        OutWidth = GridWidth;
        OutHeight = GridHeight;
        OutTileSize = TileSize;
    }

    /** Get grid origin */
    UFUNCTION(BlueprintPure, Category = "Pathfinding|Setup")
    FVector GetGridOrigin() const { return Origin; }

    /** Get navigation plane Z coordinate */
    FORCEINLINE float GetNavPlaneZ() const { return Origin.Z; }

    /** Check if grid is initialized */
    UFUNCTION(BlueprintPure, Category = "Pathfinding|Utility")
    bool IsInitialized() const
    {
        return GridWidth > 0 && GridHeight > 0 && GridCells.Num() > 0;
    }

    /** Get extended grid info including origin */
    UFUNCTION(BlueprintPure, Category = "Pathfinding|Utility")
    void GetGridInfoEx(int32& OutWidth, int32& OutHeight, int32& OutTileSize, FVector& OutOrigin) const
    {
        OutWidth = GridWidth;
        OutHeight = GridHeight;
        OutTileSize = TileSize;
        OutOrigin = Origin;
    }

    // ========== Legacy Compatibility Aliases ==========

    /** Legacy PathFinder GridChange compatibility */
    UFUNCTION(BlueprintCallable, Category = "Pathfinding|Setup", meta = (DisplayName = "Grid Change"))
    void GridChange(int32 X, int32 Y, int32 Value) { SetGridCost(X, Y, Value); }

    /** Terrain editing (not for occupancy - use UGridOccupancySubsystem) */
    UFUNCTION(BlueprintCallable, Category="Grid|Terrain")
    void GridChangeVector(const FVector& InputVector, int32 Value) { SetGridCostAtWorldPosition(InputVector, Value); }

    /** Read terrain cost (does not include occupancy - use UGridOccupancySubsystem) */
    UFUNCTION(BlueprintPure, Category="Grid|Terrain")
    int32 ReturnGridStatus(const FVector& InputVector) const;

    /** Legacy PathFinder VectorToGrid compatibility */
    UFUNCTION(BlueprintPure, Category = "Pathfinding|Utility", meta = (DisplayName = "Vector To Grid"))
    FIntPoint VectorToGrid(const FVector& WorldPos) const { return WorldToGrid(WorldPos); }

    // ========== Terrain Management ==========

    /** Set terrain cost at grid coordinates (terrain only - never use for occupancy) */
    UFUNCTION(BlueprintCallable, Category="Grid|Terrain")
    void SetGridCost(int32 X, int32 Y, int32 NewCost);

    /** Set terrain cost at world position */
    UFUNCTION(BlueprintCallable, Category = "Pathfinding|Setup")
    void SetGridCostAtWorldPosition(const FVector& WorldPos, int32 NewCost);

    /** Get terrain cost at grid coordinates */
    UFUNCTION(BlueprintPure, Category = "Pathfinding|Setup")
    int32 GetGridCost(int32 X, int32 Y) const;

    // ========== Walkability Checks ==========

    /** Check if cell is walkable ignoring a specific actor */
    UFUNCTION(BlueprintCallable, Category = "Grid|Walkability")
    bool IsCellWalkableIgnoringActor(const FIntPoint& Cell, AActor* IgnoreActor) const;

    // ========== Movement Validation ==========

    /**
     * Validates move legality (terrain, corner-cutting, occupancy, Chebyshev distance)
     * @param From Starting cell
     * @param To Target cell
     * @param MovingActor Actor attempting to move
     * @param OutFailureReason Failure reason if invalid (empty on success)
     * @return true if move is valid
     */
    UFUNCTION(BlueprintCallable, Category = "Grid|Validation")
    bool IsMoveValid(
        const FIntPoint& From,
        const FIntPoint& To,
        AActor* MovingActor,
        FString& OutFailureReason) const;

    // ========== Pathfinding ==========

    /** Standard FindPath (fails if start/end cells are -1) */
    UFUNCTION(BlueprintCallable, Category = "Pathfinding|PathFinding")
    bool FindPath(const FVector& StartWorld, const FVector& EndWorld, TArray<FVector>& OutWorldPath,
        bool bAllowDiagonal = true, EGridHeuristic Heuristic = EGridHeuristic::Manhattan,
        int32 SearchLimit = 200000, bool bHeavyDiagonal = true) const;

    /** FindPath ignoring occupied endpoints (allows pathfinding even if start/end cells are occupied) */
    UFUNCTION(BlueprintCallable, Category = "Pathfinding|PathFinding", meta = (DisplayName = "Find Path (Ignore Occupied)"))
    bool FindPathIgnoreEndpoints(const FVector& StartWorld, const FVector& EndWorld, TArray<FVector>& OutWorldPath,
        bool bAllowDiagonal = true, EGridHeuristic Heuristic = EGridHeuristic::Manhattan,
        int32 SearchLimit = 200000, bool bHeavyDiagonal = true) const;

    // ========== Vision Detection (FOV) ==========

    UFUNCTION(BlueprintCallable, Category = "Pathfinding|Vision")
    FGridVisionResult DetectInExpandingVision(
        const FVector& CenterWorld,
        const FVector& ForwardDirection,
        int32 MaxDepth = 3,
        TSubclassOf<AActor> ActorClassFilter = nullptr) const;

    UFUNCTION(BlueprintCallable, Category = "Pathfinding|Vision")
    FGridVisionResult DetectInRadius(
        const FVector& CenterWorld,
        int32 Radius = 5,
        bool bCheckLineOfSight = true,
        TSubclassOf<AActor> ActorClassFilter = nullptr) const;

    // ========== Adjacent Search ==========

    UFUNCTION(BlueprintCallable, Category = "Pathfinding|Detection")
    FGridSurroundResult SearchAdjacentTiles(
        const FVector& CenterWorld,
        bool bIncludeDiagonal = true,
        TSubclassOf<AActor> ActorClassFilter = nullptr) const;

    UFUNCTION(BlueprintPure, Category = "Pathfinding|Detection")
    bool HasLineOfSight(const FVector& StartWorld, const FVector& EndWorld) const;

    // ========== Utility Functions ==========

    /** Convert world position to grid coordinates */
    UFUNCTION(BlueprintPure, Category = "Pathfinding|Utility")
    FIntPoint WorldToGrid(const FVector& WorldPos) const;

    /** Convert grid coordinates to world position */
    UFUNCTION(BlueprintPure, Category = "Pathfinding|Utility")
    FVector GridToWorld(const FIntPoint& GridPos, float Z = 0.f) const;

    /** Get cell center world position with specified Z */
    UFUNCTION(BlueprintPure, Category = "Pathfinding|Utility")
    FVector GridToWorldCenter(const FIntPoint& Cell, float Z = 0.0f) const
    {
        return FVector(
            Origin.X + (Cell.X + 0.5f) * TileSize,
            Origin.Y + (Cell.Y + 0.5f) * TileSize,
            Z
        );
    }

    /** Get Manhattan distance between two world positions */
    UFUNCTION(BlueprintPure, Category = "Pathfinding|Utility")
    int32 GetManhattanDistance(const FVector& PosA, const FVector& PosB) const;

protected:
    // ========== Grid State ==========
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pathfinding")
    int32 GridWidth = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pathfinding")
    int32 GridHeight = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pathfinding", meta = (ClampMin = 1))
    int32 TileSize = 100;

    /** Grid world space origin (reference point for WorldToGrid/GridToWorld) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pathfinding")
    FVector Origin = FVector::ZeroVector;

    /** Grid cost array (BP compatibility - auditing implemented in SetGridCost) */
    UPROPERTY(BlueprintReadOnly, Category = "Pathfinding")
    TArray<int32> GridCells;

    // ========== Internal Helpers ==========
    FORCEINLINE int32 GetIndex(int32 X, int32 Y) const { return Y * GridWidth + X; }

    static int32 CalculateHeuristic(int32 x0, int32 y0, int32 x1, int32 y1, EGridHeuristic Mode);
    bool IsVisibleFromPoint(const FIntPoint& From, const FIntPoint& To) const;

    static FORCEINLINE int32 ToIndex(int32 X, int32 Y, int32 W) { return Y * W + X; }
    static FORCEINLINE bool InBounds(int32 X, int32 Y, int32 W, int32 H) { return X >= 0 && Y >= 0 && X < W && Y < H; }

    FIntPoint WorldToGridInternal(const FVector& W) const;
    FVector GridToWorldInternal(const FIntPoint& G, float Z) const;
};

