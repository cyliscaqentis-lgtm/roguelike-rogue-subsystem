// ============================================================================
// GridUtils.h
// グリッド関連の共通ユーティリティ
// 作成日: 2025-11-09
// ============================================================================

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Rogue/Grid/GridPathfindingLibrary.h"

/**
 * グリッド関連の共通ユーティリティ
 * SetActorLocationの重複を共通化
 */
class FGridUtils
{
public:
    /**
     * Actorをグリッドセルの中心にスナップ
     * @param Actor 対象Actor
     * @param Cell グリッドセル座標
     * @param PathFinder PathFinderインスタンス
     */
    static void SnapActorToGridCell(
        AActor* Actor,
        const FIntPoint& Cell,
        AGridPathfindingLibrary* PathFinder
    )
    {
        if (!Actor || !PathFinder)
        {
            return;
        }

        FVector WorldPos = PathFinder->GridToWorld(Cell);
        Actor->SetActorLocation(WorldPos);
    }

    /**
     * マンハッタン距離を計算
     * @param A 座標A
     * @param B 座標B
     * @return マンハッタン距離
     */
    static int32 ManhattanDistance(const FIntPoint& A, const FIntPoint& B)
    {
        return FMath::Abs(A.X - B.X) + FMath::Abs(A.Y - B.Y);
    }

    /**
     * チェビシェフ距離を計算
     * @param A 座標A
     * @param B 座標B
     * @return チェビシェフ距離
     */
    static int32 ChebyshevDistance(const FIntPoint& A, const FIntPoint& B)
    {
        return FMath::Max(FMath::Abs(A.X - B.X), FMath::Abs(A.Y - B.Y));
    }

    /**
     * ユークリッド距離を計算
     * @param A 座標A
     * @param B 座標B
     * @return ユークリッド距離
     */
    static float EuclideanDistance(const FIntPoint& A, const FIntPoint& B)
    {
        int32 dx = A.X - B.X;
        int32 dy = A.Y - B.Y;
        return FMath::Sqrt(static_cast<float>(dx * dx + dy * dy));
    }
};
