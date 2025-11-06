 
// Copyright Epic Games, Inc. All Rights Reserved.

#include "DashStopConditions.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"

int32 UDashStopEvaluator::CalculateAllowedDashSteps(
    AActor* Actor,
    const FIntPoint& StartCell,
    const FIntPoint& TargetCell,
    int32 ProposedK,
    const FDashStopConfig& Config)
{
    if (!Actor || ProposedK <= 0)
    {
        return 0;
    }

    UWorld* World = Actor->GetWorld();
    if (!World)
    {
        return 0;
    }

    // 直線経路を取得
    TArray<FIntPoint> Path = GetLinePath(StartCell, TargetCell, ProposedK);

    // 各タイルで停止条件をチェック
    for (int32 t = 0; t < Path.Num(); ++t)
    {
        const FIntPoint& CurrentCell = Path[t];

        // 敵隣接チェック
        if (Config.bStopOnEnemyAdjacent && HasAdjacentEnemy(CurrentCell, World))
        {
            UE_LOG(LogTemp, Log, TEXT("[DashStop] Stopped at step %d (EnemyAdjacent)"), t + 1);
            return t + 1;
        }

        // 危険タイルチェック
        if (Config.bStopOnDangerTile && IsDangerTile(CurrentCell, World))
        {
            UE_LOG(LogTemp, Log, TEXT("[DashStop] Stopped at step %d (DangerTile)"), t);
            return FMath::Max(0, t);
        }

        // 障害物チェック
        if (Config.bStopOnObstacle && IsObstacle(CurrentCell, World))
        {
            UE_LOG(LogTemp, Log, TEXT("[DashStop] Stopped at step %d (Obstacle)"), t);
            return FMath::Max(0, t);
        }
    }

    // 停止条件に該当しない場合は提案通り
    return ProposedK;
}

bool UDashStopEvaluator::HasAdjacentEnemy(const FIntPoint& Cell, UWorld* World)
{
    if (!World)
    {
        return false;
    }

    // TODO: Phase 3後半で実装
    // 8方向の隣接マスに敵がいるかチェック
    return false;
}

bool UDashStopEvaluator::IsDangerTile(const FIntPoint& Cell, UWorld* World)
{
    if (!World)
    {
        return false;
    }

    // TODO: Phase 3後半で実装
    // 危険タイル（罠、溶岩等）の判定
    return false;
}

bool UDashStopEvaluator::IsObstacle(const FIntPoint& Cell, UWorld* World)
{
    if (!World)
    {
        return false;
    }

    // TODO: Phase 3後半で実装
    // 障害物（壁、扉等）の判定
    return false;
}

TArray<FIntPoint> UDashStopEvaluator::GetLinePath(
    const FIntPoint& Start,
    const FIntPoint& Target,
    int32 MaxSteps)
{
    TArray<FIntPoint> Path;
    Path.Reserve(MaxSteps);

    FIntPoint Current = Start;
    FIntPoint Delta = Target - Start;

    // 方向を正規化（-1, 0, 1）
    FIntPoint Direction;
    Direction.X = FMath::Clamp(Delta.X, -1, 1);
    Direction.Y = FMath::Clamp(Delta.Y, -1, 1);

    // 直線経路を生成
    for (int32 i = 0; i < MaxSteps; ++i)
    {
        Current += Direction;
        Path.Add(Current);

        // ターゲットに到達したら終了
        if (Current == Target)
        {
            break;
        }
    }

    return Path;
}
