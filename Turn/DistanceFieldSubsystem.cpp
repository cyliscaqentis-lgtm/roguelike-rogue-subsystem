// Copyright Epic Games, Inc. All Rights Reserved.

// CodeRevision: INC-2025-00030-R2 (Migrate to UGridPathfindingSubsystem) (2025-11-17 00:40)
#include "DistanceFieldSubsystem.h"
#include "../Grid/GridOccupancySubsystem.h"
#include "../Grid/GridPathfindingSubsystem.h"
#include "../Utility/ProjectDiagnostics.h"  // DIAG_LOG, diagnostic helpers
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

//-----------------------------------------------------------------------------
// Console variables
//-----------------------------------------------------------------------------

// Upper bound for total explored cells (freeze protection)
static int32 GTS_DF_MaxCells = 300000;
static FAutoConsoleVariableRef CVarTS_DF_MaxCells(
    TEXT("ts.DistanceField.MaxCells"),
    GTS_DF_MaxCells,
    TEXT("Maximum number of explored cells (prevents infinite loops / freezes)"),
    ECVF_Default
);

// Allow diagonal movement in distance field
static int32 GTS_DF_AllowDiag = 1;
static FAutoConsoleVariableRef CVarTS_DF_AllowDiag(
    TEXT("ts.DistanceField.AllowDiagonal"),
    GTS_DF_AllowDiag,
    TEXT("Allow diagonal moves (0 = disabled, 1 = enabled)"),
    ECVF_Default
);

//-----------------------------------------------------------------------------

void UDistanceFieldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    
    // CodeRevision: INC-2025-00030-R2 (Migrate to UGridPathfindingSubsystem) (2025-11-17 00:40)
    // Cache UGridPathfindingSubsystem (may be null this early; GetPathFinder() will lazily refill)
    CachedPathFinder = GetWorld()->GetSubsystem<UGridPathfindingSubsystem>();
    if (CachedPathFinder.IsValid())
    {
        UE_LOG(LogTemp, Log, TEXT("[DistanceField] UGridPathfindingSubsystem cached"));
    }

    UE_LOG(LogTemp, Log, TEXT("[DistanceField] Initialized (PathFinder will be lazily loaded if needed)"));
}

void UDistanceFieldSubsystem::Deinitialize()
{
    DistanceMap.Empty();
    NextStepMap.Empty();
    UE_LOG(LogTemp, Log, TEXT("[DistanceField] Deinitialized"));
    Super::Deinitialize();
}

//-----------------------------------------------------------------------------
// Public API (Blueprint-friendly)
//-----------------------------------------------------------------------------

void UDistanceFieldSubsystem::UpdateDistanceField(const FIntPoint& PlayerCell)
{
    // Simple wrapper: use a generous margin (100) so distant enemies are covered.
    // Assumes a ~100x100 tile area around the player is enough for most maps.
    UpdateDistanceFieldInternal(PlayerCell, TSet<FIntPoint>(), 100);
}

// C++ optimized variant
void UDistanceFieldSubsystem::UpdateDistanceFieldOptimized(
    const FIntPoint& PlayerCell,
    const TSet<FIntPoint>& OptionalTargets,
    int32 BoundsMargin)
{
    UpdateDistanceFieldInternal(PlayerCell, OptionalTargets, BoundsMargin);
}

//-----------------------------------------------------------------------------
// Internal types (priority queue)
//-----------------------------------------------------------------------------

struct FOpenNode
{
    FIntPoint Cell;
    int32 Cost;
};

struct FOpenNodeLess
{
    bool operator()(const FOpenNode& A, const FOpenNode& B) const
    {
        return A.Cost > B.Cost; // min-heap (lower cost has higher priority)
    }
};

//-----------------------------------------------------------------------------
// Core distance field build (Dijkstra on grid using PathFinder terrain walkability)
//-----------------------------------------------------------------------------

void UDistanceFieldSubsystem::UpdateDistanceFieldInternal(
    const FIntPoint& PlayerCell,
    const TSet<FIntPoint>& OptionalTargets,
    int32 BoundsMargin)
{
    DistanceMap.Empty();
    NextStepMap.Empty();
    PlayerPosition = PlayerCell;
    
    // Coarse bounds in absolute grid space used by GetDistanceAbs/EnsureCoverage.
    Bounds.Min = PlayerCell - FIntPoint(BoundsMargin, BoundsMargin);
    Bounds.Max = PlayerCell + FIntPoint(BoundsMargin, BoundsMargin);

    // 1. Tight bounding box based on player + optional targets
    FIntPoint Min = PlayerCell;
    FIntPoint Max = PlayerCell;

    for (const FIntPoint& Target : OptionalTargets)
    {
        Min.X = FMath::Min(Min.X, Target.X);
        Min.Y = FMath::Min(Min.Y, Target.Y);
        Max.X = FMath::Max(Max.X, Target.X);
        Max.Y = FMath::Max(Max.Y, Target.Y);
    }

    Min -= FIntPoint(BoundsMargin, BoundsMargin);
    Max += FIntPoint(BoundsMargin, BoundsMargin);

    auto InBounds = [&](const FIntPoint& C)
    {
        return (C.X >= Min.X && C.X <= Max.X && C.Y >= Min.Y && C.Y <= Max.Y);
    };

    // 2. Early exit bookkeeping for specific targets
    TSet<FIntPoint> PendingTargets = OptionalTargets;
    int32 RemainingTargets = PendingTargets.Num();

    const int32 MaxCells = GTS_DF_MaxCells;
    const bool bDiagonal = !!GTS_DF_AllowDiag;

    // CodeRevision: INC-2025-00030-R2 (Migrate to UGridPathfindingSubsystem) (2025-11-17 00:40)
    // PathFinder provides terrain-only walkability (we ignore dynamic occupancy here).
    const UGridPathfindingSubsystem* GridPathfinding = GetPathFinder();
    if (!GridPathfinding)
    {
        UE_LOG(LogTemp, Error, TEXT("[DistanceField] UGridPathfindingSubsystem not found"));
        return;
    }

    int32 ProcessedCells = 0;

    TArray<FOpenNode> Open;
    Open.HeapPush(FOpenNode{ PlayerCell, 0 }, FOpenNodeLess{});
    DistanceMap.Add(PlayerCell, 0);

    // Closed set to avoid re-processing cells and to guarantee each is finalized once
    TSet<FIntPoint> ClosedSet;

    static const FIntPoint StraightDirs[] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
    static const FIntPoint DiagonalDirs[] = { {1, 1}, {1, -1}, {-1, 1}, {-1, -1} };

    const int32 StraightCost = 10;
    const int32 DiagonalCost = 14;

    while (Open.Num() > 0)
    {
        FOpenNode Current;
        Open.HeapPop(Current, FOpenNodeLess{});

        // If this cell is already finalized, skip the stale heap entry
        if (ClosedSet.Contains(Current.Cell))
        {
            continue;
        }

        if (++ProcessedCells > MaxCells)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[DistanceField] MaxCells limit reached: Processed=%d (Queue=%d)"),
                ProcessedCells, Open.Num());
            break;
        }

        if (!InBounds(Current.Cell))
        {
            continue;
        }

        // Mark as finalized
        ClosedSet.Add(Current.Cell);

        // Target bookkeeping: once all requested targets are reached, we can stop early
        if (RemainingTargets > 0 && PendingTargets.Remove(Current.Cell) > 0)
        {
            --RemainingTargets;
            if (RemainingTargets == 0 && OptionalTargets.Num() > 0)
            {
                UE_LOG(LogTemp, Log,
                    TEXT("[DistanceField] All requested targets reached after %d cells"),
                    ProcessedCells);
            }
        }

        UE_LOG(LogTemp, VeryVerbose,
            TEXT("[DistanceField] Processing Cell=(%d,%d) Cost=%d Queue=%d Closed=%d"),
            Current.Cell.X, Current.Cell.Y, Current.Cost, Open.Num(), ClosedSet.Num());

        auto Relax = [&](const FIntPoint& Next, int32 StepCost)
        {
            if (!InBounds(Next))
            {
                return;
            }

            // Already finalized => no further relaxation needed
            if (ClosedSet.Contains(Next))
            {
                return;
            }

            const bool bIsTargetCell = PendingTargets.Contains(Next);

            // Use PathFinder terrain-only walkability.
            // FIX (INC-2025-00002): Ignore dynamic occupancy, build pure terrain distance field.
            // IsCellWalkableIgnoringActor(Cell, nullptr) => checks terrain only.
            if (!bIsTargetCell &&
                Next != PlayerCell &&
                !GridPathfinding->IsCellWalkableIgnoringActor(Next, nullptr))
            {
                return;
            }

            const int32 NewCost = Current.Cost + StepCost;
            int32& Best = DistanceMap.FindOrAdd(Next, TNumericLimits<int32>::Max());

            if (NewCost < Best)
            {
                Best = NewCost;
                NextStepMap.Add(Next, Current.Cell);
                Open.HeapPush(FOpenNode{ Next, NewCost }, FOpenNodeLess{});
            }
        };

        for (const FIntPoint& Dir : StraightDirs)
        {
            Relax(Current.Cell + Dir, StraightCost);
        }

        if (bDiagonal)
        {
            for (const FIntPoint& Dir : DiagonalDirs)
            {
                const FIntPoint Next = Current.Cell + Dir;

                if (bPreventCornerCutting && !CanMoveDiagonal(Current.Cell, Next))
                {
                    continue;
                }

                Relax(Next, DiagonalCost);
            }
        }
    }

    UE_LOG(LogTemp, Log,
        TEXT("[DistanceField] Dijkstra complete: PlayerCell=(%d,%d), Cells=%d, Processed=%d, TargetsLeft=%d"),
        PlayerCell.X, PlayerCell.Y, DistanceMap.Num(), ProcessedCells, RemainingTargets);

    // Enemy movement diagnostics – log unreachable targets / enemies
    UE_LOG(LogTemp, Warning,
        TEXT("[DistanceField] BuildComplete: TotalCells=%d, ProcessedCells=%d, UnreachedTargets=%d"),
        DistanceMap.Num(), ProcessedCells, RemainingTargets);

    if (RemainingTargets > 0)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[DistanceField] %d targets could not be reached by the distance field!"),
            RemainingTargets);

        int32 LoggedTargets = 0;
        for (const FIntPoint& Pending : PendingTargets)
        {
            const int32* DistEntry = DistanceMap.Find(Pending);
            UE_LOG(LogTemp, Warning,
                TEXT("[DistanceField] Pending target Cell=(%d,%d) DistEntry=%s"),
                Pending.X, Pending.Y,
                DistEntry ? *FString::Printf(TEXT("%d"), *DistEntry) : TEXT("NONE"));

            if (++LoggedTargets >= 16)
            {
                break;
            }
        }
        
        // Simple scan: log all enemies that exist in the world (for debugging reachability)
        if (UWorld* World = GetWorld())
        {
            int32 UnreachedCount = 0;
            for (TActorIterator<AActor> It(World); It; ++It)
            {
                AActor* Actor = *It;
                if (Actor && Actor->ActorHasTag(TEXT("Enemy")))
                {
                    UE_LOG(LogTemp, Warning,
                        TEXT("[DistanceField] Found enemy: %s"),
                        *GetNameSafe(Actor));
                    ++UnreachedCount;
                }
            }
            UE_LOG(LogTemp, Error,
                TEXT("[DistanceField] Total enemies found in world: %d (all may be unreachable)"),
                UnreachedCount);
        }
    }
}

//-----------------------------------------------------------------------------

int32 UDistanceFieldSubsystem::GetDistance(const FIntPoint& Cell) const
{
    const int32* DistPtr = DistanceMap.Find(Cell);
    return DistPtr ? *DistPtr : -1;
}

//-----------------------------------------------------------------------------
// Path-following: get next step towards player, using absolute grid coords
//-----------------------------------------------------------------------------

FIntPoint UDistanceFieldSubsystem::GetNextStepTowardsPlayer(
    const FIntPoint& FromCell,
    AActor* IgnoreActor) const
{
    const int32 d0 = GetDistanceAbs(FromCell);
    if (d0 < 0)
    {
        DIAG_LOG(Warning,
            TEXT("[GetNextStep] Enemy out of bounds (%d,%d) - staying put"),
            FromCell.X, FromCell.Y);
        return FromCell;
    }

    // Already at the player's cell – do not move.
    if (d0 == 0)
    {
        DIAG_LOG(Log,
            TEXT("[GetNextStep] FromCell=(%d,%d) ALREADY AT PLAYER"),
            FromCell.X, FromCell.Y);
        return FromCell;
    }

    // Player direction (clamped to -1, 0, 1)
    const FIntPoint GoalDelta(
        FMath::Clamp(PlayerPosition.X - FromCell.X, -1, 1),
        FMath::Clamp(PlayerPosition.Y - FromCell.Y, -1, 1)
    );

    DIAG_LOG(VeryVerbose,
        TEXT("[GetNextStep] FromCell=(%d,%d), Player=(%d,%d), GoalDelta=(%d,%d)"),
        FromCell.X, FromCell.Y, PlayerPosition.X, PlayerPosition.Y, GoalDelta.X, GoalDelta.Y);

    auto ComputeAlignment = [&GoalDelta](const FIntPoint& Offset) -> int32
    {
        // Range: -2 to +2 (e.g., perfect alignment=2, perpendicular=0, opposite=-2)
        return Offset.X * GoalDelta.X + Offset.Y * GoalDelta.Y;
    };

    FIntPoint Best = FromCell;
    int32 BestDist = d0;
    int32 BestAlign = TNumericLimits<int32>::Lowest();
    bool bBestIsDiagonal = true; // Default to true so a non-diagonal move is always better on a tie

    struct FNeighborDef
    {
        FIntPoint Offset;
        bool bDiagonal;
    };

    static const FNeighborDef Neighbors[] =
    {
        {FIntPoint( 1,  0), false},
        {FIntPoint(-1,  0), false},
        {FIntPoint( 0,  1), false},
        {FIntPoint( 0, -1), false},
        {FIntPoint( 1,  1), true },
        {FIntPoint( 1, -1), true },
        {FIntPoint(-1,  1), true },
        {FIntPoint(-1, -1), true }
    };

    int32 CandidateCount = 0;

    // Get GridPathfinding for terrain-only checks (ignore dynamic occupancy)
    const UGridPathfindingSubsystem* GridPathfinding = GetPathFinder();
    if (!GridPathfinding)
    {
        DIAG_LOG(Error, TEXT("[GetNextStep] GridPathfindingSubsystem not found"));
        return FromCell;
    }

    // CodeRevision: INC-2025-1153-R1 (Prefer straight steps over diagonals when aligned with player) (2025-11-20 18:00)
    // If the enemy is exactly aligned on X or Y with the player, try a straight step along that axis first.
    if ((GoalDelta.X == 0) != (GoalDelta.Y == 0))
    {
        const FIntPoint StraightOffset = GoalDelta;
        const FIntPoint StraightCell   = FromCell + StraightOffset;

        if (GridPathfinding->IsCellWalkableIgnoringActor(StraightCell, nullptr))
        {
            const int32 nd = GetDistanceAbs(StraightCell);
            if (nd >= 0 && nd < d0)
            {
                DIAG_LOG(Log,
                    TEXT("[GetNextStep] STRAIGHT preference: From=(%d,%d) -> Next=(%d,%d) (aligned axis)"),
                    FromCell.X, FromCell.Y, StraightCell.X, StraightCell.Y);
                return StraightCell;
            }
        }
    }

    for (const FNeighborDef& Neighbor : Neighbors)
    {
        const FIntPoint N = FromCell + Neighbor.Offset;

        // Check terrain walkability only (ignore dynamic unit occupancy for optimistic pathing)
        // Actual conflicts will be resolved by ConflictResolverSubsystem
        if (!GridPathfinding->IsCellWalkableIgnoringActor(N, nullptr))
        {
            DIAG_LOG(VeryVerbose,
                TEXT("[GetNextStep]   Neighbor (%d,%d) REJECTED: terrain blocked"),
                N.X, N.Y);
            continue;
        }

        // For diagonal moves, check if both orthogonal shoulders are terrain-walkable
        if (Neighbor.bDiagonal)
        {
            const FIntPoint Delta = Neighbor.Offset;
            const FIntPoint Side1 = FromCell + FIntPoint(Delta.X, 0);
            const FIntPoint Side2 = FromCell + FIntPoint(0, Delta.Y);

            if (!GridPathfinding->IsCellWalkableIgnoringActor(Side1, nullptr) ||
                !GridPathfinding->IsCellWalkableIgnoringActor(Side2, nullptr))
            {
                DIAG_LOG(VeryVerbose,
                    TEXT("[GetNextStep]   Neighbor (%d,%d) REJECTED: diagonal path blocked"),
                    N.X, N.Y);
                continue;
            }
        }

        const int32 nd = GetDistanceAbs(N);
        // Cells that do not reduce distance are not candidates
        if (nd < 0 || nd >= d0)
        {
            DIAG_LOG(VeryVerbose,
                TEXT("[GetNextStep]   Neighbor (%d,%d) REJECTED: dist %d (no reduction from %d)"),
                N.X, N.Y, nd, d0);
            continue;
        }

        const int32 Align = ComputeAlignment(Neighbor.Offset);
        ++CandidateCount;

        bool bIsBetter = false;
        FString Reason;

        // 1. Better distance
        if (nd < BestDist)
        {
            bIsBetter = true;
            Reason = TEXT("better distance");
        }
        // 2. Same distance, better alignment
        else if (nd == BestDist)
        {
            if (Align > BestAlign)
            {
                bIsBetter = true;
                Reason = TEXT("same distance, better alignment");
            }
            // 3. Same distance and alignment, prefer non-diagonal
            else if (Align == BestAlign)
            {
                if (bBestIsDiagonal && !Neighbor.bDiagonal)
                {
                    bIsBetter = true;
                    Reason = TEXT("same distance & alignment, prefer straight");
                }
            }
        }

        DIAG_LOG(VeryVerbose,
            TEXT("[GetNextStep]   Neighbor (%d,%d): dist=%d, align=%d, diag=%d -> %s (best: dist=%d, align=%d, diag=%d)"),
            N.X, N.Y, nd, Align, Neighbor.bDiagonal ? 1 : 0,
            bIsBetter ? *FString::Printf(TEXT("ACCEPT (%s)"), *Reason) : TEXT("reject"),
            BestDist, BestAlign, bBestIsDiagonal ? 1 : 0);

        if (bIsBetter)
        {
            BestDist        = nd;
            BestAlign       = Align;
            bBestIsDiagonal = Neighbor.bDiagonal;
            Best            = N;
        }
    }

    if (Best == FromCell)
    {
        DIAG_LOG(Log,
            TEXT("[GetNextStep] STAY at (%d,%d) - no better neighbor found (checked %d candidates)"),
            FromCell.X, FromCell.Y, CandidateCount);
        return FromCell;
    }

    DIAG_LOG(Log,
        TEXT("[GetNextStep] FromCell=(%d,%d) -> NextCell=(%d,%d) (Dist: %d -> %d, Align=%d, Diag=%d, Candidates=%d)"),
        FromCell.X, FromCell.Y, Best.X, Best.Y, d0, BestDist, BestAlign, bBestIsDiagonal ? 1 : 0, CandidateCount);

    return Best;
}

//-----------------------------------------------------------------------------
// Walkability helpers
//-----------------------------------------------------------------------------

bool UDistanceFieldSubsystem::IsWalkable(const FIntPoint& Cell, AActor* IgnoreActor) const
{
    // CodeRevision: INC-2025-00030-R2 (Migrate to UGridPathfindingSubsystem) (2025-11-17 00:40)
    const UGridPathfindingSubsystem* GridPathfinding = GetPathFinder();
    if (!GridPathfinding)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[IsWalkable] UGridPathfindingSubsystem not found, returning false"));
        return false;
    }

    // CodeRevision: INC-2025-00021-R1 (Replace IsCellWalkable with IsCellWalkableIgnoringActor - Phase 2.1) (2025-11-17 15:05)
    // Use terrain-only check here; dynamic occupancy is handled by higher-level logic.
    return GridPathfinding->IsCellWalkableIgnoringActor(Cell, IgnoreActor);
}

bool UDistanceFieldSubsystem::CanMoveDiagonal(const FIntPoint& From, const FIntPoint& To) const
{
    const FIntPoint Delta = To - From;
    const FIntPoint Side1 = From + FIntPoint(Delta.X, 0); // horizontal shoulder
    const FIntPoint Side2 = From + FIntPoint(0, Delta.Y); // vertical shoulder

    // FIX (2025-11-11): Correct corner-cutting rule.
    // Only allow diagonal movement if BOTH orthogonal shoulders are walkable.
    // If either side is blocked, the diagonal is not allowed (prevents corner clipping).
    return IsWalkable(Side1) && IsWalkable(Side2);
}

//-----------------------------------------------------------------------------
// Absolute-coordinate distance API (bounds-aware)
//-----------------------------------------------------------------------------

int32 UDistanceFieldSubsystem::GetDistanceAbs(const FIntPoint& Abs) const
{
    if (!Bounds.Contains(Abs))
    {
        return -2; // Explicit "out of bounds" sentinel
    }

    // DistanceMap is stored in absolute grid coordinates.
    const int32* DistPtr = DistanceMap.Find(Abs);
    return DistPtr ? *DistPtr : -1;
}

bool UDistanceFieldSubsystem::EnsureCoverage(const FIntPoint& Abs)
{
    if (Bounds.Contains(Abs))
    {
        return false; // Already covered by current field
    }

    // Rebuild around the player with a radius large enough to cover Abs.
    const int32 Radius = FMath::Max(
        FMath::Abs(Abs.X - PlayerPosition.X),
        FMath::Abs(Abs.Y - PlayerPosition.Y)) + 2;
    
    // Rebuild and refresh Bounds.Min/Max (full field rebuild).
    UpdateDistanceFieldInternal(PlayerPosition, TSet<FIntPoint>(), Radius);
    return true;
}

//-----------------------------------------------------------------------------
// PathFinder accessor
//-----------------------------------------------------------------------------

// CodeRevision: INC-2025-00030-R2 (Migrate to UGridPathfindingSubsystem) (2025-11-17 00:40)
UGridPathfindingSubsystem* UDistanceFieldSubsystem::GetPathFinder() const
{
    if (!CachedPathFinder.IsValid())
    {
        // If cache is invalid, try to retrieve the subsystem again
        const_cast<UDistanceFieldSubsystem*>(this)->CachedPathFinder =
            GetWorld()->GetSubsystem<UGridPathfindingSubsystem>();
    }
    return CachedPathFinder.Get();
}
