// Copyright Epic Games, Inc. All Rights Reserved.

#include "DistanceFieldSubsystem.h"
#include "../Grid/GridOccupancySubsystem.h"
#include "../Grid/GridPathfindingLibrary.h"
#include "../ProjectDiagnostics.h"  // ☁E E☁EDIAG_LOG用 ☁E E☁E
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

// ☁E E☁ECVar定義 ☁E E☁E
static int32 GTS_DF_MaxCells = 300000;  // ☁E E☁Edefault拡張 E E4x64以上 Eマップで余裕を持たせる ☁E E☁E
static FAutoConsoleVariableRef CVarTS_DF_MaxCells(
    TEXT("ts.DistanceField.MaxCells"),
    GTS_DF_MaxCells,
    TEXT("探索セル数の上限（フリーズ防止）"),
    ECVF_Default
);

static int32 GTS_DF_AllowDiag = 1;
static FAutoConsoleVariableRef CVarTS_DF_AllowDiag(
    TEXT("ts.DistanceField.AllowDiagonal"),
    GTS_DF_AllowDiag,
    TEXT("斜め移動を許可 (0=無効, 1=有効)"),
    ECVF_Default
);

void UDistanceFieldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    
    // ☁E E☁EPathFinderをキャチE  ュ E  E期化時に存在しなぁE  合 E遁E  取得！E☁E E☁E
    TArray<AActor*> FoundActors;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), AGridPathfindingLibrary::StaticClass(), FoundActors);
    if (FoundActors.Num() > 0)
    {
        CachedPathFinder = Cast<AGridPathfindingLibrary>(FoundActors[0]);
        UE_LOG(LogTemp, Log, TEXT("[DistanceField] PathFinder cached: %s"), *CachedPathFinder->GetName());
    }
    // ☁E E☁E警告削除 E  E期化頁E  によりPathFinderがまだ存在しなぁE  合があるが、GetPathFinder()で遁E  取得される ☁E E☁E
    
    UE_LOG(LogTemp, Log, TEXT("[DistanceField] Initialized (PathFinder will be lazily loaded if needed)"));
}

void UDistanceFieldSubsystem::Deinitialize()
{
    DistanceMap.Empty();
    NextStepMap.Empty();
    UE_LOG(LogTemp, Log, TEXT("[DistanceField] Deinitialized"));
    Super::Deinitialize();
}

// ☁E E☁EBlueprint用 E シンプル牁E☁E E☁E
void UDistanceFieldSubsystem::UpdateDistanceField(const FIntPoint& PlayerCell)
{
    // ☁E E☁EBoundsMarginめE00に拡大 E 遠距離の敵もカバ E ☁E E☁E
    // マップ E域（紁E00x100タイル E を想宁E
    UpdateDistanceFieldInternal(PlayerCell, TSet<FIntPoint>(), 100);
}

// ☁E E☁EC++用 E 最適化版 ☁E E☁E
void UDistanceFieldSubsystem::UpdateDistanceFieldOptimized(const FIntPoint& PlayerCell,
    const TSet<FIntPoint>& OptionalTargets,
    int32 BoundsMargin)
{
    UpdateDistanceFieldInternal(PlayerCell, OptionalTargets, BoundsMargin);
}

// ☁E E☁E優先度キュー用ヘルチE☁E E☁E
struct FOpenNode
{
    FIntPoint Cell;
    int32 Cost;
};

struct FOpenNodeLess
{
    bool operator()(const FOpenNode& A, const FOpenNode& B) const
    {
        return A.Cost > B.Cost; // min-heap
    }
};

// ☁E E☁E冁E  実裁E   E通ロジチE   ☁E E☁E
void UDistanceFieldSubsystem::UpdateDistanceFieldInternal(const FIntPoint& PlayerCell,
    const TSet<FIntPoint>& OptionalTargets,
    int32 BoundsMargin)
{
    DistanceMap.Empty();
    NextStepMap.Empty();
    PlayerPosition = PlayerCell;
    
    // ☁E E☁EBoundsを E期化 E 絶対座標系 E E☁E E☁E
    Bounds.Min = PlayerCell - FIntPoint(BoundsMargin, BoundsMargin);
    Bounds.Max = PlayerCell + FIntPoint(BoundsMargin, BoundsMargin);

    // 1. バウンチE  ング
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

    // 2. 早期終亁E
    TSet<FIntPoint> PendingTargets = OptionalTargets;
    int32 RemainingTargets = PendingTargets.Num();

    const int32 MaxCells = GTS_DF_MaxCells;
    const bool bDiagonal = !!GTS_DF_AllowDiag;

    // ☁E E☁EキャチE  ュからPathFinderを取征E☁E E☁E
    const AGridPathfindingLibrary* GridPathfinding = GetPathFinder();
    if (!GridPathfinding)
    {
        UE_LOG(LogTemp, Error, TEXT("[DistanceField] GridPathfindingLibrary not found"));
        return;
    }

    int32 ProcessedCells = 0;

    TArray<FOpenNode> Open;
    Open.HeapPush(FOpenNode{ PlayerCell, 0 }, FOpenNodeLess{});
    DistanceMap.Add(PlayerCell, 0);

    // ☁E E☁ESparky修正: Closed Set追加で重褁E E琁E  防ぁE☁E E☁E
    TSet<FIntPoint> ClosedSet;

    static const FIntPoint StraightDirs[] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
    static const FIntPoint DiagonalDirs[] = { {1, 1}, {1, -1}, {-1, 1}, {-1, -1} };

    const int32 StraightCost = 10;
    const int32 DiagonalCost = 14;

    while (Open.Num() > 0)
    {
        FOpenNode Current;
        Open.HeapPop(Current, FOpenNodeLess{});

        // ☁E E☁ESparky修正: 早期終亁E- 既に処琁E  みのセルはスキチE E ☁E E☁E
        if (ClosedSet.Contains(Current.Cell))
        {
            continue;  // 重褁E  ントリをスキチE E E  E琁E  み E E
        }

        if (++ProcessedCells > MaxCells)
        {
            UE_LOG(LogTemp, Warning, TEXT("[DistanceField] MaxCells limit reached: Processed=%d (Queue=%d)"), ProcessedCells, Open.Num());
            break;
        }

        if (!InBounds(Current.Cell))
        {
            continue;
        }

        // ☁E E☁ESparky修正: こ Eセルを E琁E  みとしてマ Eク ☁E E☁E
        ClosedSet.Add(Current.Cell);

        if (RemainingTargets > 0 && PendingTargets.Remove(Current.Cell) > 0)
        {
            --RemainingTargets;
            if (RemainingTargets == 0 && OptionalTargets.Num() > 0)
            {
                UE_LOG(LogTemp, Log, TEXT("[DistanceField] All requested targets reached after %d cells"), ProcessedCells);
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

                // ☁E E☁ESparky最適匁E 既に処琁E  みのセルはスキチE E ☁E E☁E
                if (ClosedSet.Contains(Next))
                {
                    return;  // 既に最短距離確定済み
                }

                const bool bIsTargetCell = PendingTargets.Contains(Next);

                // ☁E E☁EPathFinderの統吁EPI IsCellWalkable を使用 ☁E E☁E
                // ★★★ FIX (INC-2025-00002): Occupancy を無視し、地形コストのみで Dijkstra を構築する ★★★
                // IsCellWalkable() は他のユニットの占有を壁とみなすが、IsCellWalkableIgnoringActor(Next, nullptr) は地形のみを見る
                if (!bIsTargetCell && Next != PlayerCell && !GridPathfinding->IsCellWalkableIgnoringActor(Next, nullptr))
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
                FIntPoint Next = Current.Cell + Dir;

                if (bPreventCornerCutting && !CanMoveDiagonal(Current.Cell, Next))
                {
                    continue;
                }

                Relax(Next, DiagonalCost);
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("[DistanceField] Dijkstra complete: PlayerCell=(%d,%d), Cells=%d, Processed=%d, TargetsLeft=%d"),
        PlayerCell.X, PlayerCell.Y, DistanceMap.Num(), ProcessedCells, RemainingTargets);

    // ☁E敵移動AI診断 E 未到達敵の詳細ログ
    UE_LOG(LogTemp, Warning, TEXT("[DistanceField] BuildComplete: TotalCells=%d, ProcessedCells=%d, UnreachedEnemies=%d"),
        DistanceMap.Num(), ProcessedCells, RemainingTargets);

    if (RemainingTargets > 0)
    {
        UE_LOG(LogTemp, Error, TEXT("[DistanceField] ☁E%d enemies could not be reached by Dijkstra field!"), RemainingTargets);
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
        
        // 個別敵チェチE   E 簡易版 E E
        if (UWorld* World = GetWorld())
        {
            int32 UnreachedCount = 0;
            for (TActorIterator<AActor> It(World); It; ++It)
            {
                AActor* Actor = *It;
                if (Actor && Actor->ActorHasTag(TEXT("Enemy")))
                {
                    // 簡易チェチE   E 敵が存在することをログ出劁E
                    UE_LOG(LogTemp, Warning, TEXT("[DistanceField] Found enemy: %s"), *GetNameSafe(Actor));
                    UnreachedCount++;
                }
            }
            UE_LOG(LogTemp, Error, TEXT("[DistanceField] Total enemies found: %d (all may be unreachable)"), UnreachedCount);
        }
    }
}

int32 UDistanceFieldSubsystem::GetDistance(const FIntPoint& Cell) const
{
    const int32* DistPtr = DistanceMap.Find(Cell);
    return DistPtr ? *DistPtr : -1;
}

FIntPoint UDistanceFieldSubsystem::GetNextStepTowardsPlayer(const FIntPoint& FromCell, AActor* IgnoreActor) const
{
    // ☁E E☁E絶対座標APIで距離チェチE   + EnsureCoverage対忁E☁E E☁E
    int32 d0 = GetDistanceAbs(FromCell);
    if (d0 < 0) {
        // ☁E E☁EEnsureCoverageは重いので最小限に ☁E E☁E
        // const_castで一時的にEnsureCoverageを呼ぶ E EonstメソチE  制紁E  避 E E
        // const_cast<UDistanceFieldSubsystem*>(this)->EnsureCoverage(FromCell);
        // d0 = GetDistanceAbs(FromCell);
        // if (d0 < 0) {
        //     DIAG_LOG(Error, TEXT("[GetNextStep] Unreachable after EnsureCoverage Enemy=(%d,%d) Player=(%d,%d)"),
        //              FromCell.X, FromCell.Y, PlayerPosition.X, PlayerPosition.Y);
        //     return FromCell;  // 現在地に留まめE
        // }

        // ☁E E☁E暫定：距離場外 E場合 Eそ E場征E  （軽量！E☁E E☁E
        DIAG_LOG(Warning, TEXT("[GetNextStep] Enemy out of bounds (%d,%d) - staying put"),
                 FromCell.X, FromCell.Y);
        return FromCell;
    }

    int32 CurrentDist = d0;

    // プレイヤー位置の場合 Eそ Eまま返す
    if (CurrentDist == 0)
    {
        DIAG_LOG(Log, TEXT("[GetNextStep] FromCell=(%d,%d) ALREADY AT PLAYER"), FromCell.X, FromCell.Y);
        return FromCell;
    }

    // ☁E E☁E近傍選定：絶対座標で距離を取征E☁E E☁E
    FIntPoint Best = FromCell;
    int32 bestDist = CurrentDist;

    // 4方向近傍をチェチE  

    struct FNeighborDef
    {
        FIntPoint Offset;
        bool bDiagonal;
    };

    static const FNeighborDef Neighbors[] =
    {
        {FIntPoint(1, 0), false}, {FIntPoint(-1, 0), false},
        {FIntPoint(0, 1), false}, {FIntPoint(0, -1), false},
        {FIntPoint(1, 1), true},  {FIntPoint(1, -1), true},
        {FIntPoint(-1, 1), true}, {FIntPoint(-1, -1), true}
    };

    for (const FNeighborDef& Neighbor : Neighbors)
    {
        const FIntPoint N = FromCell + Neighbor.Offset;
        if (Neighbor.bDiagonal && !CanMoveDiagonal(FromCell, N)) continue;
        if (!IsWalkable(N, IgnoreActor)) continue;

        const int32 nd = GetDistanceAbs(N);
        if (nd >= 0 && nd < bestDist)
        {
            bestDist = nd;
            Best = N;
        }
    }

    if (Best == FromCell) 
    {
        DIAG_LOG(Log, TEXT("[GetNextStep] STAY at (%d,%d) - no better neighbor found"), FromCell.X, FromCell.Y);
        return FromCell;  // 攻撁E  E  など
    }
    
    DIAG_LOG(Log, TEXT("[GetNextStep] FromCell=(%d,%d) -> NextCell=(%d,%d) (Dist: %d ↁE%d)"), 
        FromCell.X, FromCell.Y, Best.X, Best.Y, CurrentDist, bestDist);
    
    return Best;
}

bool UDistanceFieldSubsystem::IsWalkable(const FIntPoint& Cell, AActor* IgnoreActor) const
{
    // ☁E E☁EキャチE  ュからPathFinderを取征E☁E E☁E
    const AGridPathfindingLibrary* GridPathfinding = GetPathFinder();
    if (!GridPathfinding)
    {
        UE_LOG(LogTemp, Error, TEXT("[IsWalkable] GridPathfindingLibrary not found, returning false"));
        return false;
    }

    // ☁E E☁E修正 (2025-11-11): IgnoreActorを老E Eした歩行可能性判定！EI征E  問題修正 E E☁E E☁E
    if (IgnoreActor)
    {
        return GridPathfinding->IsCellWalkableIgnoringActor(Cell, IgnoreActor);
    }
    else
    {
        return GridPathfinding->IsCellWalkable(Cell);
    }
}

bool UDistanceFieldSubsystem::CanMoveDiagonal(const FIntPoint& From, const FIntPoint& To) const
{
    const FIntPoint Delta = To - From;
    const FIntPoint Side1 = From + FIntPoint(Delta.X, 0);  // 横の肩
    const FIntPoint Side2 = From + FIntPoint(0, Delta.Y);  // 縦の肩

    // ☁E E☁EFIX (2025-11-11): 正しい角抜け禁止ルール ☁E E☁E
    // 角をすり抜けて移動することを防ぐため、両方の肩が通行可能な場合 Eみ許可
    // 牁E  の肩でも壁があれば、その斜め移動 E禁止
    return IsWalkable(Side1) && IsWalkable(Side2);  // 両方が通行可能な場合 Eみ許可
}

//------------------------------------------------------------------------------
// ☁E E☁E距離場座標系修正 E 絶対座標API ☁E E☁E
//------------------------------------------------------------------------------

int32 UDistanceFieldSubsystem::GetDistanceAbs(const FIntPoint& Abs) const
{
    if (!Bounds.Contains(Abs)) return -2;            // OOB めE-2 と明示
    const int32 lx = Abs.X - Bounds.Min.X;
    const int32 ly = Abs.Y - Bounds.Min.Y;
    
    // DistanceMapは絶対座標で保存されてぁE  ので直接参 E
    const int32* DistPtr = DistanceMap.Find(Abs);
    return DistPtr ? *DistPtr : -1;
}

bool UDistanceFieldSubsystem::EnsureCoverage(const FIntPoint& Abs)
{
    if (Bounds.Contains(Abs)) return false;          // 既にカバ E
    
    // 侁E プレイヤー中忁E  半征E  庁E  て再構築（ E域ならここで全域構築！E
    const int32 Radius = FMath::Max(
        FMath::Abs(Abs.X - PlayerPosition.X),
        FMath::Abs(Abs.Y - PlayerPosition.Y)) + 2;
    
    // 再構築して Bounds.Min/Max を更新
    UpdateDistanceFieldInternal(PlayerPosition, TSet<FIntPoint>(), Radius);
    return true;
}

//------------------------------------------------------------------------------
// PathFinder取得 Eルパ E
//------------------------------------------------------------------------------

AGridPathfindingLibrary* UDistanceFieldSubsystem::GetPathFinder() const
{
    if (!CachedPathFinder.IsValid())
    {
        // ☁E E☁EキャチE  ュが無効な場合 E再取得を試みめE☁E E☁E
        TArray<AActor*> FoundActors;
        UGameplayStatics::GetAllActorsOfClass(GetWorld(), AGridPathfindingLibrary::StaticClass(), FoundActors);
        if (FoundActors.Num() > 0)
        {
            const_cast<UDistanceFieldSubsystem*>(this)->CachedPathFinder = Cast<AGridPathfindingLibrary>(FoundActors[0]);
        }
    }
    return CachedPathFinder.Get();
}

