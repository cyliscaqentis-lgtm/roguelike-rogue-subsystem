// EnemyThinkerBase.cpp
#include "EnemyThinkerBase.h"
#include "AbilitySystemComponent.h"
#include "AI/Enemy/EnemyThinkerBase.h"
#include "Turn/DistanceFieldSubsystem.h"
#include "Grid/GridPathfindingLibrary.h"
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
    
    // ★★★ PathFinderをキャッシュ ★★★
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
    Intent.Owner = GetOwner();     // ← 追加：Ownerに必ずセット
    Intent.Actor = GetOwner();     // ← 既存互換：Actorにもセット
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

    // ★ 敵移動AI診断ログ
    UE_LOG(LogTemp, Warning, TEXT("[GetNextStep] ENTRY: EnemyCell=(%d,%d)"),
        Intent.CurrentCell.X, Intent.CurrentCell.Y);

    // ★★★ キャッシュからPathFinderを取得 ★★★
    const AGridPathfindingLibrary* GridPathfinding = CachedPathFinder.Get();
    
    if (GridPathfinding)
    {
        // ★★★ PathFinderの統合API IsCellWalkable を使用 ★★★
        bool bCurrentWalkable = GridPathfinding->IsCellWalkableIgnoringActor(Intent.CurrentCell, Intent.Actor.Get());
        UE_LOG(LogTemp, Warning, TEXT("[PathFinder] Enemy at (%d,%d): Walkable=%d"), 
            Intent.CurrentCell.X, Intent.CurrentCell.Y, bCurrentWalkable ? 1 : 0);
        
        // 周囲4方向の歩行可能性を確認
        FIntPoint Neighbors[4] = {
            Intent.CurrentCell + FIntPoint(1, 0),   // Right
            Intent.CurrentCell + FIntPoint(-1, 0),  // Left
            Intent.CurrentCell + FIntPoint(0, 1),   // Up
            Intent.CurrentCell + FIntPoint(0, -1)   // Down
        };
        
        int32 BlockedCount = 0;
        for (int i = 0; i < 4; ++i)
        {
            bool bNeighborWalkable = GridPathfinding->IsCellWalkable(Neighbors[i]);
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

    // グリッド周囲のステータスダンプ（簡易版）
    UE_LOG(LogTemp, Warning, TEXT("[Grid] Enemy at (%d, %d): checking surroundings"), 
        Intent.CurrentCell.X, Intent.CurrentCell.Y);

    // ★★★ Geminiが指摘した診断：DistanceFieldの返り値を確認 ★★★
    const FIntPoint BeforeNextCell = Intent.NextCell;
    Intent.NextCell = DistanceField->GetNextStepTowardsPlayer(Intent.CurrentCell);
    int32 Distance = DistanceField->GetDistance(Intent.CurrentCell);

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
    
    // ★★★ 異常な値を検出 ★★★
    if (Intent.NextCell.X < -100 || Intent.NextCell.X > 100 || Intent.NextCell.Y < -100 || Intent.NextCell.Y > 100)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[DecideIntent] ❌ CRITICAL: DistanceField returned ABNORMAL NextCell=(%d,%d) for CurrentCell=(%d,%d)"),
            Intent.NextCell.X, Intent.NextCell.Y, Intent.CurrentCell.X, Intent.CurrentCell.Y);
        UE_LOG(LogTemp, Error,
            TEXT("[DecideIntent] Distance=%d, This indicates DistanceField data corruption or uninitialized state!"),
            Distance);
    }
    else
    {
        UE_LOG(LogTemp, Verbose,
            TEXT("[DecideIntent] %s: CurrentCell=(%d,%d) → NextCell=(%d,%d), Distance=%d"),
            *GetNameSafe(GetOwner()), Intent.CurrentCell.X, Intent.CurrentCell.Y,
            Intent.NextCell.X, Intent.NextCell.Y, Distance);
    }

    if (Intent.NextCell == Intent.CurrentCell)
    {
        // ★ 正常系（遠い敵を意図的に判定）なので Verbose に降格
        UE_LOG(LogTurnManager, Verbose, TEXT("[GetNextStep] Distance=-1 (intended: far enemy skipped)"));
        UE_LOG(LogTurnManager, Verbose, TEXT("[GetNextStep] Distance=%d (should be -1 if unreachable)"), Distance);
    }
    else
    {
        UE_LOG(LogTemp, Verbose, TEXT("[GetNextStep] ✅ NextStep=(%d,%d) from (%d,%d)"),
            Intent.NextCell.X, Intent.NextCell.Y, Intent.CurrentCell.X, Intent.CurrentCell.Y);
    }

    // ★★★ デバッグログ：DistanceFieldの状態を詳細に出力 ★★★
    // ★ 遠距離敵の WAIT 判定は通常動作→ Verbose に
    UE_LOG(LogTurnManager, Verbose, 
        TEXT("[DecideIntent] %s: CurrentCell=(%d,%d), NextCell=(%d,%d), Distance=%d, AttackRange=%d"),
        *GetNameSafe(GetOwner()), 
        Intent.CurrentCell.X, Intent.CurrentCell.Y,
        Intent.NextCell.X, Intent.NextCell.Y,
        Distance, AttackRangeInTiles);

    // ★★★ P1対策：攻撃意図のデフォルト実装 ★★★
    // Distanceからプレイヤー距離を取得して攻撃判定
    // マンハッタン距離 ≤ AttackRange → Attack、それ以外は追尾Move
    const int32 DistanceToPlayer = Distance; // 既に取得したDistanceを使用
    
    if (DistanceToPlayer <= AttackRangeInTiles && DistanceToPlayer > 0)
    {
        Intent.AbilityTag = FGameplayTag::RequestGameplayTag(TEXT("AI.Intent.Attack"));
        Intent.NextCell = Intent.CurrentCell;  // 攻撃時は移動しない

        UE_LOG(LogTemp, Log, TEXT("[DecideIntent] %s: ★ ATTACK intent (Distance=%d, Range=%d)"),
            *GetNameSafe(GetOwner()), DistanceToPlayer, AttackRangeInTiles);
    }
    else
    {
        Intent.AbilityTag = FGameplayTag::RequestGameplayTag(TEXT("AI.Intent.Move"));
        
        // P2対策：現在セル==目的セルならWaitにダウングレード
        if (Intent.NextCell == Intent.CurrentCell)
        {
        Intent.AbilityTag = FGameplayTag::RequestGameplayTag(TEXT("AI.Intent.Wait"));
        UE_LOG(LogTurnManager, Log, TEXT("[Thinker] %s WAIT - NextCell identical to current"),
            *GetNameSafe(GetOwner()));
        }
        else
        {
            UE_LOG(LogTemp, Log, TEXT("[DecideIntent] %s: ★ MOVE intent (Distance=%d > Range=%d)"),
                *GetNameSafe(GetOwner()), DistanceToPlayer, AttackRangeInTiles);
        }
    }

    return Intent;
}

FEnemyIntent UEnemyThinkerBase::ComputeIntent_Implementation(const FEnemyObservation& Observation)
{
    // ★★★ 重要：DecideIntent()を呼び出す ★★★
    return DecideIntent();
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

// ★★★ GridPathfindingLibraryを使用してWorldToGridを実行 ★★★
FIntPoint UEnemyThinkerBase::GetCurrentGridPosition() const
{
    if (AActor* Owner = GetOwner())
    {
        UWorld* World = GetWorld();
        if (!World)
        {
            return FIntPoint(0, 0);
        }

        // ★★★ GridPathfindingLibraryのインスタンスを取得 ★★★
        for (TActorIterator<AGridPathfindingLibrary> It(World); It; ++It)
        {
            AGridPathfindingLibrary* GridLib = *It;
            if (GridLib)
            {
                FVector WorldLocation = Owner->GetActorLocation();
                return GridLib->WorldToGrid(WorldLocation);
            }
        }

        // ★★★ フォールバック：GridPathfindingLibraryが見つからない場合 ★★★
        // タイルサイズ100cmと仮定
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
