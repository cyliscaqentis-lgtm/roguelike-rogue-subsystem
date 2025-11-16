#include "Grid/GridPathfindingLibrary.h"
#include <queue>
#include "EngineUtils.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Controller.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/PlatformStackWalk.h"
#include "Misc/DateTime.h"
#include "Grid/GridOccupancySubsystem.h"
#include "Misc/ScopeLock.h"
#include "Grid/DungeonFloorGenerator.h"
#include "Utility/GridUtils.h"
#include "../ProjectDiagnostics.h"

// Normalize dungeon-generated cell values for navigation
namespace
{
    /** Normalize dungeon-generated cell values for navigation. */
    static int32 NormalizeDungeonCellValue(int32 RawValue)
    {
        if (RawValue < 0)
        {
            return RawValue;
        }

        if (RawValue == static_cast<int32>(ECellType::Wall))
        {
            return -1;
        }

        return RawValue;
    }
}

// CodeRevision: INC-2025-00021-R1 (Remove DoesCellContainBlockingActor - Phase 2.5) (2025-11-17 15:10)
// Removed: DoesCellContainBlockingActor() - only used by IsCellWalkable which is also removed

static bool GGridAuditEnabled = true;   // Enable audit mode for temporary debugging
static FCriticalSection GridAuditCS;    // Critical section for thread-safe audit logging

AGridPathfindingLibrary::AGridPathfindingLibrary()
{
    PrimaryActorTick.bCanEverTick = false;
    RequestingController = nullptr;
}

void AGridPathfindingLibrary::BeginPlay()
{
    Super::BeginPlay();

    // NOTE: Grid initialization is handled by GameTurnManager or external systems
    // InitializeFromParams() is called by external systems after BeginPlay
    
    if (GridWidth > 0 && GridHeight > 0 && GridCells.Num() > 0)
    {
        // Already initialized by GameTurnManager
        UE_LOG(LogGridPathfinding, Log, 
            TEXT("[GridPathfinding] BeginPlay: Already initialized (GridWidth=%d, GridHeight=%d, GridCells.Num()=%d, TileSize=%d, Origin=%s)"),
            GridWidth, GridHeight, GridCells.Num(), TileSize, *Origin.ToCompactString());
    }
    else
    {
        // Not initialized yet; waiting for external initialization (e.g., GameTurnManager)
        UE_LOG(LogGridPathfinding, Warning, 
            TEXT("[GridPathfinding] BeginPlay: Not initialized yet. Waiting for external initialization (e.g., GameTurnManager)."));
    }
}


// ==================== Grid Initialization ====================

void AGridPathfindingLibrary::InitializeGrid(const TArray<int32>& InGridCost, const FVector& InMapSize, int32 InTileSizeCM)
{
    GridWidth = FMath::Max(0, FMath::RoundToInt(InMapSize.X));
    GridHeight = FMath::Max(0, FMath::RoundToInt(InMapSize.Y));
    TileSize = FMath::Max(1, InTileSizeCM);

    // Initialize origin
    Origin = FVector::ZeroVector;

    const int32 Num = GridWidth * GridHeight;
    GridCells.Reset();
    GridCells.Reserve(Num);

    if (Num > 0 && InGridCost.Num() == Num)
    {
        for (int32 Value : InGridCost)
        {
            GridCells.Add(NormalizeDungeonCellValue(Value));
        }
    }
    else if (Num > 0)
    {
        GridCells.Init(0, Num);
    }

    UE_LOG(LogGridPathfinding, Log, TEXT("GridPathfinding蛻晁E��蛹・ %dx%d, 繧�E�繧�E�繝ｫ繧�E�繧�E�繧�E�=%dcm, Origin=%s"),
        GridWidth, GridHeight, TileSize, *Origin.ToCompactString());
}

void AGridPathfindingLibrary::InitializeFromParams(const FGridInitParams& Params)
{
    InitializeGrid(Params.GridCostArray, Params.MapSize, Params.TileSizeCM);

    // Set origin
    Origin = Params.Origin;

    UE_LOG(LogGridPathfinding, Log, TEXT("[GridPathfinding] Origin set to: %s"), *Origin.ToCompactString());
}

void AGridPathfindingLibrary::SetGridCost(int32 X, int32 Y, int32 Cost)
{
    if (!GGridAuditEnabled) { /* Audit mode disabled */ }

    // Input bounds check
    if (!InBounds(X, Y, GridWidth, GridHeight))
    {
        UE_LOG(LogGridPathfinding, Error,
            TEXT("[SetGridCost] Invalid cell: (%d,%d)"), X, Y);
        return;
    }

    const int32 Index = ToIndex(X, Y, GridWidth);

    // Store previous value for audit logging
    const int32 Before =
        (GridCells.IsValidIndex(Index) ? GridCells[Index] : INT32_MIN);

    // Lock to prevent race conditions in multi-threaded writes
    FScopeLock _lock(&GridAuditCS);

    UE_LOG(LogGridPathfinding, Warning,
        TEXT("[SetGridCost] Cell(%d,%d) Index=%d BEFORE=%d 竊�EAFTER=%d  (Time=%s)"),
        X, Y, Index, Before, Cost,
        *FDateTime::Now().ToString());

    if (GridCells.IsValidIndex(Index))
    {
        GridCells[Index] = Cost;

        // Verify write succeeded by reading back
        const int32 ReadBack = GridCells[Index];
        if (ReadBack != Cost)
        {
            UE_LOG(LogGridPathfinding, Error,
                TEXT("  笶・WRITE FAILED: Wrote %d but read %d at (%d,%d) Index=%d"),
                Cost, ReadBack, X, Y, Index);
        }

        // Detect suspicious wall-to-floor transitions (wall cost < 0, floor cost == 0)
        if (Before < 0 && Cost == 0)
        {
            UE_LOG(LogGridPathfinding, Error,
                TEXT("  ���圷 WALL竊�E DETECTED at Cell(%d,%d) Index=%d"), X, Y, Index);

            // Log call site information (function/file/line)
            UE_LOG(LogGridPathfinding, Error,
                TEXT("  CallSite: %s (%s:%d)"),
                ANSI_TO_TCHAR(__FUNCTION__), ANSI_TO_TCHAR(__FILE__), __LINE__);

            // Dump stack trace
            FDebug::DumpStackTraceToLog(ELogVerbosity::Error);

            // Test round-trip conversion: Grid -> World -> Grid
            const FVector TestWorld = GridToWorld(FIntPoint{X, Y});
            const FIntPoint RoundTrip = WorldToGrid(TestWorld);
            UE_LOG(LogGridPathfinding, Error,
                TEXT("  RoundTrip: Grid(%d,%d) -> World(%.1f,%.1f,%.1f) -> Grid(%d,%d)"),
                X, Y, TestWorld.X, TestWorld.Y, TestWorld.Z, RoundTrip.X, RoundTrip.Y);
        }
    }
    else
    {
        UE_LOG(LogGridPathfinding, Error,
            TEXT("  笶・GridCells.IsValidIndex(%d) = false! Size=%d"),
            Index, GridCells.Num());
    }
}

void AGridPathfindingLibrary::SetGridCostAtWorldPosition(const FVector& WorldPos, int32 NewCost)
{
    FIntPoint G = WorldToGridInternal(WorldPos);
    SetGridCost(G.X, G.Y, NewCost);
}

int32 AGridPathfindingLibrary::GetGridCost(int32 X, int32 Y) const
{
    if (InBounds(X, Y, GridWidth, GridHeight))
    {
        return GridCells[ToIndex(X, Y, GridWidth)];
    }
    return -1;
}

int32 AGridPathfindingLibrary::ReturnGridStatus(const FVector& InputVector) const
{
    FIntPoint GridPos = WorldToGridInternal(InputVector);
    int32 Cost = GetGridCost(GridPos.X, GridPos.Y);

    // Log level adaptation: Walkable=VeryVerbose, Blocked=Warning, Invalid=Error
    if (Cost >= 0)
    {
        UE_LOG(LogGridPathfinding, VeryVerbose,
            TEXT("[ReturnGridStatus] Walkable World:%s -> Grid:(%d,%d) Cost:%d"),
            *InputVector.ToCompactString(), GridPos.X, GridPos.Y, Cost);
    }
    else if (Cost == -1)
    {
        UE_LOG(LogGridPathfinding, Warning,
            TEXT("[ReturnGridStatus] Blocked World:%s -> Grid:(%d,%d) Cost:%d"),
            *InputVector.ToCompactString(), GridPos.X, GridPos.Y, Cost);
    }
    else
    {
        UE_LOG(LogGridPathfinding, Error,
            TEXT("[ReturnGridStatus] INVALID World:%s -> Grid:(%d,%d) Cost:%d (W=%d H=%d GridCells.Num=%d)"),
            *InputVector.ToCompactString(), GridPos.X, GridPos.Y, Cost, GridWidth, GridHeight, GridCells.Num());
    }

    return Cost;
}


// ==================== Pathfinding Functions ====================

int32 AGridPathfindingLibrary::CalculateHeuristic(int32 x0, int32 y0, int32 x1, int32 y1, EGridHeuristic Mode)
{
    const int32 dx = FMath::Abs(x0 - x1);
    const int32 dy = FMath::Abs(y0 - y1);

    switch (Mode)
    {
    case EGridHeuristic::Euclidean:
        return FMath::RoundToInt(10.f * FMath::Sqrt(float(dx * dx + dy * dy)));
    case EGridHeuristic::MaxDXDY:
        return 10 * FMath::Max(dx, dy);
    default:
        return 10 * (dx + dy);
    }
}

    static bool CanTraverseDelta(int32 GridWidth, int32 GridHeight, const TArray<int32>& Grid, int32 X, int32 Y, const FIntPoint& Delta)
    {
        const int32 nx = X + Delta.X;
        const int32 ny = Y + Delta.Y;

        if (nx < 0 || ny < 0 || nx >= GridWidth || ny >= GridHeight)
        {
            return false;
        }

        const int32 nid = ny * GridWidth + nx;
        if (!Grid.IsValidIndex(nid) || Grid[nid] < 0)
        {
            return false;
        }

        if (Delta.X != 0 && Delta.Y != 0)
        {
            const int32 adjXId = Y * GridWidth + (X + Delta.X);
            const int32 adjYId = (Y + Delta.Y) * GridWidth + X;
            if (!Grid.IsValidIndex(adjXId) || !Grid.IsValidIndex(adjYId))
            {
                return false;
            }
            if (Grid[adjXId] < 0 || Grid[adjYId] < 0)
        {
            return false;
        }
    }

    return true;
}

bool AGridPathfindingLibrary::FindPath(
    const FVector& StartWorld,
    const FVector& EndWorld,
    TArray<FVector>& OutWorldPath,
    bool bAllowDiagonal,
    EGridHeuristic Heuristic,
    int32 SearchLimit,
    bool bHeavyDiagonal) const
{
    OutWorldPath.Reset();

    const int32 Num = GridWidth * GridHeight;
    if (Num == 0 || GridCells.Num() != Num)
        return false;

    const FIntPoint S = WorldToGridInternal(StartWorld);
    const FIntPoint E = WorldToGridInternal(EndWorld);

    if (!InBounds(S.X, S.Y, GridWidth, GridHeight) ||
        !InBounds(E.X, E.Y, GridWidth, GridHeight))
        return false;

    const int32 StartId = ToIndex(S.X, S.Y, GridWidth);
    const int32 EndId = ToIndex(E.X, E.Y, GridWidth);

    if (GridCells[StartId] < 0 || GridCells[EndId] < 0)
        return false;

    TArray<int32> G;
    G.Init(INT_MAX, Num);
    TArray<int32> P;
    P.Init(-1, Num);
    TBitArray<> Closed(false, Num);

    struct FNode { int32 Id; int32 F; };
    struct FCompare { bool operator()(const FNode& a, const FNode& b) const { return a.F > b.F; } };
    std::priority_queue<FNode, std::vector<FNode>, FCompare> Open;

    G[StartId] = 0;
    const int32 H = CalculateHeuristic(S.X, S.Y, E.X, E.Y, Heuristic);
    Open.push({ StartId, H });

    TArray<FIntPoint> Directions = { {1,0}, {-1,0}, {0,1}, {0,-1} };
    if (bAllowDiagonal)
    {
        Directions.Append({ {1,1}, {1,-1}, {-1,1}, {-1,-1} });
    }

    auto CanTraverse = [&](int32 X, int32 Y, const FIntPoint& Delta) -> bool
    {
        const int32 nx = X + Delta.X;
        const int32 ny = Y + Delta.Y;

        if (!InBounds(nx, ny, GridWidth, GridHeight))
        {
            return false;
        }

        const int32 nid = ToIndex(nx, ny, GridWidth);
        if (GridCells[nid] < 0)
        {
            return false;
        }

        if (Delta.X != 0 && Delta.Y != 0)
        {
            const int32 adjX = ToIndex(X + Delta.X, Y, GridWidth);
            const int32 adjY = ToIndex(X, Y + Delta.Y, GridWidth);
            if (!InBounds(X + Delta.X, Y, GridWidth, GridHeight) || GridCells[adjX] < 0)
            {
                return false;
            }
            if (!InBounds(X, Y + Delta.Y, GridWidth, GridHeight) || GridCells[adjY] < 0)
            {
                return false;
            }
        }

        return true;
    };

    int32 Expanded = 0;
    while (!Open.empty())
    {
        if (++Expanded > SearchLimit)
            return false;

        const int32 Cur = Open.top().Id;
        Open.pop();

        if (Closed[Cur])
            continue;
        Closed[Cur] = true;

        if (Cur == EndId)
        {
            TArray<int32> Chain;
            for (int32 n = Cur; n != -1; n = P[n])
                Chain.Add(n);

            for (int32 i = Chain.Num() - 1; i >= 0; --i)
            {
                const int32 id = Chain[i];
                const FIntPoint GridPos(id % GridWidth, id / GridWidth);
                OutWorldPath.Add(GridToWorldInternal(GridPos, StartWorld.Z));
            }
            return true;
        }

        const int32 cx = Cur % GridWidth;
        const int32 cy = Cur / GridWidth;

        for (const FIntPoint& d : Directions)
        {
            if (!CanTraverseDelta(GridWidth, GridHeight, GridCells, cx, cy, d))
            {
                continue;
            }

            const int32 nx = cx + d.X;
            const int32 ny = cy + d.Y;
            const int32 nid = ToIndex(nx, ny, GridWidth);

            if (Closed[nid])
                continue;

            const bool bDiag = (d.X != 0 && d.Y != 0);
            const int32 step = (bDiag && bHeavyDiagonal) ? 14 : 10;
            const int32 terrain = FMath::Max(0, GridCells[nid]);
            const int32 gNew = (G[Cur] == INT_MAX) ? INT_MAX : (G[Cur] + step + terrain);

            if (gNew < G[nid])
            {
                G[nid] = gNew;
                P[nid] = Cur;
                const int32 h = CalculateHeuristic(nx, ny, E.X, E.Y, Heuristic);
                Open.push({ nid, gNew + h });
            }
        }
    }

    return false;
}

bool AGridPathfindingLibrary::FindPathIgnoreEndpoints(
    const FVector& StartWorld,
    const FVector& EndWorld,
    TArray<FVector>& OutWorldPath,
    bool bAllowDiagonal,
    EGridHeuristic Heuristic,
    int32 SearchLimit,
    bool bHeavyDiagonal) const
{
    OutWorldPath.Reset();

    const int32 Num = GridWidth * GridHeight;
    if (Num == 0 || GridCells.Num() != Num)
        return false;

    const FIntPoint S = WorldToGridInternal(StartWorld);
    const FIntPoint E = WorldToGridInternal(EndWorld);

    if (!InBounds(S.X, S.Y, GridWidth, GridHeight) ||
        !InBounds(E.X, E.Y, GridWidth, GridHeight))
        return false;

    const int32 StartId = ToIndex(S.X, S.Y, GridWidth);
    const int32 EndId = ToIndex(E.X, E.Y, GridWidth);

    const int32 OriginalStartCost = GridCells[StartId];
    const int32 OriginalEndCost = GridCells[EndId];

    TArray<int32>& MutableGrid = const_cast<TArray<int32>&>(GridCells);
    MutableGrid[StartId] = 0;
    MutableGrid[EndId] = 0;

    TArray<int32> G;
    G.Init(INT_MAX, Num);
    TArray<int32> P;
    P.Init(-1, Num);
    TBitArray<> Closed(false, Num);

    struct FNode { int32 Id; int32 F; };
    struct FCompare { bool operator()(const FNode& a, const FNode& b) const { return a.F > b.F; } };
    std::priority_queue<FNode, std::vector<FNode>, FCompare> Open;

    G[StartId] = 0;
    const int32 H = CalculateHeuristic(S.X, S.Y, E.X, E.Y, Heuristic);
    Open.push({ StartId, H });

    TArray<FIntPoint> Directions = { {1,0}, {-1,0}, {0,1}, {0,-1} };
    if (bAllowDiagonal)
    {
        Directions.Append({ {1,1}, {1,-1}, {-1,1}, {-1,-1} });
    }

    bool bResult = false;
    int32 Expanded = 0;

    while (!Open.empty())
    {
        if (++Expanded > SearchLimit)
            break;

        const int32 Cur = Open.top().Id;
        Open.pop();

        if (Closed[Cur])
            continue;
        Closed[Cur] = true;

        if (Cur == EndId)
        {
            TArray<int32> Chain;
            for (int32 n = Cur; n != -1; n = P[n])
                Chain.Add(n);

            for (int32 i = Chain.Num() - 1; i >= 0; --i)
            {
                const int32 id = Chain[i];
                const FIntPoint GridPos(id % GridWidth, id / GridWidth);
                OutWorldPath.Add(GridToWorldInternal(GridPos, StartWorld.Z));
            }
            bResult = true;
            break;
        }

        const int32 cx = Cur % GridWidth;
        const int32 cy = Cur / GridWidth;

        for (const FIntPoint& d : Directions)
        {
            if (!CanTraverseDelta(GridWidth, GridHeight, MutableGrid, cx, cy, d))
            {
                continue;
            }

            const int32 nx = cx + d.X;
            const int32 ny = cy + d.Y;
            const int32 nid = ToIndex(nx, ny, GridWidth);

            if (Closed[nid])
                continue;

            const bool bDiag = (d.X != 0 && d.Y != 0);
            const int32 step = (bDiag && bHeavyDiagonal) ? 14 : 10;
            const int32 terrain = FMath::Max(0, GridCells[nid]);
            const int32 gNew = (G[Cur] == INT_MAX) ? INT_MAX : (G[Cur] + step + terrain);

            if (gNew < G[nid])
            {
                G[nid] = gNew;
                P[nid] = Cur;
                const int32 h = CalculateHeuristic(nx, ny, E.X, E.Y, Heuristic);
                Open.push({ nid, gNew + h });
            }
        }
    }

    MutableGrid[StartId] = OriginalStartCost;
    MutableGrid[EndId] = OriginalEndCost;

    return bResult;
}

// ==================== Vision Detection ====================

FGridVisionResult AGridPathfindingLibrary::DetectInExpandingVision(
    const FVector& CenterWorld,
    const FVector& ForwardDirection,
    int32 MaxDepth,
    TSubclassOf<AActor> ActorClassFilter) const
{
    FGridVisionResult Result;

    if (GridWidth == 0 || GridHeight == 0)
        return Result;

    const FIntPoint Center = WorldToGridInternal(CenterWorld);
    const FVector2D Dir2D = FVector2D(ForwardDirection.X, ForwardDirection.Y).GetSafeNormal();

    TSet<FIntPoint> VisitedTiles;

    for (int32 Depth = 1; Depth <= MaxDepth; ++Depth)
    {
        const int32 BaseWidth = 3;
        const int32 CurrentWidth = BaseWidth + (Depth - 1) * 2;

        for (int32 w = -CurrentWidth / 2; w <= CurrentWidth / 2; ++w)
        {
            FVector2D Perpendicular(-Dir2D.Y, Dir2D.X);
            FVector2D TargetOffset = Dir2D * Depth + Perpendicular * w;

            FIntPoint TargetGrid(
                Center.X + FMath::RoundToInt(TargetOffset.X),
                Center.Y + FMath::RoundToInt(TargetOffset.Y)
            );

            if (!InBounds(TargetGrid.X, TargetGrid.Y, GridWidth, GridHeight))
                continue;

            if (VisitedTiles.Contains(TargetGrid))
                continue;

            if (!IsVisibleFromPoint(Center, TargetGrid))
                continue;

            VisitedTiles.Add(TargetGrid);
            Result.VisibleTiles.Add(TargetGrid);

            // CodeRevision: INC-2025-00021-R1 (Replace GetActorsAtGridPosition with direct UGridOccupancySubsystem access - Phase 3.1) (2025-11-17 15:15)
            // Direct access to UGridOccupancySubsystem without fallback
            if (UWorld* World = GetWorld())
            {
                if (UGridOccupancySubsystem* Occupancy = World->GetSubsystem<UGridOccupancySubsystem>())
                {
                    if (AActor* Occupant = Occupancy->GetActorAtCell(TargetGrid))
                    {
                        if (!ActorClassFilter || Occupant->IsA(ActorClassFilter))
                        {
                            Result.VisibleActors.Add(Occupant);
                        }
                    }
                    if (AActor* Reserved = Occupancy->GetReservationOwner(TargetGrid))
                    {
                        if (Reserved && (!ActorClassFilter || Reserved->IsA(ActorClassFilter)))
                        {
                            Result.VisibleActors.AddUnique(Reserved);
                        }
                    }
                }
            }
        }
    }

    Result.TotalTilesScanned = VisitedTiles.Num();
    return Result;
}

FGridVisionResult AGridPathfindingLibrary::DetectInRadius(
    const FVector& CenterWorld,
    int32 Radius,
    bool bCheckLineOfSight,
    TSubclassOf<AActor> ActorClassFilter) const
{
    FGridVisionResult Result;

    if (GridWidth == 0 || GridHeight == 0)
        return Result;

    const FIntPoint Center = WorldToGridInternal(CenterWorld);

    for (int32 dy = -Radius; dy <= Radius; ++dy)
    {
        for (int32 dx = -Radius; dx <= Radius; ++dx)
        {
            const int32 tx = Center.X + dx;
            const int32 ty = Center.Y + dy;

            if (!InBounds(tx, ty, GridWidth, GridHeight))
                continue;

            const int32 dist = FMath::Abs(dx) + FMath::Abs(dy);
            if (dist > Radius)
                continue;

            FIntPoint TargetGrid(tx, ty);

            if (bCheckLineOfSight && !IsVisibleFromPoint(Center, TargetGrid))
                continue;

            Result.VisibleTiles.Add(TargetGrid);

            // CodeRevision: INC-2025-00021-R1 (Replace GetActorsAtGridPosition with direct UGridOccupancySubsystem access - Phase 3.2) (2025-11-17 15:15)
            // Direct access to UGridOccupancySubsystem without fallback
            if (UWorld* World = GetWorld())
            {
                if (UGridOccupancySubsystem* Occupancy = World->GetSubsystem<UGridOccupancySubsystem>())
                {
                    if (AActor* Occupant = Occupancy->GetActorAtCell(TargetGrid))
                    {
                        if (!ActorClassFilter || Occupant->IsA(ActorClassFilter))
                        {
                            Result.VisibleActors.Add(Occupant);
                        }
                    }
                    if (AActor* Reserved = Occupancy->GetReservationOwner(TargetGrid))
                    {
                        if (Reserved && (!ActorClassFilter || Reserved->IsA(ActorClassFilter)))
                        {
                            Result.VisibleActors.AddUnique(Reserved);
                        }
                    }
                }
            }
        }
    }

    Result.TotalTilesScanned = Result.VisibleTiles.Num();
    return Result;
}

// ==================== Adjacent Tile Search ====================

FGridSurroundResult AGridPathfindingLibrary::SearchAdjacentTiles(
    const FVector& CenterWorld,
    bool bIncludeDiagonal,
    TSubclassOf<AActor> ActorClassFilter) const
{
    FGridSurroundResult Result;

    if (GridWidth == 0 || GridHeight == 0)
        return Result;

    const FIntPoint Center = WorldToGridInternal(CenterWorld);

    TArray<FIntPoint> Directions = { {1,0}, {-1,0}, {0,1}, {0,-1} };
    if (bIncludeDiagonal)
    {
        Directions.Append({ {1,1}, {1,-1}, {-1,1}, {-1,-1} });
    }

    for (const FIntPoint& Dir : Directions)
    {
        const FIntPoint Target(Center.X + Dir.X, Center.Y + Dir.Y);

        if (!InBounds(Target.X, Target.Y, GridWidth, GridHeight))
            continue;

        const int32 Cost = GridCells[ToIndex(Target.X, Target.Y, GridWidth)];

        if (Cost < 0)
        {
            Result.BlockedTiles.Add(Target);
        }
        else
        {
            // CodeRevision: INC-2025-00021-R1 (Replace GetActorsAtGridPosition with direct UGridOccupancySubsystem access - Phase 3.3) (2025-11-17 15:15)
            // Direct access to UGridOccupancySubsystem without fallback
            bool bFoundActor = false;
            if (UWorld* World = GetWorld())
            {
                if (UGridOccupancySubsystem* Occupancy = World->GetSubsystem<UGridOccupancySubsystem>())
                {
                    if (AActor* Occupant = Occupancy->GetActorAtCell(Target))
                    {
                        if (!ActorClassFilter || Occupant->IsA(ActorClassFilter))
                        {
                            Result.AdjacentActors.Add(Occupant);
                            bFoundActor = true;
                        }
                    }
                    if (AActor* Reserved = Occupancy->GetReservationOwner(Target))
                    {
                        if (Reserved && (!ActorClassFilter || Reserved->IsA(ActorClassFilter)))
                        {
                            Result.AdjacentActors.AddUnique(Reserved);
                            bFoundActor = true;
                        }
                    }
                }
            }
            if (!bFoundActor)
            {
                Result.EmptyTiles.Add(Target);
            }
        }
    }

    return Result;
}

// CodeRevision: INC-2025-00021-R1 (Remove GetActorAtPosition - Phase 3.4) (2025-11-17 15:20)
// Removed: GetActorAtPosition() - use UGridOccupancySubsystem::GetActorAtCell directly

bool AGridPathfindingLibrary::HasLineOfSight(const FVector& StartWorld, const FVector& EndWorld) const
{
    const FIntPoint Start = WorldToGridInternal(StartWorld);
    const FIntPoint End = WorldToGridInternal(EndWorld);
    return IsVisibleFromPoint(Start, End);
}

// ==================== Utility Functions ====================

FIntPoint AGridPathfindingLibrary::WorldToGrid(const FVector& WorldPos) const
{
    return WorldToGridInternal(WorldPos);
}

FVector AGridPathfindingLibrary::GridToWorld(const FIntPoint& GridPos, float Z) const
{
    return GridToWorldInternal(GridPos, Z);
}

int32 AGridPathfindingLibrary::GetManhattanDistance(const FVector& PosA, const FVector& PosB) const
{
    const FIntPoint A = WorldToGridInternal(PosA);
    const FIntPoint B = WorldToGridInternal(PosB);
    // ★★★ Optimization: Use GridUtils (duplicate code removed 2025-11-09)
    return FGridUtils::ManhattanDistance(A, B);
}

// ==================== Internal Helpers ====================

FIntPoint AGridPathfindingLibrary::WorldToGridInternal(const FVector& W) const
{
    return FIntPoint(
        FMath::FloorToInt((W.X - Origin.X) / TileSize),
        FMath::FloorToInt((W.Y - Origin.Y) / TileSize)
    );
}

FVector AGridPathfindingLibrary::GridToWorldInternal(const FIntPoint& G, float Z) const
{
    return FVector(
        Origin.X + (G.X + 0.5f) * TileSize,
        Origin.Y + (G.Y + 0.5f) * TileSize,
        Z
    );
}

bool AGridPathfindingLibrary::IsVisibleFromPoint(const FIntPoint& From, const FIntPoint& To) const
{
    int32 x0 = From.X, y0 = From.Y;
    int32 x1 = To.X, y1 = To.Y;

    const int32 dx = FMath::Abs(x1 - x0);
    const int32 dy = FMath::Abs(y1 - y0);
    const int32 sx = (x0 < x1) ? 1 : -1;
    const int32 sy = (y0 < y1) ? 1 : -1;
    int32 err = dx - dy;

    while (true)
    {
        if (x0 == x1 && y0 == y1)
            break;

        if (!InBounds(x0, y0, GridWidth, GridHeight))
            return false;

        const int32 cost = GridCells[ToIndex(x0, y0, GridWidth)];
        if (cost < 0)
            return false;

        const int32 e2 = 2 * err;
        if (e2 > -dy)
        {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx)
        {
            err += dx;
            y0 += sy;
        }
    }

    return true;
}

// CodeRevision: INC-2025-00021-R1 (Remove GetActorsAtGridPosition - Phase 3.5) (2025-11-17 15:25)
// Removed: GetActorsAtGridPosition() - use UGridOccupancySubsystem::GetActorAtCell directly

// ==================== Static Distance Functions ====================

// CodeRevision: INC-2025-00021-R1 (Remove static distance functions - Phase 4.1) (2025-11-17 15:35)
// Removed: GetChebyshevDistance(), GetManhattanDistanceGrid(), GetEuclideanDistanceGrid()
// Use FGridUtils::ChebyshevDistance, FGridUtils::ManhattanDistance, FGridUtils::EuclideanDistance directly

// ==================== Grid Status Query ====================

// CodeRevision: INC-2025-00021-R1 (Remove ReturnGridStatusIgnoringSelf - Phase 3.6) (2025-11-17 15:30)
// Removed: ReturnGridStatusIgnoringSelf() - unused function

// ==================== Debug/Smoke Test ====================

void AGridPathfindingLibrary::GridSmokeTest()
{
    const int32 W = GridWidth, H = GridHeight;
    UE_LOG(LogGridPathfinding, Warning, TEXT("[Smoke] Size=%dx%d GridCells=%d Tile=%d Origin=%s"),
        W, H, GridCells.Num(), TileSize, *Origin.ToCompactString());

    if (UWorld* World = GetWorld())
    {
        if (APlayerController* PC = World->GetFirstPlayerController())
        {
            if (APawn* P = PC->GetPawn())
            {
                const FVector L = P->GetActorLocation();
                const int32 Cost = ReturnGridStatus(L);
                UE_LOG(LogGridPathfinding, Warning, TEXT("[Smoke] Player@%s -> Cost=%d"),
                    *L.ToCompactString(), Cost);
            }
            else
            {
                UE_LOG(LogGridPathfinding, Warning, TEXT("[Smoke] No Pawn possessed"));
            }
        }
        else
        {
            UE_LOG(LogGridPathfinding, Warning, TEXT("[Smoke] No PlayerController"));
        }
    }
}

// ==================== Audit Mode Commands ====================

void AGridPathfindingLibrary::GridAuditEnable(int32 bEnable)
{
    GGridAuditEnabled = (bEnable != 0);
    UE_LOG(LogGridPathfinding, Warning, TEXT("[GridAudit] %s"),
        GGridAuditEnabled ? TEXT("ENABLED") : TEXT("DISABLED"));
}

void AGridPathfindingLibrary::GridAuditProbe(int32 X, int32 Y)
{
    const int32 Cost = GetGridCost(X, Y);
    const FVector W = GridToWorld({X, Y});
    UE_LOG(LogGridPathfinding, Warning,
        TEXT("[GridAuditProbe] Cell(%d,%d) Cost=%d  World(%.1f,%.1f,%.1f)"),
        X, Y, Cost, W.X, W.Y, W.Z);
    FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
}


// ==================== Unified Movement Validation API for AI Blocking ====================

// CodeRevision: INC-2025-00021-R1 (Remove IsCellWalkable - Phase 2.5) (2025-11-17 15:10)
// Removed: IsCellWalkable() - replaced with IsCellWalkableIgnoringActor + UGridOccupancySubsystem::IsCellOccupied

bool AGridPathfindingLibrary::IsCellWalkableIgnoringActor(const FIntPoint& Cell, AActor* IgnoreActor) const
{
    // ★★★ CRITICAL FIX (2025-11-11): Truly ignore Actor (check terrain only) ★★★
    // As the method name suggests, check terrain only without checking occupancy
    const int32 TerrainCost = GetGridCost(Cell.X, Cell.Y);
    if (TerrainCost < 0)
    {
        return false;  // Terrain is not walkable
    }

    // Do not check occupancy (caller checks separately)
    return true;
}

// CodeRevision: INC-2025-00021-R1 (Remove IsCellWalkableAtWorldPosition - Phase 1.1) (2025-11-17 15:00)
// Removed: IsCellWalkableAtWorldPosition() - unused wrapper function

//==========================================================================
// ★★★ New Addition: Unified Movement Validation API Implementation ★★★
// CodeRevision: INC-2025-00015-R1 (Unified validation API) (2025-11-16 13:55)
//==========================================================================

bool AGridPathfindingLibrary::IsMoveValid(
    const FIntPoint& From,
    const FIntPoint& To,
    AActor* MovingActor,
    FString& OutFailureReason) const
{
    OutFailureReason.Empty();

    // 1. Chebyshev distance check (only 1 cell movement allowed)
    const int32 ChebyshevDist = FGridUtils::ChebyshevDistance(From, To);
    if (ChebyshevDist != 1)
    {
        OutFailureReason = FString::Printf(
            TEXT("Invalid distance: Chebyshev distance is %d (must be 1)"),
            ChebyshevDist);
        return false;
    }

    // 2. Terrain check
    if (!IsCellWalkableIgnoringActor(To, MovingActor))
    {
        OutFailureReason = TEXT("Target cell terrain is not walkable");
        return false;
    }

    // 3. Corner cutting check (diagonal moves only)
    const int32 DeltaX = To.X - From.X;
    const int32 DeltaY = To.Y - From.Y;
    const bool bIsDiagonalMove = (FMath::Abs(DeltaX) == 1 && FMath::Abs(DeltaY) == 1);

    if (bIsDiagonalMove)
    {
        const FIntPoint Side1 = From + FIntPoint(DeltaX, 0);
        const FIntPoint Side2 = From + FIntPoint(0, DeltaY);
        const bool bSide1Walkable = IsCellWalkableIgnoringActor(Side1, MovingActor);
        const bool bSide2Walkable = IsCellWalkableIgnoringActor(Side2, MovingActor);

        if (!bSide1Walkable || !bSide2Walkable)
        {
            OutFailureReason = FString::Printf(
                TEXT("Corner cutting blocked: Side1(%d,%d)=%d Side2(%d,%d)=%d"),
                Side1.X, Side1.Y, bSide1Walkable ? 1 : 0,
                Side2.X, Side2.Y, bSide2Walkable ? 1 : 0);
            return false;
        }
    }

    // 4. Occupancy check (via GridOccupancySubsystem)
    if (UWorld* World = GetWorld())
    {
        if (UGridOccupancySubsystem* OccSys = World->GetSubsystem<UGridOccupancySubsystem>())
        {
            AActor* Occupant = OccSys->GetActorAtCell(To);
            if (Occupant && Occupant != MovingActor)
            {
                OutFailureReason = FString::Printf(
                    TEXT("Cell occupied by %s"),
                    *GetNameSafe(Occupant));
                return false;
            }

            // Reservation check (ignore own reservations)
            if (OccSys->IsCellReserved(To))
            {
                if (!OccSys->IsReservationOwnedByActor(MovingActor, To))
                {
                    OutFailureReason = TEXT("Cell reserved by another actor");
                    return false;
                }
            }
        }
    }

    // All checks passed
    return true;
}







