// Copyright Epic Games, Inc. All Rights Reserved.

#include "DistanceFieldSubsystem.h"
#include "../Grid/GridOccupancySubsystem.h"
#include "../Grid/GridPathfindingLibrary.h"
#include "../ProjectDiagnostics.h"  // ★★★ DIAG_LOG用 ★★★
#include "../Utility/GridCoordinateUtils.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

// ★★★ CVar定義 ★★★
static int32 GTS_DF_MaxCells = 300000;  // ★★★ default拡張：64x64以上のマップで余裕を持たせる ★★★
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
    TEXT("斜め移動を許可（0=無効, 1=有効）"),
    ECVF_Default
);

void UDistanceFieldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    
    // ★★★ PathFinderをキャッシュ（初期化時に存在しない場合は遅延取得） ★★★
    TArray<AActor*> FoundActors;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), AGridPathfindingLibrary::StaticClass(), FoundActors);
    if (FoundActors.Num() > 0)
    {
        CachedPathFinder = Cast<AGridPathfindingLibrary>(FoundActors[0]);
        UE_LOG(LogTemp, Log, TEXT("[DistanceField] PathFinder cached: %s"), *CachedPathFinder->GetName());
    }
    // ★★★ 警告削除：初期化順序によりPathFinderがまだ存在しない場合があるが、GetPathFinder()で遅延取得される ★★★
    
    UE_LOG(LogTemp, Log, TEXT("[DistanceField] Initialized (PathFinder will be lazily loaded if needed)"));
}

void UDistanceFieldSubsystem::Deinitialize()
{
    DistanceMap.Empty();
    NextStepMap.Empty();
    UE_LOG(LogTemp, Log, TEXT("[DistanceField] Deinitialized"));
    Super::Deinitialize();
}

// ★★★ Blueprint用：シンプル版 ★★★
void UDistanceFieldSubsystem::UpdateDistanceField(const FIntPoint& PlayerCell)
{
    // ★★★ BoundsMarginを100に拡大：遠距離の敵もカバー ★★★
    // マップ全域（約100x100タイル）を想定
    UpdateDistanceFieldInternal(PlayerCell, TSet<FIntPoint>(), 100);
}

// ★★★ C++用：最適化版 ★★★
void UDistanceFieldSubsystem::UpdateDistanceFieldOptimized(const FIntPoint& PlayerCell,
    const TSet<FIntPoint>& OptionalTargets,
    int32 BoundsMargin)
{
    UpdateDistanceFieldInternal(PlayerCell, OptionalTargets, BoundsMargin);
}

// ★★★ 優先度キュー用ヘルパ ★★★
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

// ★★★ 内部実装：共通ロジック ★★★
void UDistanceFieldSubsystem::UpdateDistanceFieldInternal(const FIntPoint& PlayerCell,
    const TSet<FIntPoint>& OptionalTargets,
    int32 BoundsMargin)
{
    DistanceMap.Empty();
    NextStepMap.Empty();
    PlayerPosition = PlayerCell;
    
    // ★★★ Boundsを初期化（絶対座標系） ★★★
    Bounds.Min = PlayerCell - FIntPoint(BoundsMargin, BoundsMargin);
    Bounds.Max = PlayerCell + FIntPoint(BoundsMargin, BoundsMargin);

    // 1. バウンディング
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

    // 2. 早期終了
    TSet<FIntPoint> PendingTargets = OptionalTargets;
    int32 RemainingTargets = PendingTargets.Num();

    const int32 MaxCells = GTS_DF_MaxCells;
    const bool bDiagonal = !!GTS_DF_AllowDiag;

    // ★★★ キャッシュからPathFinderを取得 ★★★
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

    // ★★★ Sparky修正: Closed Set追加で重複処理を防ぐ ★★★
    TSet<FIntPoint> ClosedSet;

    static const FIntPoint StraightDirs[] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
    static const FIntPoint DiagonalDirs[] = { {1, 1}, {1, -1}, {-1, 1}, {-1, -1} };

    const int32 StraightCost = 10;
    const int32 DiagonalCost = 14;

    while (Open.Num() > 0)
    {
        FOpenNode Current;
        Open.HeapPop(Current, FOpenNodeLess{});

        // ★★★ Sparky修正: 早期終了 - 既に処理済みのセルはスキップ ★★★
        if (ClosedSet.Contains(Current.Cell))
        {
            continue;  // 重複エントリをスキップ（処理済み）
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

        // ★★★ Sparky修正: このセルを処理済みとしてマーク ★★★
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

                // ★★★ Sparky最適化: 既に処理済みのセルはスキップ ★★★
                if (ClosedSet.Contains(Next))
                {
                    return;  // 既に最短距離確定済み
                }

                const bool bIsTargetCell = PendingTargets.Contains(Next);

                // ★★★ PathFinderの統合API IsCellWalkable を使用 ★★★
                if (!bIsTargetCell && Next != PlayerCell && !GridPathfinding->IsCellWalkable(Next))
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

    // ★ 敵移動AI診断：未到達敵の詳細ログ
    UE_LOG(LogTemp, Warning, TEXT("[DistanceField] BuildComplete: TotalCells=%d, ProcessedCells=%d, UnreachedEnemies=%d"),
        DistanceMap.Num(), ProcessedCells, RemainingTargets);

    if (RemainingTargets > 0)
    {
        UE_LOG(LogTemp, Error, TEXT("[DistanceField] ★ %d enemies could not be reached by Dijkstra field!"), RemainingTargets);
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
        
        // 個別敵チェック（簡易版）
        if (UWorld* World = GetWorld())
        {
            int32 UnreachedCount = 0;
            for (TActorIterator<AActor> It(World); It; ++It)
            {
                AActor* Actor = *It;
                if (Actor && Actor->ActorHasTag(TEXT("Enemy")))
                {
                    // 簡易チェック：敵が存在することをログ出力
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

FIntPoint UDistanceFieldSubsystem::GetNextStepTowardsPlayer(const FIntPoint& FromCell) const
{
    // ★★★ 絶対座標APIで距離チェック + EnsureCoverage対応 ★★★
    int32 d0 = GetDistanceAbs(FromCell);
    if (d0 < 0) {
        // ★★★ EnsureCoverageは重いので最小限に ★★★
        // const_castで一時的にEnsureCoverageを呼ぶ（constメソッド制約回避）
        // const_cast<UDistanceFieldSubsystem*>(this)->EnsureCoverage(FromCell);
        // d0 = GetDistanceAbs(FromCell);
        // if (d0 < 0) {
        //     DIAG_LOG(Error, TEXT("[GetNextStep] Unreachable after EnsureCoverage Enemy=(%d,%d) Player=(%d,%d)"),
        //              FromCell.X, FromCell.Y, PlayerPosition.X, PlayerPosition.Y);
        //     return FromCell;  // 現在地に留まる
        // }
        
        // ★★★ 暫定：距離場外の場合はその場待機（軽量） ★★★
        DIAG_LOG(Warning, TEXT("[GetNextStep] Enemy out of bounds (%d,%d) - staying put"), 
                 FromCell.X, FromCell.Y);
        return FromCell;
    }

    int32 CurrentDist = d0;

    // プレイヤー位置の場合はそのまま返す
    if (CurrentDist == 0)
    {
        DIAG_LOG(Log, TEXT("[GetNextStep] FromCell=(%d,%d) ALREADY AT PLAYER"), FromCell.X, FromCell.Y);
        return FromCell;
    }

    // ★★★ 近傍選定：絶対座標で距離を取得 ★★★
    FIntPoint Best = FromCell;
    int32 bestDist = CurrentDist;
    
    // 4方向近傍をチェック
    for (const FIntPoint& Dir : RogueGrid::CardinalDirections)
    {
        const FIntPoint N = FromCell + Dir;
        if (!IsWalkable(N)) continue;
        
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
        return FromCell;  // 攻撃待機など
    }
    
    DIAG_LOG(Log, TEXT("[GetNextStep] FromCell=(%d,%d) -> NextCell=(%d,%d) (Dist: %d → %d)"), 
        FromCell.X, FromCell.Y, Best.X, Best.Y, CurrentDist, bestDist);
    
    return Best;
}

bool UDistanceFieldSubsystem::IsWalkable(const FIntPoint& Cell) const
{
    // ★★★ キャッシュからPathFinderを取得 ★★★
    const AGridPathfindingLibrary* GridPathfinding = GetPathFinder();
    if (!GridPathfinding)
    {
        UE_LOG(LogTemp, Error, TEXT("[IsWalkable] GridPathfindingLibrary not found, returning false"));
        return false;
    }
    
    // ★★★ PathFinderの統合API IsCellWalkable を使用 ★★★
    return GridPathfinding->IsCellWalkable(Cell);
}

bool UDistanceFieldSubsystem::CanMoveDiagonal(const FIntPoint& From, const FIntPoint& To) const
{
    const FIntPoint Delta = To - From;
    const FIntPoint Side1 = From + FIntPoint(Delta.X, 0);
    const FIntPoint Side2 = From + FIntPoint(0, Delta.Y);

    return IsWalkable(Side1) && IsWalkable(Side2);
}

//------------------------------------------------------------------------------
// ★★★ 距離場座標系修正：絶対座標API ★★★
//------------------------------------------------------------------------------

int32 UDistanceFieldSubsystem::GetDistanceAbs(const FIntPoint& Abs) const
{
    if (!Bounds.Contains(Abs)) return -2;            // OOB を -2 と明示
    const int32 lx = Abs.X - Bounds.Min.X;
    const int32 ly = Abs.Y - Bounds.Min.Y;
    
    // DistanceMapは絶対座標で保存されているので直接参照
    const int32* DistPtr = DistanceMap.Find(Abs);
    return DistPtr ? *DistPtr : -1;
}

bool UDistanceFieldSubsystem::EnsureCoverage(const FIntPoint& Abs)
{
    if (Bounds.Contains(Abs)) return false;          // 既にカバー
    
    // 例: プレイヤー中心に半径を広げて再構築（全域ならここで全域構築）
    const int32 Radius = FMath::Max(
        FMath::Abs(Abs.X - PlayerPosition.X),
        FMath::Abs(Abs.Y - PlayerPosition.Y)) + 2;
    
    // 再構築して Bounds.Min/Max を更新
    UpdateDistanceFieldInternal(PlayerPosition, TSet<FIntPoint>(), Radius);
    return true;
}

//------------------------------------------------------------------------------
// PathFinder取得ヘルパー
//------------------------------------------------------------------------------

AGridPathfindingLibrary* UDistanceFieldSubsystem::GetPathFinder() const
{
    if (!CachedPathFinder.IsValid())
    {
        // ★★★ キャッシュが無効な場合は再取得を試みる ★★★
        TArray<AActor*> FoundActors;
        UGameplayStatics::GetAllActorsOfClass(GetWorld(), AGridPathfindingLibrary::StaticClass(), FoundActors);
        if (FoundActors.Num() > 0)
        {
            const_cast<UDistanceFieldSubsystem*>(this)->CachedPathFinder = Cast<AGridPathfindingLibrary>(FoundActors[0]);
        }
    }
    return CachedPathFinder.Get();
}
