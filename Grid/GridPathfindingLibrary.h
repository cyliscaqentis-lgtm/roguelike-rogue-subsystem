// =============================================================================
// GridPathfindingLibrary.h
// ✅ 既存の全機能 + グリッド初期化確認関数 + Origin対応
// =============================================================================

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "../ProjectDiagnostics.h"
#include "GridPathfindingLibrary.generated.h"

UENUM(BlueprintType)
enum class EGridHeuristic : uint8
{
    Manhattan UMETA(DisplayName = "Manhattan", ToolTip = "4-directional movement (|dx| + |dy|)"),
    Euclidean UMETA(DisplayName = "Euclidean", ToolTip = "Straight-line distance (sqrt(dx² + dy²))"),
    MaxDXDY UMETA(DisplayName = "Chebyshev", ToolTip = "8-directional movement (max(|dx|, |dy|))")
};

// SpawnActor時に渡す初期化パラメータ
USTRUCT(BlueprintType)
struct FGridInitParams
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
    TArray<int32> GridCostArray;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
    FVector MapSize = FVector(50, 50, 0);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
    int32 TileSizeCM = 100;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
    FVector Origin = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct FGridVisionResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite)
    TArray<FIntPoint> VisibleTiles;

    UPROPERTY(BlueprintReadWrite)
    TArray<TObjectPtr<AActor>> VisibleActors;

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
    TArray<FIntPoint> EmptyTiles;

    UPROPERTY(BlueprintReadWrite)
    TArray<FIntPoint> BlockedTiles;
};

UCLASS(BlueprintType, Blueprintable)
class LYRAGAME_API AGridPathfindingLibrary : public AActor
{
    GENERATED_BODY()

public:

    // ★★★ スモークテスト関数の宣言（public セクションに追加）★★★
    UFUNCTION(Exec)
    void GridSmokeTest();

    AGridPathfindingLibrary();

    virtual void BeginPlay() override;

    /** SpawnActor時にExpose on Spawnで設定可能 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pathfinding|Setup", meta = (ExposeOnSpawn = "true"))
    FGridInitParams InitParams;

    /** 旧PathFinder互換: パス探索をリクエストしたコントローラー */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pathfinding")
    TObjectPtr<AController> RequestingController;

    // ==================== 初期化・設定 ====================

    /** FVectorでMapSizeを渡せる版（Z軸は無視） */
    UFUNCTION(BlueprintCallable, Category = "Pathfinding|Setup")
    void InitializeGrid(const TArray<int32>& InGridCost, const FVector& InMapSize, int32 InTileSizeCM = 100);

    UFUNCTION(BlueprintCallable, Category = "Pathfinding|Setup")
    void InitializeFromParams(const FGridInitParams& Params);

    UFUNCTION(BlueprintPure, Category = "Pathfinding|Setup")
    void GetGridInfo(int32& OutWidth, int32& OutHeight, int32& OutTileSize) const
    {
        OutWidth = GridWidth;
        OutHeight = GridHeight;
        OutTileSize = TileSize;
    }

    UFUNCTION(BlueprintPure, Category = "Pathfinding|Setup")
    FVector GetGridOrigin() const { return Origin; }

    FORCEINLINE float GetNavPlaneZ() const { return Origin.Z; }

    // ==================== 旧PathFinder互換エイリアス ====================

    /** 旧PathFinderのGridChange互換 */
    UFUNCTION(BlueprintCallable, Category = "Pathfinding|Setup", meta = (DisplayName = "Grid Change"))
    void GridChange(int32 X, int32 Y, int32 Value) { SetGridCost(X, Y, Value); }

    /** 地形の一時編集用。占有用途は非対応（OccupancySubsystem を使用） */
    UFUNCTION(BlueprintCallable, Category="Grid|Terrain")
    void GridChangeVector(const FVector& InputVector, int32 Value) { SetGridCostAtWorldPosition(InputVector, Value); }

    /** 地形コストの読み出し。占有は含まない（OccupancySubsystem を参照せよ） */
    UFUNCTION(BlueprintPure, Category="Grid|Terrain")
    int32 ReturnGridStatus(const FVector& InputVector) const;

    /** 旧PathFinderのVectorToGrid互換 */
    UFUNCTION(BlueprintPure, Category = "Pathfinding|Utility", meta = (DisplayName = "Vector To Grid"))
    FIntPoint VectorToGrid(const FVector& WorldPos) const { return WorldToGrid(WorldPos); }

    // ==================== 統合API（敵AI移動ブロック修正） ====================

    /** 統合された歩行可能性判定（地形+占有） */
    UFUNCTION(BlueprintCallable, Category = "Grid|Walkability")
    bool IsCellWalkable(const FIntPoint& Cell) const;

    UFUNCTION(BlueprintCallable, Category = "Grid|Walkability")
    bool IsCellWalkableIgnoringActor(const FIntPoint& Cell, AActor* IgnoreActor) const;

    /** FVector版の歩行可能性判定 */
    UFUNCTION(BlueprintCallable, Category = "Grid|Walkability")
    bool IsCellWalkableAtWorldPosition(const FVector& WorldPos) const;

    // ==================== 内部実装（新名前） ====================

    /** 地形専用：占有状態には絶対に使わないこと */
    UFUNCTION(BlueprintCallable, Category="Grid|Terrain")
    void SetGridCost(int32 X, int32 Y, int32 NewCost);

    UFUNCTION(BlueprintCallable, Category = "Pathfinding|Setup")
    void SetGridCostAtWorldPosition(const FVector& WorldPos, int32 NewCost);

    UFUNCTION(BlueprintPure, Category = "Pathfinding|Setup")
    int32 GetGridCost(int32 X, int32 Y) const;

    // ==================== パスファインディング ====================

    /** 標準のFindPath（開始/終了地点が-1ならNG） */
    UFUNCTION(BlueprintCallable, Category = "Pathfinding|PathFinding")
    bool FindPath(const FVector& StartWorld, const FVector& EndWorld, TArray<FVector>& OutWorldPath,
        bool bAllowDiagonal = true, EGridHeuristic Heuristic = EGridHeuristic::Manhattan,
        int32 SearchLimit = 200000, bool bHeavyDiagonal = true) const;

    /** キャラがいるマスでもパス探索可能（開始/終了地点の-1を無視） */
    UFUNCTION(BlueprintCallable, Category = "Pathfinding|PathFinding", meta = (DisplayName = "Find Path (Ignore Occupied)"))
    bool FindPathIgnoreEndpoints(const FVector& StartWorld, const FVector& EndWorld, TArray<FVector>& OutWorldPath,
        bool bAllowDiagonal = true, EGridHeuristic Heuristic = EGridHeuristic::Manhattan,
        int32 SearchLimit = 200000, bool bHeavyDiagonal = true) const;

    // ==================== 視野検知 (FOV) ====================

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

    // ==================== 周囲検索 ====================

    UFUNCTION(BlueprintCallable, Category = "Pathfinding|Detection")
    FGridSurroundResult SearchAdjacentTiles(
        const FVector& CenterWorld,
        bool bIncludeDiagonal = true,
        TSubclassOf<AActor> ActorClassFilter = nullptr) const;

    UFUNCTION(BlueprintCallable, Category = "Pathfinding|Detection")
    AActor* GetActorAtPosition(
        const FVector& WorldPos,
        float SearchRadius = -1.0f,
        TSubclassOf<AActor> ActorClassFilter = nullptr) const;

    UFUNCTION(BlueprintPure, Category = "Pathfinding|Detection")
    bool HasLineOfSight(const FVector& StartWorld, const FVector& EndWorld) const;

    // ==================== ユーティリティ ====================

    UFUNCTION(BlueprintPure, Category = "Pathfinding|Utility")
    FIntPoint WorldToGrid(const FVector& WorldPos) const;

    UFUNCTION(BlueprintPure, Category = "Pathfinding|Utility")
    FVector GridToWorld(const FIntPoint& GridPos, float Z = 0.f) const;

    UFUNCTION(BlueprintPure, Category = "Pathfinding|Utility")
    int32 GetManhattanDistance(const FVector& PosA, const FVector& PosB) const;

    //==========================================================================
    // ★★★ 新規追加：グリッド状態確認・拡張情報取得 ★★★
    //==========================================================================

    /** グリッドが初期化されているか確認 */
    UFUNCTION(BlueprintPure, Category = "Pathfinding|Utility")
    bool IsInitialized() const
    {
        return GridWidth > 0 && GridHeight > 0 && GridCells.Num() > 0;
    }

    /** グリッド情報取得（Origin付き拡張版） */
    UFUNCTION(BlueprintPure, Category = "Pathfinding|Utility")
    void GetGridInfoEx(int32& OutWidth, int32& OutHeight, int32& OutTileSize, FVector& OutOrigin) const
    {
        OutWidth = GridWidth;
        OutHeight = GridHeight;
        OutTileSize = TileSize;
        OutOrigin = Origin;
    }

    /** セル中心のワールド座標取得（Z座標指定版） */
    UFUNCTION(BlueprintPure, Category = "Pathfinding|Utility")
    FVector GridToWorldCenter(const FIntPoint& Cell, float Z = 0.0f) const
    {
        return FVector(
            Origin.X + (Cell.X + 0.5f) * TileSize,
            Origin.Y + (Cell.Y + 0.5f) * TileSize,
            Z
        );
    }

    /** 自己占有を無視したグリッドステータス取得 */
    UFUNCTION(BlueprintPure, Category = "Pathfinding|Utility")
    int32 ReturnGridStatusIgnoringSelf(const FVector& InputVector, AActor* IgnoreActor) const;

    //==========================================================================
    // ✅ 既存：マス単位距離計算（静的関数）
    //==========================================================================

    /** チェビシェフ距離（8方向移動の標準） */
    UFUNCTION(BlueprintPure, Category = "Grid|Distance", meta = (CompactNodeTitle = "Chebyshev"))
    static int32 GetChebyshevDistance(FIntPoint A, FIntPoint B);

    /** マンハッタン距離（4方向移動専用） */
    UFUNCTION(BlueprintPure, Category = "Grid|Distance", meta = (CompactNodeTitle = "Manhattan"))
    static int32 GetManhattanDistanceGrid(FIntPoint A, FIntPoint B);

    /** ユークリッド距離（参考用） */
    UFUNCTION(BlueprintPure, Category = "Grid|Distance", meta = (CompactNodeTitle = "Euclidean"))
    static int32 GetEuclideanDistanceGrid(FIntPoint A, FIntPoint B);

protected:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pathfinding")
    int32 GridWidth = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pathfinding")
    int32 GridHeight = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pathfinding", meta = (ClampMin = 1))
    int32 TileSize = 100;

    //==========================================================================
    // ★★★ 新規追加：Origin メンバー変数 ★★★
    //==========================================================================

    /** グリッドのワールド空間原点（WorldToGrid/GridToWorld の基準点） */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pathfinding")
    FVector Origin = FVector::ZeroVector;

    /** グリッドコスト配列（BP 互換性維持 - 監査は SetGridCost で実装） */
    UPROPERTY(BlueprintReadOnly, Category = "Pathfinding")
    TArray<int32> GridCells;

    // ==================== 監査モード用ヘルパ ====================

    UFUNCTION(Exec)
    void GridAuditEnable(int32 bEnable);

    UFUNCTION(Exec)
    void GridAuditProbe(int32 X, int32 Y);

    FORCEINLINE int32 GetIndex(int32 X, int32 Y) const { return Y * GridWidth + X; }

    static int32 CalculateHeuristic(int32 x0, int32 y0, int32 x1, int32 y1, EGridHeuristic Mode);
    bool IsVisibleFromPoint(const FIntPoint& From, const FIntPoint& To) const;
    void GetActorsAtGridPosition(const FIntPoint& GridPos, TSubclassOf<AActor> ClassFilter, TArray<AActor*>& OutActors) const;

    static FORCEINLINE int32 ToIndex(int32 X, int32 Y, int32 W) { return Y * W + X; }
    static FORCEINLINE bool InBounds(int32 X, int32 Y, int32 W, int32 H) { return X >= 0 && Y >= 0 && X < W && Y < H; }

    FIntPoint WorldToGridInternal(const FVector& W) const;
    FVector GridToWorldInternal(const FIntPoint& G, float Z) const;
};
