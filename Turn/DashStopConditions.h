 
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "TurnSystemTypes.h"
#include "DashStopConditions.generated.h"

/**
 * UDashStopEvaluator: ダッシュ停止条件の評価（v2.2 第18条）
 */
UCLASS(BlueprintType)
class LYRAGAME_API UDashStopEvaluator : public UObject
{
    GENERATED_BODY()

public:
    /**
     * ダッシュ可能距離を計算（停止条件を考慮）
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Dash")
    static int32 CalculateAllowedDashSteps(
        AActor* Actor,
        const FIntPoint& StartCell,
        const FIntPoint& TargetCell,
        int32 ProposedK,
        const FDashStopConfig& Config);

    /**
     * 隣接マスに敵がいるか判定
     */
    UFUNCTION(BlueprintPure, Category = "Turn|Dash")
    static bool HasAdjacentEnemy(const FIntPoint& Cell, UWorld* World);

    /**
     * 危険タイルか判定
     */
    UFUNCTION(BlueprintPure, Category = "Turn|Dash")
    static bool IsDangerTile(const FIntPoint& Cell, UWorld* World);

    /**
     * 障害物があるか判定
     */
    UFUNCTION(BlueprintPure, Category = "Turn|Dash")
    static bool IsObstacle(const FIntPoint& Cell, UWorld* World);

    /**
     * 直線経路を取得（StartからTargetへの直線）
     */
    UFUNCTION(BlueprintPure, Category = "Turn|Dash")
    static TArray<FIntPoint> GetLinePath(
        const FIntPoint& Start,
        const FIntPoint& Target,
        int32 MaxSteps);
};
