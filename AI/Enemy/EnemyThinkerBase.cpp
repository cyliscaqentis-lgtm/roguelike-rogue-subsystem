// CodeRevision: INC-2025-00030-R2 (Migrate to UGridPathfindingSubsystem) (2025-11-17 00:40)
// EnemyThinkerBase.cpp
#include "EnemyThinkerBase.h"
#include "AbilitySystemComponent.h"
#include "AI/Enemy/EnemyThinkerBase.h"
#include "Turn/DistanceFieldSubsystem.h"
#include "Grid/GridPathfindingSubsystem.h"
#include "Utility/GridUtils.h"  // CodeRevision: INC-2025-00016-R1 (2025-11-16 14:00)
#include "Utility/RogueGameplayTags.h"  // INC-2025-0002: GameplayTag統一のため追加
#include "Kismet/GameplayStatics.h"
#include "../../Turn/GameTurnManagerBase.h"
#include "EngineUtils.h"

DEFINE_LOG_CATEGORY(LogEnemyThinker);

UEnemyThinkerBase::UEnemyThinkerBase()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UEnemyThinkerBase::BeginPlay()
{
    // CodeRevision: INC-2025-00030-R2 (Migrate to UGridPathfindingSubsystem) (2025-11-17 00:40)
    Super::BeginPlay();

    UWorld* World = GetWorld();
    if (World)
    {
        CachedPathFinder = World->GetSubsystem<UGridPathfindingSubsystem>();
        if (CachedPathFinder.IsValid())
        {
            UE_LOG(LogEnemyThinker, Log, TEXT("[EnemyThinker] UGridPathfindingSubsystem cached"));
        }
    }
}

FEnemyIntent UEnemyThinkerBase::DecideIntent_Implementation()
{
    FEnemyIntent Intent;
    Intent.Owner = GetOwner();
    Intent.Actor = GetOwner();
    Intent.CurrentCell = GetCurrentGridPosition();

    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogEnemyThinker, Warning, TEXT("[DecideIntent] %s: World is NULL"), *GetNameSafe(GetOwner()));
        return Intent;
    }

    UDistanceFieldSubsystem* DistanceField = World->GetSubsystem<UDistanceFieldSubsystem>();
    if (!DistanceField)
    {
        UE_LOG(LogEnemyThinker, Warning, TEXT("[DecideIntent] %s: DistanceField is NULL"), *GetNameSafe(GetOwner()));
        return Intent;
    }

    UE_LOG(LogEnemyThinker, Warning, TEXT("[GetNextStep] ENTRY: EnemyCell=(%d,%d)"),
        Intent.CurrentCell.X, Intent.CurrentCell.Y);

    // CodeRevision: INC-2025-00030-R2 (Migrate to UGridPathfindingSubsystem) (2025-11-17 00:40)
    const UGridPathfindingSubsystem* GridPathfinding = CachedPathFinder.Get();

    if (GridPathfinding)
    {
        bool bCurrentWalkable = GridPathfinding->IsCellWalkableIgnoringActor(Intent.CurrentCell, Intent.Actor.Get());
        UE_LOG(LogEnemyThinker, Warning, TEXT("[PathFinder] Enemy at (%d,%d): Walkable=%d"), 
            Intent.CurrentCell.X, Intent.CurrentCell.Y, bCurrentWalkable ? 1 : 0);
        
        /*
        FIntPoint Neighbors[4] = {
            Intent.CurrentCell + FIntPoint(1, 0),   // Right
            Intent.CurrentCell + FIntPoint(-1, 0),  // Left
            Intent.CurrentCell + FIntPoint(0, 1),   // Up
            Intent.CurrentCell + FIntPoint(0, -1)   // Down
        };
        
        int32 BlockedCount = 0;
        for (int i = 0; i < 4; ++i)
        {
            // CodeRevision: INC-2025-00021-R1 (Replace IsCellWalkable with IsCellWalkableIgnoringActor - Phase 2.2) (2025-11-17 15:05)
            // Debug code: only terrain check needed
            bool bNeighborWalkable = GridPathfinding->IsCellWalkableIgnoringActor(Neighbors[i], Intent.Actor.Get());
            if (!bNeighborWalkable) BlockedCount++;
            UE_LOG(LogEnemyThinker, Warning, TEXT("[PathFinder] Neighbor[%d]=(%d,%d): Walkable=%d"), 
                i, Neighbors[i].X, Neighbors[i].Y, bNeighborWalkable ? 1 : 0);
        }
        
        UE_LOG(LogEnemyThinker, Warning, TEXT("[PathFinder] SUMMARY: %d/4 neighbors are blocked"), BlockedCount);
        */
    }
    else
    {
        UE_LOG(LogEnemyThinker, Error, TEXT("[PathFinder] GridPathfindingLibrary not found in cache"));
    }

    UE_LOG(LogEnemyThinker, Warning, TEXT("[Grid] Enemy at (%d, %d): checking surroundings"),
        Intent.CurrentCell.X, Intent.CurrentCell.Y);

    // CodeRevision: INC-2025-1123-LOG-R5 (Debug: Log player position before GetNextStep call) (2025-11-23 02:30)
    const FIntPoint PlayerPos = DistanceField->GetPlayerPosition();
    UE_LOG(LogEnemyThinker, Warning, TEXT("[GetNextStep] BEFORE CALL: Enemy=(%d,%d) Player=(%d,%d)"),
        Intent.CurrentCell.X, Intent.CurrentCell.Y, PlayerPos.X, PlayerPos.Y);

    const FIntPoint BeforeNextCell = Intent.NextCell;
    Intent.NextCell = DistanceField->GetNextStepTowardsPlayer(Intent.CurrentCell, GetOwner());

    // Log the result immediately after GetNextStep returns
    UE_LOG(LogEnemyThinker, Warning, TEXT("[GetNextStep] AFTER CALL: Enemy=(%d,%d) -> Next=(%d,%d)"),
        Intent.CurrentCell.X, Intent.CurrentCell.Y, Intent.NextCell.X, Intent.NextCell.Y);
    int32 Distance = DistanceField->GetDistance(Intent.CurrentCell);

    const int32 TileDistanceToPlayer = (Distance >= 0) ? (Distance / 10) : -1;

    const int32 CurrentDF = Distance;
    const FIntPoint NeighborOffsets[4] = {
        { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }
    };

    UE_LOG(LogEnemyThinker, Verbose,
        TEXT("[Thinker] %s DF(Current=%d) TargetCandidate=(%d,%d) Distance=%d"),
        *GetNameSafe(GetOwner()), CurrentDF, Intent.NextCell.X, Intent.NextCell.Y, Distance);

    FIntPoint BestCell = Intent.NextCell;
    int32 BestDF = Distance;

    for (int32 idx = 0; idx < 4; ++idx)
    {
        const FIntPoint TestCell = Intent.CurrentCell + NeighborOffsets[idx];
        const int32 NeighborDist = DistanceField->GetDistance(TestCell);
        UE_LOG(LogEnemyThinker, Verbose,
            TEXT("[Thinker] %s Neighbor[%d] Cell=(%d,%d) DF=%d"),
            *GetNameSafe(GetOwner()), idx, TestCell.X, TestCell.Y, NeighborDist);

        if (NeighborDist >= 0 && (BestDF < 0 || NeighborDist < BestDF))
        {
            BestCell = TestCell;
            BestDF = NeighborDist;
        }
    }

    if (BestDF >= 0 && (Distance < 0 || Intent.NextCell == Intent.CurrentCell || BestDF < Distance))
    {
        UE_LOG(LogEnemyThinker, Verbose,
            TEXT("[Thinker] %s Fallback target override: (%d,%d) DF=%d"),
            *GetNameSafe(GetOwner()), BestCell.X, BestCell.Y, BestDF);
        Intent.NextCell = BestCell;
        Distance = BestDF;
    }

    bool bPathFound = false;
    int32 PathLen = 0;
    /*
    if (GridPathfinding && Intent.NextCell != Intent.CurrentCell)
    {
        TArray<FVector> DebugPath;
        const FVector StartWorld = GridPathfinding->GridToWorld(Intent.CurrentCell, GetOwner()->GetActorLocation().Z);
        const FVector EndWorld = GridPathfinding->GridToWorld(Intent.NextCell, GetOwner()->GetActorLocation().Z);
        UE_LOG(LogEnemyThinker, Verbose,
            TEXT("[Thinker] %s FindPath CALL Start=(%d,%d) End=(%d,%d)"),
            *GetNameSafe(GetOwner()), Intent.CurrentCell.X, Intent.CurrentCell.Y, Intent.NextCell.X, Intent.NextCell.Y);
        // CodeRevision: INC-2025-00030-R2 (Fix MaxDXDY -> Chebyshev heuristic) (2025-11-17 00:40)
        bPathFound = GridPathfinding->FindPathIgnoreEndpoints(StartWorld, EndWorld, DebugPath, true, EGridHeuristic::Chebyshev, 200000, true);
        PathLen = DebugPath.Num();
        UE_LOG(LogEnemyThinker, Verbose,
            TEXT("[Thinker] %s FindPath RESULT Success=%d PathLen=%d"),
            *GetNameSafe(GetOwner()), bPathFound ? 1 : 0, PathLen);
    }
    */
    
    if (Intent.NextCell.X < -100 || Intent.NextCell.X > 100 || Intent.NextCell.Y < -100 || Intent.NextCell.Y > 100)
    {
        UE_LOG(LogEnemyThinker, Error,
            TEXT("[DecideIntent] CRITICAL: DistanceField returned ABNORMAL NextCell=(%d,%d) for CurrentCell=(%d,%d)"),
            Intent.NextCell.X, Intent.NextCell.Y, Intent.CurrentCell.X, Intent.CurrentCell.Y);
        UE_LOG(LogEnemyThinker, Error,
            TEXT("[DecideIntent] Distance=%d, This indicates DistanceField data corruption or uninitialized state!"),
            Distance);
    }
    else
    {
        UE_LOG(LogEnemyThinker, Verbose,
            TEXT("[DecideIntent] %s: CurrentCell=(%d,%d) NextCell=(%d,%d), Distance=%d"),
            *GetNameSafe(GetOwner()), Intent.CurrentCell.X, Intent.CurrentCell.Y,
            Intent.NextCell.X, Intent.NextCell.Y, Distance);
    }

    if (Intent.NextCell == Intent.CurrentCell)
    {
        UE_LOG(LogEnemyThinker, Verbose, TEXT("[GetNextStep] Distance=-1 (intended: far enemy skipped)"));
        UE_LOG(LogEnemyThinker, Verbose, TEXT("[GetNextStep] Distance=%d (should be -1 if unreachable)"), Distance);
    }
    else
    {
        UE_LOG(LogEnemyThinker, Verbose, TEXT("[GetNextStep] NextStep=(%d,%d) from (%d,%d)"),
            Intent.NextCell.X, Intent.NextCell.Y, Intent.CurrentCell.X, Intent.CurrentCell.Y);
    }

    UE_LOG(LogEnemyThinker, Verbose, 
        TEXT("[DecideIntent] %s: CurrentCell=(%d,%d), NextCell=(%d,%d), DF_Cost=%d, Tiles=%d, AttackRange=%d"),
        *GetNameSafe(GetOwner()), 
        Intent.CurrentCell.X, Intent.CurrentCell.Y,
        Intent.NextCell.X, Intent.NextCell.Y,
        Distance, TileDistanceToPlayer, AttackRangeInTiles);

    const int32 DistanceToPlayer = TileDistanceToPlayer; // Convert Dijkstra cost to tile units for range comparisons

    AActor* PlayerActor = UGameplayStatics::GetPlayerPawn(World, 0);

    // 角抜け防止：攻撃前に射線チェックを実施
    bool bHasLineOfSight = false;
    if (DistanceToPlayer >= 0 && DistanceToPlayer <= AttackRangeInTiles)
    {
        // GridPathfindingは既にline 60で宣言済み、再利用する
        if (GridPathfinding && PlayerActor)
        {
            const FIntPoint PlayerGridPos = GridPathfinding->WorldToGrid(PlayerActor->GetActorLocation());
            const FVector StartWorld = GridPathfinding->GridToWorld(Intent.CurrentCell);
            const FVector EndWorld = GridPathfinding->GridToWorld(PlayerGridPos);
            bHasLineOfSight = GridPathfinding->HasLineOfSight(StartWorld, EndWorld);

            // 対角攻撃の場合、両方の肩セルがwalkableかチェック（角抜け防止）
            if (bHasLineOfSight)
            {
                const int32 dx = FMath::Abs(PlayerGridPos.X - Intent.CurrentCell.X);
                const int32 dy = FMath::Abs(PlayerGridPos.Y - Intent.CurrentCell.Y);

                // 対角攻撃（dx == 1 && dy == 1）の場合のみチェック
                if (dx == 1 && dy == 1)
                {
                    const FIntPoint Delta = PlayerGridPos - Intent.CurrentCell;
                    const FIntPoint Shoulder1 = Intent.CurrentCell + FIntPoint(Delta.X, 0);
                    const FIntPoint Shoulder2 = Intent.CurrentCell + FIntPoint(0, Delta.Y);

                    const bool bShoulder1Walkable = GridPathfinding->IsCellWalkableIgnoringActor(Shoulder1, nullptr);
                    const bool bShoulder2Walkable = GridPathfinding->IsCellWalkableIgnoringActor(Shoulder2, nullptr);

                    if (!bShoulder1Walkable || !bShoulder2Walkable)
                    {
                        bHasLineOfSight = false;
                        UE_LOG(LogEnemyThinker, Log,
                            TEXT("[DecideIntent] %s: Diagonal attack blocked by corner (Enemy=%d,%d Player=%d,%d Shoulder1=%d Shoulder2=%d)"),
                            *GetNameSafe(GetOwner()),
                            Intent.CurrentCell.X, Intent.CurrentCell.Y,
                            PlayerGridPos.X, PlayerGridPos.Y,
                            bShoulder1Walkable ? 1 : 0, bShoulder2Walkable ? 1 : 0);
                    }
                }
            }

            if (!bHasLineOfSight)
            {
                UE_LOG(LogEnemyThinker, Verbose,
                    TEXT("[DecideIntent] %s: No line of sight to player (Distance=%d) - switching to MOVE"),
                    *GetNameSafe(GetOwner()), DistanceToPlayer);
            }
        }
        else
        {
            // PathFindingがない場合は、射線チェックをスキップ（従来の動作）
            bHasLineOfSight = true;
        }
    }

    if (DistanceToPlayer >= 0 && DistanceToPlayer <= AttackRangeInTiles && bHasLineOfSight)
    {
        // INC-2025-0002: RogueGameplayTags経由でタグを取得（文字列ベースのRequestを廃止）
        Intent.AbilityTag = RogueGameplayTags::AI_Intent_Attack;
        Intent.NextCell = Intent.CurrentCell;

        if (PlayerActor)
        {
            Intent.Target = PlayerActor;
            Intent.TargetActor = PlayerActor;

            UE_LOG(LogEnemyThinker, Log, TEXT("[DecideIntent] %s: ATTACK intent (Distance=%d, Range=%d, Target=%s)"),
                *GetNameSafe(GetOwner()), DistanceToPlayer, AttackRangeInTiles, *GetNameSafe(PlayerActor));
        }
        else
        {
            UE_LOG(LogEnemyThinker, Warning, TEXT("[DecideIntent] %s: ATTACK intent (Distance=%d, Range=%d) but Player not found!"),
                *GetNameSafe(GetOwner()), DistanceToPlayer, AttackRangeInTiles);
        }
    }
    else
    {
        // INC-2025-0002: RogueGameplayTags経由でタグを取得（文字列ベースのRequestを廃止）
        Intent.AbilityTag = RogueGameplayTags::AI_Intent_Move;

        if (Intent.NextCell == Intent.CurrentCell)
        {
        // INC-2025-0002: RogueGameplayTags経由でタグを取得（文字列ベースのRequestを廃止）
        Intent.AbilityTag = RogueGameplayTags::AI_Intent_Wait;
        UE_LOG(LogEnemyThinker, Log, TEXT("[Thinker] %s WAIT - NextCell identical to current"),
            *GetNameSafe(GetOwner()));
        }
        else
        {
            UE_LOG(LogEnemyThinker, Log, TEXT("[DecideIntent] %s: MOVE intent (Distance=%d > Range=%d)"),
                *GetNameSafe(GetOwner()), DistanceToPlayer, AttackRangeInTiles);
        }
    }

    return Intent;
}

FEnemyIntent UEnemyThinkerBase::ComputeIntent_Implementation(const FEnemyObservation& Observation)
{
    FEnemyIntent Intent;
    Intent.Owner = GetOwner();
    Intent.Actor = GetOwner();
    Intent.CurrentCell = Observation.GridPosition;

    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogEnemyThinker, Warning, TEXT("[ComputeIntent] %s: World is NULL"), *GetNameSafe(GetOwner()));
        return Intent;
    }

    const int32 ObservationDistance = Observation.DistanceInTiles;
    const FIntPoint PlayerGridCell = Observation.PlayerGridPosition;
    int32 DistanceToPlayer = ObservationDistance;

    const FIntPoint CurrentEnemyCell = Intent.CurrentCell;
    // CodeRevision: INC-2025-00016-R1 (Use FGridUtils::ChebyshevDistance) (2025-11-16 14:00)
    const int32 ChebyshevDistance = FGridUtils::ChebyshevDistance(CurrentEnemyCell, PlayerGridCell);
    if (DistanceToPlayer != ChebyshevDistance)
    {
        UE_LOG(LogEnemyThinker, Log,
            TEXT("[ComputeIntent] %s: Chebyshev distance (%d) differs from observation (%d)"),
            *GetNameSafe(GetOwner()), ChebyshevDistance, DistanceToPlayer);
    }
    DistanceToPlayer = ChebyshevDistance;
    AActor* PlayerActor = UGameplayStatics::GetPlayerPawn(World, 0);

    // 角抜け防止：攻撃前に射線チェックを実施
    bool bHasLineOfSight = false;
    if (DistanceToPlayer >= 0 && DistanceToPlayer <= AttackRangeInTiles)
    {
        // GridPathfindingSubsystemを取得（キャッシュまたは新規取得）
        const UGridPathfindingSubsystem* PathfindingForLOS = CachedPathFinder.Get();
        if (!PathfindingForLOS)
        {
            PathfindingForLOS = World->GetSubsystem<UGridPathfindingSubsystem>();
        }

        if (PathfindingForLOS)
        {
            const FVector StartWorld = PathfindingForLOS->GridToWorld(Intent.CurrentCell);
            const FVector EndWorld = PathfindingForLOS->GridToWorld(PlayerGridCell);
            bHasLineOfSight = PathfindingForLOS->HasLineOfSight(StartWorld, EndWorld);

            // 対角攻撃の場合、両方の肩セルがwalkableかチェック（角抜け防止）
            if (bHasLineOfSight)
            {
                const int32 dx = FMath::Abs(PlayerGridCell.X - Intent.CurrentCell.X);
                const int32 dy = FMath::Abs(PlayerGridCell.Y - Intent.CurrentCell.Y);

                // 対角攻撃（dx == 1 && dy == 1）の場合のみチェック
                if (dx == 1 && dy == 1)
                {
                    const FIntPoint Delta = PlayerGridCell - Intent.CurrentCell;
                    const FIntPoint Shoulder1 = Intent.CurrentCell + FIntPoint(Delta.X, 0);
                    const FIntPoint Shoulder2 = Intent.CurrentCell + FIntPoint(0, Delta.Y);

                    const bool bShoulder1Walkable = PathfindingForLOS->IsCellWalkableIgnoringActor(Shoulder1, nullptr);
                    const bool bShoulder2Walkable = PathfindingForLOS->IsCellWalkableIgnoringActor(Shoulder2, nullptr);

                    if (!bShoulder1Walkable || !bShoulder2Walkable)
                    {
                        bHasLineOfSight = false;
                        UE_LOG(LogEnemyThinker, Log,
                            TEXT("[ComputeIntent] %s: Diagonal attack blocked by corner (Enemy=%d,%d Player=%d,%d Shoulder1=%d Shoulder2=%d)"),
                            *GetNameSafe(GetOwner()),
                            Intent.CurrentCell.X, Intent.CurrentCell.Y,
                            PlayerGridCell.X, PlayerGridCell.Y,
                            bShoulder1Walkable ? 1 : 0, bShoulder2Walkable ? 1 : 0);
                    }
                }
            }

            if (!bHasLineOfSight)
            {
                UE_LOG(LogEnemyThinker, Verbose,
                    TEXT("[ComputeIntent] %s: No line of sight to player (TileDistance=%d) - switching to MOVE"),
                    *GetNameSafe(GetOwner()), DistanceToPlayer);
            }
        }
        else
        {
            // PathFindingがない場合は、射線チェックをスキップ（従来の動作）
            bHasLineOfSight = true;
        }
    }

    if (DistanceToPlayer >= 0 && DistanceToPlayer <= AttackRangeInTiles && bHasLineOfSight)
    {
        // INC-2025-0002: RogueGameplayTags経由でタグを取得（文字列ベースのRequestを廃止）
        Intent.AbilityTag = RogueGameplayTags::AI_Intent_Attack;
        Intent.NextCell = Intent.CurrentCell;

        if (PlayerActor)
        {
            Intent.Target = PlayerActor;
            Intent.TargetActor = PlayerActor;
            UE_LOG(LogEnemyThinker, Log,
                TEXT("[ComputeIntent] %s: ATTACK intent (TileDistance=%d, Range=%d, Target=%s)"),
                *GetNameSafe(GetOwner()), DistanceToPlayer, AttackRangeInTiles, *GetNameSafe(PlayerActor));
        }
        else
        {
            UE_LOG(LogEnemyThinker, Warning,
                TEXT("[ComputeIntent] %s: ATTACK intent (TileDistance=%d, Range=%d) but Player not found!"),
                *GetNameSafe(GetOwner()), DistanceToPlayer, AttackRangeInTiles);
        }
    }
    else
    {
        // INC-2025-0002: RogueGameplayTags経由でタグを取得（文字列ベースのRequestを廃止）
        Intent.AbilityTag = RogueGameplayTags::AI_Intent_Move;

        if (UDistanceFieldSubsystem* DistanceField = World->GetSubsystem<UDistanceFieldSubsystem>())
        {
            // CodeRevision: INC-2025-1123-LOG-R2 (Add movement decision logs to EnemyThinkerBase::ComputeIntent) (2025-11-23 01:52)
            // Log before calling GetNextStep for diagnostics
            UE_LOG(LogEnemyThinker, Log,
                TEXT("[ComputeIntent] %s: Calling GetNextStep from (%d,%d) to Player at (%d,%d)"),
                *GetNameSafe(GetOwner()), Intent.CurrentCell.X, Intent.CurrentCell.Y,
                PlayerGridCell.X, PlayerGridCell.Y);

            // CodeRevision: INC-2025-1123-LOG-R6 (Add DistanceField debug info) (2025-11-23 02:45)
            // Log DistanceField's stored PlayerPosition and current cell distance
            const FIntPoint DFPlayerPos = DistanceField->GetPlayerPosition();
            const int32 CurrentDist = DistanceField->GetDistance(Intent.CurrentCell);
            UE_LOG(LogEnemyThinker, Warning,
                TEXT("[ComputeIntent] %s: DF PlayerPos=(%d,%d), CurrentCellDist=%d"),
                *GetNameSafe(GetOwner()), DFPlayerPos.X, DFPlayerPos.Y, CurrentDist);

            // CodeRevision: INC-2025-1124-R1 (Delegate move validation to CoreResolvePhase) (2025-11-24 09:30)
            Intent.NextCell = DistanceField->GetNextStepTowardsPlayer(Intent.CurrentCell, GetOwner());

            // Log the result of GetNextStep with neighbor distances
            const bool bMoved = (Intent.NextCell != Intent.CurrentCell);
            const int32 NextDist = DistanceField->GetDistance(Intent.NextCell);
            UE_LOG(LogEnemyThinker, Warning,
                TEXT("[ComputeIntent] %s: GetNextStep returned (%d,%d) (moved=%d, NextDist=%d)"),
                *GetNameSafe(GetOwner()), Intent.NextCell.X, Intent.NextCell.Y, bMoved ? 1 : 0, NextDist);

            // Log all 8 neighbor distances for debugging
            static const FIntPoint Offsets[] = {{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
            FString NeighborInfo;
            for (const FIntPoint& Off : Offsets)
            {
                const FIntPoint N = Intent.CurrentCell + Off;
                const int32 NDist = DistanceField->GetDistance(N);
                NeighborInfo += FString::Printf(TEXT("(%d,%d)=%d "), N.X, N.Y, NDist);
            }
            UE_LOG(LogEnemyThinker, Warning, TEXT("[ComputeIntent] %s: Neighbors: %s"),
                *GetNameSafe(GetOwner()), *NeighborInfo);
        }
        else
        {
            Intent.NextCell = Intent.CurrentCell;
            UE_LOG(LogEnemyThinker, Warning,
                TEXT("[ComputeIntent] %s: DistanceField not available, staying put"),
                *GetNameSafe(GetOwner()));
        }

        if (Intent.NextCell == Intent.CurrentCell)
        {
            // INC-2025-0002: RogueGameplayTags経由でタグを取得（文字列ベースのRequestを廃止）
            Intent.AbilityTag = RogueGameplayTags::AI_Intent_Wait;
            UE_LOG(LogEnemyThinker, Log,
                TEXT("[ComputeIntent] %s WAIT - NextCell identical to current (TileDistance=%d)"),
                *GetNameSafe(GetOwner()), DistanceToPlayer);
        }
        else
        {
            UE_LOG(LogEnemyThinker, Log,
                TEXT("[ComputeIntent] %s: MOVE intent (TileDistance=%d > Range=%d)"),
                *GetNameSafe(GetOwner()), DistanceToPlayer, AttackRangeInTiles);
        }
    }

    return Intent;
}

int32 UEnemyThinkerBase::GetMaxAttackRangeInTiles() const
{
    return AttackRangeInTiles;
}

FGameplayTag UEnemyThinkerBase::GetAttackAbilityForRange(int32 DistanceInTiles) const
{
    if (DistanceInTiles <= AttackRangeInTiles)
    {
        // INC-2025-0002: RogueGameplayTags経由でタグを取得（文字列ベースのRequestを廃止）
        return RogueGameplayTags::AI_Intent_Attack;
    }

    return FGameplayTag();
}

UAbilitySystemComponent* UEnemyThinkerBase::GetOwnerAbilitySystemComponent() const
{
    if (AActor* Owner = GetOwner())
    {
        return Owner->FindComponentByClass<UAbilitySystemComponent>();
    }

    return nullptr;
}

FIntPoint UEnemyThinkerBase::GetCurrentGridPosition() const
{
    if (AActor* Owner = GetOwner())
    {
        UWorld* World = GetWorld();
        if (!World)
        {
            return FIntPoint(0, 0);
        }

        // CodeRevision: INC-2025-00030-R2 (Migrate to UGridPathfindingSubsystem) (2025-11-17 00:40)
        if (UGridPathfindingSubsystem* GridLib = World->GetSubsystem<UGridPathfindingSubsystem>())
        {
            FVector WorldLocation = Owner->GetActorLocation();
            return GridLib->WorldToGrid(WorldLocation);
        }

        FVector WorldLocation = Owner->GetActorLocation();
        const float TileSize = 100.0f;

        int32 GridX = FMath::RoundToInt(WorldLocation.X / TileSize);
        int32 GridY = FMath::RoundToInt(WorldLocation.Y / TileSize);

        UE_LOG(LogEnemyThinker, Warning, TEXT("[GetCurrentGridPosition] %s: Using fallback calculation (%d,%d)"),
            *GetNameSafe(Owner), GridX, GridY);

        return FIntPoint(GridX, GridY);
    }

    return FIntPoint(0, 0);
}

int32 UEnemyThinkerBase::GetDistanceToPlayer() const
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return -1;
    }

    UDistanceFieldSubsystem* DistanceField = World->GetSubsystem<UDistanceFieldSubsystem>();
    if (!DistanceField)
    {
        return -1;
    }

    FIntPoint CurrentCell = GetCurrentGridPosition();
    return DistanceField->GetDistance(CurrentCell);
}
