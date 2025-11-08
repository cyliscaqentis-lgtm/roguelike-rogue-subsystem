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
#include "Grid/GridOccupancySubsystem.h"  // Á¨ò„ÅE„ÉªÁ¨ò„ÅEIsCellWalkableÈÄïÔΩ® Á¨ò„ÅE„ÉªÁ¨ò„ÅE
#include "Misc/ScopeLock.h"
#include "Grid/DungeonFloorGenerator.h"
#include "../ProjectDiagnostics.h"

// Á¨ò„ÅE„ÉªÁ¨ò„ÅEÈÄ∂ÅE£Ë≠üÔΩªÁπùÔΩ¢ÁπùÔΩºÁπùËÅEÁïëÁπßÅE∞ÁπùÔΩ≠ÁπùÔΩºÁπùËÅEŒùËûüÁîªÁÑÅEÁ¨ò„ÅE„ÉªÁ¨ò„ÅE
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

static bool DoesCellContainBlockingActor(const UGridOccupancySubsystem* Occupancy, const FIntPoint& Cell, AActor* IgnoreActor)
{
    if (!Occupancy)
    {
        return false;
    }

    if (AActor* Occupant = Occupancy->GetActorAtCell(Cell))
    {
        if (!IgnoreActor || Occupant != IgnoreActor)
        {
            return true;
        }
    }

    if (Occupancy->IsCellReserved(Cell))
    {
        if (!IgnoreActor || !Occupancy->IsReservationOwnedByActor(IgnoreActor, Cell))
        {
            return true;
        }
    }

    return false;
}

static bool GGridAuditEnabled = true;   // Ë†¢„ÉªÅE¶‚àö‚ÅEË†¢Êáä¬ßÁ∏∫ÅE¶Ëç≥¬ÄË≠é„Çâ‚îåËúâÅEπËõπÈñÄÂ∫ÅE
static FCriticalSection GridAuditCS;    // Ëç≥ÅE¶Èô¶Ê¢ßÂ∂åÁ∏∫Â¥éÔΩæÅEºÁ∏∫ÅEøÁ∏∫Âæå‚â†ÁπßÂæå„ÅEËÆÄÊáÅE°çÁ∏∫ÅEÆÁ∏∫Ê∫òÔΩÅE

AGridPathfindingLibrary::AGridPathfindingLibrary()
{
    PrimaryActorTick.bCanEverTick = false;
    RequestingController = nullptr;
}

void AGridPathfindingLibrary::BeginPlay()
{
    Super::BeginPlay();

    // Á¨ò„ÅE„ÉªÁ¨ò„ÅEÈ©•Â¥éÔΩ¶„ÉªÅEºÂ§ä„ÅEËúçÂãü„ÉªË≠õÊ∫∑ÂñßÁπßÂ†§‚îåËúâÅEπËõπÂéÅEΩº„ÉªameTurnManagerÁ∏∫Ê¢ß„ÉªÈÅâÔΩ∫ÈÄß„Éª‚ÜìËõªÊôÅEÇÑËõπÊÇ∂‚òÅEπßÂÖ∑ÅEº„ÉªÁ¨ò„ÅE„ÉªÁ¨ò„ÅE
    // BeginPlayÁ∏∫ÅEßÁ∏∫ÅEÆÈñæÅE™ËúçÂãü„ÉªË≠õÊ∫∑ÂñßÁ∏∫ÅEØËúë‰ºÅÂãÅÁ∏∫Âä±¬Ä‚àùÔΩ§Â§úŒöÁ∏∫‰πùÔΩâÁ∏∫ÅEÆInitializeFromParams()ËúªÅEºÁ∏∫ÅE≥ËúÅEΩ∫Á∏∫Âä±„ÉªÁ∏∫ÅEøÁπßÂÆöÔΩ®ÅE±ËúøÅEØ
    
    if (GridWidth > 0 && GridHeight > 0 && mGrid.Num() > 0)
    {
        // Ë≠åÔΩ¢Á∏∫ÅE´ËõªÊôÅEÇÑËõπÂåÅEΩ∏Âåª‚à©„Éª„ÉªameTurnManagerÁ∏∫‰πùÔΩâÁ∏∫ÅEÆËúªÅEºÁ∏∫ÅE≥ËúÅEΩ∫Á∏∫Êö¶ÅEº„Éª
        UE_LOG(LogGridPathfinding, Log, 
            TEXT("[GridPathfinding] BeginPlay: Already initialized (GridWidth=%d, GridHeight=%d, mGrid.Num()=%d, TileSize=%d, Origin=%s)"),
            GridWidth, GridHeight, mGrid.Num(), TileSize, *Origin.ToCompactString());
    }
    else
    {
        // Á∏∫ÅEæÁ∏∫ÅE∞ËõªÊôÅEÇÑËõπÊÇ∂ÅEÅEπßÂæå‚ÄªÁ∏∫„Éª‚ÜëÁ∏∫„ÉªÅEº„ÉªameTurnManagerÁ∏∫ÊÇüÔΩæÂæå„ÄíËõªÊôÅEÇÑËõπÊÇ∂‚òÅEπßÂÖ∑ÅEº„Éª
        UE_LOG(LogGridPathfinding, Warning, 
            TEXT("[GridPathfinding] BeginPlay: Not initialized yet. Waiting for external initialization (e.g., GameTurnManager)."));
    }
}


// ==================== ËõªÊôÅEÇÑËõπÊÇ∂„ÉªÈö™ÅE≠Ëû≥„Éª====================

void AGridPathfindingLibrary::InitializeGrid(const TArray<int32>& InGridCost, const FVector& InMapSize, int32 InTileSizeCM)
{
    GridWidth = FMath::Max(0, FMath::RoundToInt(InMapSize.X));
    GridHeight = FMath::Max(0, FMath::RoundToInt(InMapSize.Y));
    TileSize = FMath::Max(1, InTileSizeCM);

    // Á¨ò„ÅE„ÉªÁ¨ò„ÅEOrigin Á∏∫ÅEÆËõªÊôÅEÇÑËõπ„ÉªÁ¨ò„ÅE„ÉªÁ¨ò„ÅE
    Origin = FVector::ZeroVector;

    const int32 Num = GridWidth * GridHeight;
    mGrid.Reset();
    mGrid.Reserve(Num);

    if (Num > 0 && InGridCost.Num() == Num)
    {
        for (int32 Value : InGridCost)
        {
            mGrid.Add(NormalizeDungeonCellValue(Value));
        }
    }
    else if (Num > 0)
    {
        mGrid.Init(0, Num);
    }

    UE_LOG(LogGridPathfinding, Log, TEXT("GridPathfindingËõªÊôÅEÇÑËõπ„Éª %dx%d, ÁπßÅEøÁπßÅE§ÁπùÔΩ´ÁπßÅEµÁπßÅE§ÁπßÅE∫=%dcm, Origin=%s"),
        GridWidth, GridHeight, TileSize, *Origin.ToCompactString());
}

void AGridPathfindingLibrary::InitializeFromParams(const FGridInitParams& Params)
{
    InitializeGrid(Params.GridCostArray, Params.MapSize, Params.TileSizeCM);

    // Á¨ò„ÅE„ÉªÁ¨ò„ÅEOriginÈö™ÅE≠Ëû≥„ÉªÁ¨ò„ÅE„ÉªÁ¨ò„ÅE
    Origin = Params.Origin;

    UE_LOG(LogGridPathfinding, Log, TEXT("[GridPathfinding] Origin set to: %s"), *Origin.ToCompactString());
}

void AGridPathfindingLibrary::SetGridCost(int32 X, int32 Y, int32 Cost)
{
    if (!GGridAuditEnabled) { /* ÈÄ∂ÅE£Ë≠üÔΩªÁπßÅE™ÁπùËºî‚ÅEÁπßËäΩÅEªÅEΩÈ©•‰∏ûÂñß */ }

    // Á¨ò„ÅEËúàÔΩ•ËúâÂ∏ô„ÅEÈÅΩ„ÉªÂ≥ÅEπù‚ÅE„ÅâÁπù„ÅE„ÅÅE
    if (!InBounds(X, Y, GridWidth, GridHeight))
    {
        UE_LOG(LogGridPathfinding, Error,
            TEXT("[SetGridCost] Invalid cell: (%d,%d)"), X, Y);
        return;
    }

    const int32 Index = ToIndex(X, Y, GridWidth);

    // Á¨ò„ÅEË≠åÔΩ¢ËüÅEºú¬ÄÅE§ÁπßË≤û„ÅEÁ∏∫ÅE´Èö±ÅE≠Áπß¬Ä„ÉªËÇ≤Â±ÆË≠üÔΩªÈÄïÔΩ®„Éª„Éª
    const int32 Before =
        (mGrid.IsValidIndex(Index) ? mGrid[Index] : INT32_MIN);

    // Á¨ò„ÅEË≠ñÔΩ∏Á∏∫Â¥éÔΩæÅEºÁ∏∫ÅEøÁ∏∫ÅEÆÈÄ∂ÅE¥ËúëÈò™„ÄíÁπùÔΩ≠Áπù„ÅE„Åë„ÅE‰∫•ÅE§Â§ê„çæÁπßÅEπÁπùÔΩ¨Áπù„ÅE„É©Á∏∫ÅE™ÁπßÂ≥®ÅEÅE∏∫Ëñô„ÄíËú∑Ê¢ßÂáæË≠ñÔΩ∏Á∏∫Â¥éÔΩæÅEºÁ∏∫ÅEøËÆÄÊáÅE°ç„ÅE„Éª
    FScopeLock _lock(&GridAuditCS);

    UE_LOG(LogGridPathfinding, Warning,
        TEXT("[SetGridCost] Cell(%d,%d) Index=%d BEFORE=%d Á´ä„ÅEAFTER=%d  (Time=%s)"),
        X, Y, Index, Before, Cost,
        *FDateTime::Now().ToString());

    if (mGrid.IsValidIndex(Index))
    {
        mGrid[Index] = Cost;

        // Á¨ò„ÅEË≠ñÔΩ∏Á∏∫ËÇ¥Á∂æÁ∏∫Á§ºÅE¢ÅE∫Èö±„Éª
        const int32 ReadBack = mGrid[Index];
        if (ReadBack != Cost)
        {
            UE_LOG(LogGridPathfinding, Error,
                TEXT("  Á¨∂„ÉªWRITE FAILED: Wrote %d but read %d at (%d,%d) Index=%d"),
                Cost, ReadBack, X, Y, Index);
        }

        // Á¨ò„ÅEËû¢„ÉªÅEº„Éª0Ë´†ÅE≥Ëû≥Â§≤ÅEº‰øÅEÅE 0 Á∏∫ÅE∏Ëç≥ËÇ¥ÅE≠ÅE£Ëç≥È†ëÂ∂åÁ∏∫Èò™ÅEíÁ™∂ÊáâÔΩ∫ÂÄ∂ÅEªÅE∂Á™∂Êò¥‚ÜíÁ∏∫Âä±‚ÄªÈößÅE≥ÈÇèÔΩ∞ËúÅEΩ∫Ëúâ„ÅE
        if (Before < 0 && Cost == 0)
        {
            UE_LOG(LogGridPathfinding, Error,
                TEXT("  ˚ùûÂú∑ WALLÁ´ä„ÅE DETECTED at Cell(%d,%d) Index=%d"), X, Y, Index);

            // ÁπßÅE≥ÁπùÔΩºÁπùÔΩ´ÁπßÅEµÁπßÅE§ÁπùËå®ÅEºÁ£ØÊú™Ë¨®ÅE∞/ÁπùËºî„ÅÉÁπßÅE§ÁπùÔΩ´/Èô¶ÈØâÔΩº„Éª
            UE_LOG(LogGridPathfinding, Error,
                TEXT("  CallSite: %s (%s:%d)"),
                ANSI_TO_TCHAR(__FUNCTION__), ANSI_TO_TCHAR(__FILE__), __LINE__);

            // ÁπßÅEπÁπßÅEøÁπù„ÅE„ÅëÁπùÂåªŒûÁπùÔΩºÁπßÅEπÁπßÂâÅEΩ∏ÅE∏Á∏∫Êñê‚ÅEËú∑ËàåÔø•
            FDebug::DumpStackTraceToLog(ELogVerbosity::Error);

            // Á∏∫ÅE§Á∏∫„Éª„ÄíÁ∏∫ÅE´ÁπßÅE∞ÁπùÔΩ™Áπù„ÅE„É©Á´äÂÅEŒ°ÁπùÔΩºÁπùÔΩ´ÁπùÂ≥®¬Ä‚àöŒ°ÁπùÔΩºÁπùÔΩ´Áπù‰øÅEÅEÁπßÅE∞ÁπùÔΩ™Áπù„ÅE„É©Á∏∫ÅEÆË†ï¬ÄË†ïÔΩ©ËÆÄÊáÅEΩ®ÅEºÁπßË≤ûÁëûÁ∏∫„Éª
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
            TEXT("  Á¨∂„ÉªmGrid.IsValidIndex(%d) = false! Size=%d"),
            Index, mGrid.Num());
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
        return mGrid[ToIndex(X, Y, GridWidth)];
    }
    return -1;
}

int32 AGridPathfindingLibrary::ReturnGridStatus(const FVector& InputVector) const
{
    FIntPoint GridPos = WorldToGridInternal(InputVector);
    int32 Cost = GetGridCost(GridPos.X, GridPos.Y);

    // ÁπùÔΩ≠ÁπßÅE∞ÁπùÔΩ¨ÁπùÂê∂ŒùË≠õ¬ÄÈ©ïÔΩ©Ëõπ„Éª È®æÂ§äÔΩ°ÊÇüÂ∫ÅE∏∫ÅEØ VeryVerboseÁ∏≤‚àö„É∂ÁπùÔΩ≠Áπù„ÅE„ÅëÁ∏∫ÅEØ WarningÁ∏≤‚à´ÁÑöËü∂ÅE∏Á∏∫ÅEÆÁ∏∫ÅEø Error
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
            TEXT("[ReturnGridStatus] INVALID World:%s -> Grid:(%d,%d) Cost:%d (W=%d H=%d mGrid.Num=%d)"),
            *InputVector.ToCompactString(), GridPos.X, GridPos.Y, Cost, GridWidth, GridHeight, mGrid.Num());
    }

    return Cost;
}


// ==================== Áπù‰ª£„ÅõÁπùËºî„ÅÉÁπßÅE§ÁπùÔΩ≥Áπù„ÅE„ÅÅEπùÔΩ≥ÁπßÅE∞ ====================

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
    if (Num == 0 || mGrid.Num() != Num)
        return false;

    const FIntPoint S = WorldToGridInternal(StartWorld);
    const FIntPoint E = WorldToGridInternal(EndWorld);

    if (!InBounds(S.X, S.Y, GridWidth, GridHeight) ||
        !InBounds(E.X, E.Y, GridWidth, GridHeight))
        return false;

    const int32 StartId = ToIndex(S.X, S.Y, GridWidth);
    const int32 EndId = ToIndex(E.X, E.Y, GridWidth);

    if (mGrid[StartId] < 0 || mGrid[EndId] < 0)
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
        if (mGrid[nid] < 0)
        {
            return false;
        }

        if (Delta.X != 0 && Delta.Y != 0)
        {
            const int32 adjX = ToIndex(X + Delta.X, Y, GridWidth);
            const int32 adjY = ToIndex(X, Y + Delta.Y, GridWidth);
            if (!InBounds(X + Delta.X, Y, GridWidth, GridHeight) || mGrid[adjX] < 0)
            {
                return false;
            }
            if (!InBounds(X, Y + Delta.Y, GridWidth, GridHeight) || mGrid[adjY] < 0)
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
            if (!CanTraverseDelta(GridWidth, GridHeight, mGrid, cx, cy, d))
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
            const int32 terrain = FMath::Max(0, mGrid[nid]);
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
    if (Num == 0 || mGrid.Num() != Num)
        return false;

    const FIntPoint S = WorldToGridInternal(StartWorld);
    const FIntPoint E = WorldToGridInternal(EndWorld);

    if (!InBounds(S.X, S.Y, GridWidth, GridHeight) ||
        !InBounds(E.X, E.Y, GridWidth, GridHeight))
        return false;

    const int32 StartId = ToIndex(S.X, S.Y, GridWidth);
    const int32 EndId = ToIndex(E.X, E.Y, GridWidth);

    const int32 OriginalStartCost = mGrid[StartId];
    const int32 OriginalEndCost = mGrid[EndId];

    TArray<int32>& MutableGrid = const_cast<TArray<int32>&>(mGrid);
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
            const int32 terrain = FMath::Max(0, mGrid[nid]);
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

// ==================== ÈöïÂ§ú„çΩËÆÄÊáÅE°ÅE====================

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

            TArray<AActor*> Actors;
            GetActorsAtGridPosition(TargetGrid, ActorClassFilter, Actors);
            for (AActor* Actor : Actors)
            {
                Result.VisibleActors.Add(Actor);
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

            TArray<AActor*> Actors;
            GetActorsAtGridPosition(TargetGrid, ActorClassFilter, Actors);
            for (AActor* Actor : Actors)
            {
                Result.VisibleActors.Add(Actor);
            }
        }
    }

    Result.TotalTilesScanned = Result.VisibleTiles.Num();
    return Result;
}

// ==================== ËúªÅE®ËùóÔΩ≤ËÆÄÊáÅEΩ¥ÅE¢ ====================

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

        const int32 Cost = mGrid[ToIndex(Target.X, Target.Y, GridWidth)];

        if (Cost < 0)
        {
            Result.BlockedTiles.Add(Target);
        }
        else
        {
            TArray<AActor*> Actors;
            GetActorsAtGridPosition(Target, ActorClassFilter, Actors);
            if (Actors.Num() > 0)
            {
                for (AActor* Actor : Actors)
                {
                    Result.AdjacentActors.Add(Actor);
                }
            }
            else
            {
                Result.EmptyTiles.Add(Target);
            }
        }
    }

    return Result;
}

AActor* AGridPathfindingLibrary::GetActorAtPosition(
    const FVector& WorldPos,
    float SearchRadius,
    TSubclassOf<AActor> ActorClassFilter) const
{
    if (SearchRadius < 0.f)
    {
        SearchRadius = TileSize * 0.5f;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return nullptr;
    }

    UClass* FilterClass = ActorClassFilter ? ActorClassFilter.Get() : AActor::StaticClass();

    if (UGridOccupancySubsystem* Occupancy = World->GetSubsystem<UGridOccupancySubsystem>())
    {
        const FIntPoint Cell = WorldToGridInternal(WorldPos);

        if (AActor* Occupant = Occupancy->GetActorAtCell(Cell))
        {
            if (!FilterClass || Occupant->IsA(FilterClass))
            {
                return Occupant;
            }
        }

        if (AActor* Reserved = Occupancy->GetReservationOwner(Cell))
        {
            if (Reserved && (!FilterClass || Reserved->IsA(FilterClass)))
            {
                return Reserved;
            }
        }
    }

    for (TActorIterator<AActor> It(World, FilterClass); It; ++It)
    {
        AActor* Actor = *It;
        if (!Actor || Actor == this)
        {
            continue;
        }

        const float Dist = FVector::Dist2D(Actor->GetActorLocation(), WorldPos);
        if (Dist <= SearchRadius)
        {
            return Actor;
        }
    }

    return nullptr;
}

bool AGridPathfindingLibrary::HasLineOfSight(const FVector& StartWorld, const FVector& EndWorld) const
{
    const FIntPoint Start = WorldToGridInternal(StartWorld);
    const FIntPoint End = WorldToGridInternal(EndWorld);
    return IsVisibleFromPoint(Start, End);
}

// ==================== ÁπùÔΩ¶ÁπùÔΩºÁπù„ÅE„ÅÅEπùÔΩ™Áπù„ÅE„ÅÅE====================

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
    return FMath::Abs(A.X - B.X) + FMath::Abs(A.Y - B.Y);
}

// ==================== ËúÄ„ÉªŒöÁπùÂÄ•ŒùÁπù‰ª£„Éª ====================

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

        const int32 cost = mGrid[ToIndex(x0, y0, GridWidth)];
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

void AGridPathfindingLibrary::GetActorsAtGridPosition(const FIntPoint& GridPos, TSubclassOf<AActor> ClassFilter, TArray<AActor*>& OutActors) const
{
    OutActors.Reset();

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    UClass* FilterClass = ClassFilter ? ClassFilter.Get() : AActor::StaticClass();

    if (UGridOccupancySubsystem* Occupancy = World->GetSubsystem<UGridOccupancySubsystem>())
    {
        if (AActor* Occupant = Occupancy->GetActorAtCell(GridPos))
        {
            if (!FilterClass || Occupant->IsA(FilterClass))
            {
                OutActors.Add(Occupant);
            }
        }

        if (AActor* Reserved = Occupancy->GetReservationOwner(GridPos))
        {
            if (Reserved && (!FilterClass || Reserved->IsA(FilterClass)))
            {
                OutActors.AddUnique(Reserved);
            }
        }

        if (OutActors.Num() > 0)
        {
            return;
        }
    }

    const FVector WorldPos = GridToWorldInternal(GridPos, 0.f);
    const float SearchRadius = TileSize * 0.5f;

    for (TActorIterator<AActor> It(World, FilterClass); It; ++It)
    {
        AActor* Actor = *It;
        if (!Actor || Actor == this)
        {
            continue;
        }

        const float Dist = FVector::Dist2D(Actor->GetActorLocation(), WorldPos);
        if (Dist <= SearchRadius)
        {
            OutActors.Add(Actor);
        }
    }
}

// ==================== ÈúçÊôûÂ±¨Èö™ËÇ≤ÅEÆ„Éª====================

int32 AGridPathfindingLibrary::GetChebyshevDistance(FIntPoint A, FIntPoint B)
{
    return FMath::Max(FMath::Abs(A.X - B.X), FMath::Abs(A.Y - B.Y));
}

int32 AGridPathfindingLibrary::GetManhattanDistanceGrid(FIntPoint A, FIntPoint B)
{
    return FMath::Abs(A.X - B.X) + FMath::Abs(A.Y - B.Y);
}

int32 AGridPathfindingLibrary::GetEuclideanDistanceGrid(FIntPoint A, FIntPoint B)
{
    int32 DX = A.X - B.X;
    int32 DY = A.Y - B.Y;
    return FMath::RoundToInt(FMath::Sqrt(static_cast<float>(DX * DX + DY * DY)));
}

// ==================== Ë≠ÅEΩ∞ÈöïÂüéÊú™Ë¨®ÅE∞ ====================

int32 AGridPathfindingLibrary::ReturnGridStatusIgnoringSelf(const FVector& InputVector, AActor* IgnoreActor) const
{
    FIntPoint GridPos = WorldToGridInternal(InputVector);
    int32 Cost = GetGridCost(GridPos.X, GridPos.Y);

    if (Cost == -1 && IgnoreActor)
    {
        AActor* Occupant = GetActorAtPosition(InputVector, TileSize * 0.5f, nullptr);
        if (Occupant == IgnoreActor)
        {
            UE_LOG(LogGridPathfinding, Verbose, TEXT("[GridPathfinding] Self-occupied at %s, allowing pass"),
                *InputVector.ToCompactString());
            return 1;  // Walkable
        }
    }

    return Cost;
}

// ==================== Áπù„ÅE„É∞Áπù„ÅE„ÅíÁπùÔΩªÁπßÅEπÁπùÔΩ¢ÁπùÔΩºÁπßÅEØÁπù„ÅE„ÅõÁπù„ÅE====================

void AGridPathfindingLibrary::GridSmokeTest()
{
    const int32 W = GridWidth, H = GridHeight;
    UE_LOG(LogGridPathfinding, Warning, TEXT("[Smoke] Size=%dx%d mGrid=%d Tile=%d Origin=%s"),
        W, H, mGrid.Num(), TileSize, *Origin.ToCompactString());

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

// ==================== ÈÄ∂ÅE£Ë≠üÔΩªÁπùÔΩ¢ÁπùÔΩºÁπùËÅEÁïëÁπßÅE≥ÁπùÊß≠Œ¶Áπù„ÅE====================

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


// ==================== ÈÇ®ÅE±Ëú∑„ÉªPI„ÉªÂüüÈõ∞AIÈÅòÔΩªËúçËºî„É∂ÁπùÔΩ≠Áπù„ÅE„ÅëËè´ÅEÆË±ÅEΩ£„Éª„Éª====================

bool AGridPathfindingLibrary::IsCellWalkable(const FIntPoint& Cell) const
{
    const int32 TerrainCost = GetGridCost(Cell.X, Cell.Y);
    if (TerrainCost < 0)
    {
        return false;
    }

    if (UWorld* World = GetWorld())
    {
        if (UGridOccupancySubsystem* Occupancy = World->GetSubsystem<UGridOccupancySubsystem>())
        {
            if (DoesCellContainBlockingActor(Occupancy, Cell, nullptr))
            {
                return false;
            }
        }
    }

    return true;
}

bool AGridPathfindingLibrary::IsCellWalkableIgnoringActor(const FIntPoint& Cell, AActor* IgnoreActor) const
{
    const int32 TerrainCost = GetGridCost(Cell.X, Cell.Y);
    if (TerrainCost < 0)
    {
        return false;
    }

    if (UWorld* World = GetWorld())
    {
        if (UGridOccupancySubsystem* Occupancy = World->GetSubsystem<UGridOccupancySubsystem>())
        {
            if (DoesCellContainBlockingActor(Occupancy, Cell, IgnoreActor))
            {
                return false;
            }
        }
    }

    return true;
}

bool AGridPathfindingLibrary::IsCellWalkableAtWorldPosition(const FVector& WorldPos) const
{
    const FIntPoint Cell = WorldToGrid(WorldPos);
    return IsCellWalkable(Cell);
}






