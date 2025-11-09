// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * グリッド座標計算のためのユーティリティ関数群
 * 基本方向の定義や隣接セル取得など、重複した座標計算を削減
 */
namespace RogueGrid
{
    /**
     * 基本4方向（東西南北）のオフセット定義
     * 多くの場所で FIntPoint(1,0), (-1,0), (0,1), (0,-1) が重複していたため共通化
     */
    static const TArray<FIntPoint> CardinalDirections = {
        FIntPoint(1, 0),   // 右（東）
        FIntPoint(-1, 0),  // 左（西）
        FIntPoint(0, 1),   // 上（北）
        FIntPoint(0, -1)   // 下（南）
    };

    /**
     * 対角線を含む8方向のオフセット定義
     * 将来的な拡張のために用意
     */
    static const TArray<FIntPoint> AllDirections = {
        FIntPoint(1, 0),   // 右
        FIntPoint(-1, 0),  // 左
        FIntPoint(0, 1),   // 上
        FIntPoint(0, -1),  // 下
        FIntPoint(1, 1),   // 右上
        FIntPoint(1, -1),  // 右下
        FIntPoint(-1, 1),  // 左上
        FIntPoint(-1, -1)  // 左下
    };

    /**
     * グリッド方向ベクトルを-1~1の範囲にクランプ
     * 移動方向の正規化に使用
     *
     * @param Direction クランプ対象の方向ベクトル
     * @return クランプされた方向ベクトル（各成分が-1, 0, 1のいずれか）
     *
     * 使用例:
     *   FIntPoint Dir = FIntPoint(5, -3);
     *   FIntPoint ClampedDir = RogueGrid::ClampGridDirection(Dir);  // (1, -1)
     */
    static FIntPoint ClampGridDirection(const FIntPoint& Direction)
    {
        return FIntPoint(
            FMath::Clamp(Direction.X, -1, 1),
            FMath::Clamp(Direction.Y, -1, 1)
        );
    }

    /**
     * 指定セルの隣接セル（4方向）を取得
     * 基本4方向（上下左右）の隣接セルを配列で返す
     *
     * @param Cell 中心となるセル座標
     * @return 隣接セル座標の配列（4要素）
     *
     * 使用例:
     *   FIntPoint MyCell(5, 10);
     *   TArray<FIntPoint> Neighbors = RogueGrid::GetAdjacentCells(MyCell);
     *   // Neighbors = [(6,10), (4,10), (5,11), (5,9)]
     */
    static TArray<FIntPoint> GetAdjacentCells(const FIntPoint& Cell)
    {
        TArray<FIntPoint> Result;
        Result.Reserve(4);
        for (const FIntPoint& Dir : CardinalDirections)
        {
            Result.Add(Cell + Dir);
        }
        return Result;
    }

    /**
     * 指定セルの隣接セル（8方向）を取得
     * 対角線を含む8方向の隣接セルを配列で返す
     *
     * @param Cell 中心となるセル座標
     * @return 隣接セル座標の配列（8要素）
     *
     * 使用例:
     *   FIntPoint MyCell(5, 10);
     *   TArray<FIntPoint> AllNeighbors = RogueGrid::GetAllNeighborCells(MyCell);
     *   // 8方向すべての隣接セルが返る
     */
    static TArray<FIntPoint> GetAllNeighborCells(const FIntPoint& Cell)
    {
        TArray<FIntPoint> Result;
        Result.Reserve(8);
        for (const FIntPoint& Dir : AllDirections)
        {
            Result.Add(Cell + Dir);
        }
        return Result;
    }

    /**
     * 2つのセル間のマンハッタン距離を計算
     *
     * @param From 始点セル
     * @param To 終点セル
     * @return マンハッタン距離（X差分の絶対値 + Y差分の絶対値）
     */
    static int32 ManhattanDistance(const FIntPoint& From, const FIntPoint& To)
    {
        return FMath::Abs(To.X - From.X) + FMath::Abs(To.Y - From.Y);
    }

    /**
     * 2つのセル間のチェビシェフ距離を計算
     *
     * @param From 始点セル
     * @param To 終点セル
     * @return チェビシェフ距離（X差分とY差分の最大値）
     */
    static int32 ChebyshevDistance(const FIntPoint& From, const FIntPoint& To)
    {
        return FMath::Max(FMath::Abs(To.X - From.X), FMath::Abs(To.Y - From.Y));
    }
}
