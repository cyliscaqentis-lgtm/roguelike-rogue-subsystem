// EnemyThinkerBase.cpp
#include "EnemyThinkerBase.h"
#include "AbilitySystemComponent.h"
#include "AI/Enemy/EnemyThinkerBase.h"
#include "Turn/DistanceFieldSubsystem.h"
#include "Grid/GridPathfindingLibrary.h"
#include "Utility/GridUtils.h"  // CodeRevision: INC-2025-00016-R1 (2025-11-16 14:00)
#include "Kismet/GameplayStatics.h"
#include "../../Grid/GridPathfindingLibrary.h"
#include "../../Turn/GameTurnManagerBase.h"
#include "EngineUtils.h"

UEnemyThinkerBase::UEnemyThinkerBase()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UEnemyThinkerBase::BeginPlay()
{
    Super::BeginPlay();
    
    UWorld* World = GetWorld();
    if (World)
    {
        for (TActorIterator<AGridPathfindingLibrary> It(World); It; ++It)
        {
            CachedPathFinder = *It;
            UE_LOG(LogTemp, Log, TEXT("[EnemyThinker] PathFinder cached: %s"), *CachedPathFinder->GetName());
            break;
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
        UE_LOG(LogTemp, Warning, TEXT("[DecideIntent] %s: World is NULL"), *GetNameSafe(GetOwner()));
        return Intent;
    }

    UDistanceFieldSubsystem* DistanceField = World->GetSubsystem<UDistanceFieldSubsystem>();
    if (!DistanceField)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DecideIntent] %s: DistanceField is NULL"), *GetNameSafe(GetOwner()));
        return Intent;
    }

    UE_LOG(LogTemp, Warning, TEXT("[GetNextStep] ENTRY: EnemyCell=(%d,%d)"),
        Intent.CurrentCell.X, Intent.CurrentCell.Y);

    const AGridPathfindingLibrary* GridPathfinding = CachedPathFinder.Get();
    
    if (GridPathfinding)
    {
        bool bCurrentWalkable = GridPathfinding->IsCellWalkableIgnoringActor(Intent.CurrentCell, Intent.Actor.Get());
        UE_LOG(LogTemp, Warning, TEXT("[PathFinder] Enemy at (%d,%d): Walkable=%d"), 
            Intent.CurrentCell.X, Intent.CurrentCell.Y, bCurrentWalkable ? 1 : 0);
        
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
            UE_LOG(LogTemp, Warning, TEXT("[PathFinder] Neighbor[%d]=(%d,%d): Walkable=%d"), 
                i, Neighbors[i].X, Neighbors[i].Y, bNeighborWalkable ? 1 : 0);
        }
        
        UE_LOG(LogTemp, Warning, TEXT("[PathFinder] SUMMARY: %d/4 neighbors are blocked"), BlockedCount);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[PathFinder] GridPathfindingLibrary not found in cache"));
    }

    UE_LOG(LogTemp, Warning, TEXT("[Grid] Enemy at (%d, %d): checking surroundings"), 
        Intent.CurrentCell.X, Intent.CurrentCell.Y);

    const FIntPoint BeforeNextCell = Intent.NextCell;
    Intent.NextCell = DistanceField->GetNextStepTowardsPlayer(Intent.CurrentCell, GetOwner());
    int32 Distance = DistanceField->GetDistance(Intent.CurrentCell);

    const int32 TileDistanceToPlayer = (Distance >= 0) ? (Distance / 10) : -1;

    const int32 CurrentDF = Distance;
    const FIntPoint NeighborOffsets[4] = {
        { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }
    };

    UE_LOG(LogTemp, Verbose,
        TEXT("[Thinker] %s DF(Current=%d) TargetCandidate=(%d,%d) Distance=%d"),
        *GetNameSafe(GetOwner()), CurrentDF, Intent.NextCell.X, Intent.NextCell.Y, Distance);

    FIntPoint BestCell = Intent.NextCell;
    int32 BestDF = Distance;

    for (int32 idx = 0; idx < 4; ++idx)
    {
        const FIntPoint TestCell = Intent.CurrentCell + NeighborOffsets[idx];
        const int32 NeighborDist = DistanceField->GetDistance(TestCell);
        UE_LOG(LogTemp, Verbose,
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
        UE_LOG(LogTemp, Verbose,
            TEXT("[Thinker] %s Fallback target override: (%d,%d) DF=%d"),
            *GetNameSafe(GetOwner()), BestCell.X, BestCell.Y, BestDF);
        Intent.NextCell = BestCell;
        Distance = BestDF;
    }

    bool bPathFound = false;
    int32 PathLen = 0;
    if (GridPathfinding && Intent.NextCell != Intent.CurrentCell)
    {
        TArray<FVector> DebugPath;
        const FVector StartWorld = GridPathfinding->GridToWorld(Intent.CurrentCell, GetOwner()->GetActorLocation().Z);
        const FVector EndWorld = GridPathfinding->GridToWorld(Intent.NextCell, GetOwner()->GetActorLocation().Z);
        UE_LOG(LogTemp, Verbose,
            TEXT("[Thinker] %s FindPath CALL Start=(%d,%d) End=(%d,%d)"),
            *GetNameSafe(GetOwner()), Intent.CurrentCell.X, Intent.CurrentCell.Y, Intent.NextCell.X, Intent.NextCell.Y);
        bPathFound = GridPathfinding->FindPathIgnoreEndpoints(StartWorld, EndWorld, DebugPath, true, EGridHeuristic::MaxDXDY, 200000, true);
        PathLen = DebugPath.Num();
        UE_LOG(LogTemp, Verbose,
            TEXT("[Thinker] %s FindPath RESULT Success=%d PathLen=%d"),
            *GetNameSafe(GetOwner()), bPathFound ? 1 : 0, PathLen);
    }
    
    if (Intent.NextCell.X < -100 || Intent.NextCell.X > 100 || Intent.NextCell.Y < -100 || Intent.NextCell.Y > 100)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[DecideIntent] CRITICAL: DistanceField returned ABNORMAL NextCell=(%d,%d) for CurrentCell=(%d,%d)"),
            Intent.NextCell.X, Intent.NextCell.Y, Intent.CurrentCell.X, Intent.CurrentCell.Y);
        UE_LOG(LogTemp, Error,
            TEXT("[DecideIntent] Distance=%d, This indicates DistanceField data corruption or uninitialized state!"),
            Distance);
    }
    else
    {
        UE_LOG(LogTemp, Verbose,
            TEXT("[DecideIntent] %s: CurrentCell=(%d,%d) NextCell=(%d,%d), Distance=%d"),
            *GetNameSafe(GetOwner()), Intent.CurrentCell.X, Intent.CurrentCell.Y,
            Intent.NextCell.X, Intent.NextCell.Y, Distance);
    }

    if (Intent.NextCell == Intent.CurrentCell)
    {
        UE_LOG(LogTurnManager, Verbose, TEXT("[GetNextStep] Distance=-1 (intended: far enemy skipped)"));
        UE_LOG(LogTurnManager, Verbose, TEXT("[GetNextStep] Distance=%d (should be -1 if unreachable)"), Distance);
    }
    else
    {
        UE_LOG(LogTemp, Verbose, TEXT("[GetNextStep] NextStep=(%d,%d) from (%d,%d)"),
            Intent.NextCell.X, Intent.NextCell.Y, Intent.CurrentCell.X, Intent.CurrentCell.Y);
    }

    UE_LOG(LogTurnManager, Verbose, 
        TEXT("[DecideIntent] %s: CurrentCell=(%d,%d), NextCell=(%d,%d), DF_Cost=%d, Tiles=%d, AttackRange=%d"),
        *GetNameSafe(GetOwner()), 
        Intent.CurrentCell.X, Intent.CurrentCell.Y,
        Intent.NextCell.X, Intent.NextCell.Y,
        Distance, TileDistanceToPlayer, AttackRangeInTiles);

    const int32 DistanceToPlayer = TileDistanceToPlayer; // Convert Dijkstra cost to tile units for range comparisons

    AActor* PlayerActor = UGameplayStatics::GetPlayerPawn(World, 0);

    if (DistanceToPlayer >= 0 && DistanceToPlayer <= AttackRangeInTiles)
    {
        Intent.AbilityTag = FGameplayTag::RequestGameplayTag(TEXT("AI.Intent.Attack"));
        Intent.NextCell = Intent.CurrentCell;

        if (PlayerActor)
        {
            Intent.Target = PlayerActor;
            Intent.TargetActor = PlayerActor;

            UE_LOG(LogTemp, Log, TEXT("[DecideIntent] %s: ATTACK intent (Distance=%d, Range=%d, Target=%s)"),
                *GetNameSafe(GetOwner()), DistanceToPlayer, AttackRangeInTiles, *GetNameSafe(PlayerActor));
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[DecideIntent] %s: ATTACK intent (Distance=%d, Range=%d) but Player not found!"),
                *GetNameSafe(GetOwner()), DistanceToPlayer, AttackRangeInTiles);
        }
    }
    else
    {
        Intent.AbilityTag = FGameplayTag::RequestGameplayTag(TEXT("AI.Intent.Move"));
        
        if (Intent.NextCell == Intent.CurrentCell)
        {
        Intent.AbilityTag = FGameplayTag::RequestGameplayTag(TEXT("AI.Intent.Wait"));
        UE_LOG(LogTurnManager, Log, TEXT("[Thinker] %s WAIT - NextCell identical to current"),
            *GetNameSafe(GetOwner()));
        }
        else
        {
            UE_LOG(LogTemp, Log, TEXT("[DecideIntent] %s: MOVE intent (Distance=%d > Range=%d)"),
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
        UE_LOG(LogTemp, Warning, TEXT("[ComputeIntent] %s: World is NULL"), *GetNameSafe(GetOwner()));
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
        UE_LOG(LogTurnManager, Log,
            TEXT("[ComputeIntent] %s: Chebyshev distance (%d) differs from observation (%d)"),
            *GetNameSafe(GetOwner()), ChebyshevDistance, DistanceToPlayer);
    }
    DistanceToPlayer = ChebyshevDistance;
    AActor* PlayerActor = UGameplayStatics::GetPlayerPawn(World, 0);

    if (DistanceToPlayer >= 0 && DistanceToPlayer <= AttackRangeInTiles)
    {
        Intent.AbilityTag = FGameplayTag::RequestGameplayTag(TEXT("AI.Intent.Attack"));
        Intent.NextCell = Intent.CurrentCell;

        if (PlayerActor)
        {
            Intent.Target = PlayerActor;
            Intent.TargetActor = PlayerActor;
            UE_LOG(LogTemp, Log,
                TEXT("[ComputeIntent] %s: ATTACK intent (TileDistance=%d, Range=%d, Target=%s)"),
                *GetNameSafe(GetOwner()), DistanceToPlayer, AttackRangeInTiles, *GetNameSafe(PlayerActor));
        }
        else
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[ComputeIntent] %s: ATTACK intent (TileDistance=%d, Range=%d) but Player not found!"),
                *GetNameSafe(GetOwner()), DistanceToPlayer, AttackRangeInTiles);
        }
    }
    else
    {
        Intent.AbilityTag = FGameplayTag::RequestGameplayTag(TEXT("AI.Intent.Move"));

        if (UDistanceFieldSubsystem* DistanceField = World->GetSubsystem<UDistanceFieldSubsystem>())
        {
            Intent.NextCell = DistanceField->GetNextStepTowardsPlayer(Intent.CurrentCell, GetOwner());
        }
        else
        {
            Intent.NextCell = Intent.CurrentCell;
            UE_LOG(LogTemp, Warning,
                TEXT("[ComputeIntent] %s: DistanceField not available, staying put"),
                *GetNameSafe(GetOwner()));
        }

        // ★★★ CodeRevision: INC-2025-00016-R1 (Add IsMoveValid validation) (2025-11-16 14:00) ★★★
        // Validate move using unified API before committing to intent
        if (Intent.NextCell != Intent.CurrentCell)
        {
            AGridPathfindingLibrary* PathFinder = nullptr;
            for (TActorIterator<AGridPathfindingLibrary> It(World); It; ++It)
            {
                PathFinder = *It;
                break;
            }

            if (PathFinder)
            {
                FString FailureReason;
                const bool bMoveValid = PathFinder->IsMoveValid(
                    Intent.CurrentCell,
                    Intent.NextCell,
                    GetOwner(),
                    FailureReason);

                if (!bMoveValid)
                {
                    UE_LOG(LogTurnManager, Warning,
                        TEXT("[ComputeIntent] %s: MOVE rejected by validation: %s | From=(%d,%d) To=(%d,%d)"),
                        *GetNameSafe(GetOwner()), *FailureReason,
                        Intent.CurrentCell.X, Intent.CurrentCell.Y,
                        Intent.NextCell.X, Intent.NextCell.Y);

                    // Fallback to wait if move is invalid
                    Intent.NextCell = Intent.CurrentCell;
                    Intent.AbilityTag = FGameplayTag::RequestGameplayTag(TEXT("AI.Intent.Wait"));
                }
            }
        }

        if (Intent.NextCell == Intent.CurrentCell)
        {
            Intent.AbilityTag = FGameplayTag::RequestGameplayTag(TEXT("AI.Intent.Wait"));
            UE_LOG(LogTurnManager, Log,
                TEXT("[ComputeIntent] %s WAIT - NextCell identical to current (TileDistance=%d)"),
                *GetNameSafe(GetOwner()), DistanceToPlayer);
        }
        else
        {
            UE_LOG(LogTemp, Log,
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
        return FGameplayTag::RequestGameplayTag(FName("AI.Intent.Attack"));
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

        for (TActorIterator<AGridPathfindingLibrary> It(World); It; ++It)
        {
            AGridPathfindingLibrary* GridLib = *It;
            if (GridLib)
            {
                FVector WorldLocation = Owner->GetActorLocation();
                return GridLib->WorldToGrid(WorldLocation);
            }
        }

        FVector WorldLocation = Owner->GetActorLocation();
        const float TileSize = 100.0f;

        int32 GridX = FMath::RoundToInt(WorldLocation.X / TileSize);
        int32 GridY = FMath::RoundToInt(WorldLocation.Y / TileSize);

        UE_LOG(LogTemp, Warning, TEXT("[GetCurrentGridPosition] %s: Using fallback calculation (%d,%d)"),
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
