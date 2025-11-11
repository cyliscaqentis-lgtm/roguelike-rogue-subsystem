// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DistanceFieldSubsystem.generated.h"

UCLASS()
class LYRAGAME_API UDistanceFieldSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // ★★★ Blueprint用：シンプル版（デフォルト引数なし） ★★★
    UFUNCTION(BlueprintCallable, Category = "Turn|DistanceField")
    void UpdateDistanceField(const FIntPoint& PlayerCell);

    // ★★★ C++用：最適化版（デフォルト引数あり） ★★★
    void UpdateDistanceFieldOptimized(const FIntPoint& PlayerCell,
        const TSet<FIntPoint>& OptionalTargets = TSet<FIntPoint>(),
        int32 BoundsMargin = 8);

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Turn|DistanceField")
    int32 GetDistance(const FIntPoint& Cell) const;

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Turn|DistanceField")
    FIntPoint GetNextStepTowardsPlayer(const FIntPoint& FromCell, AActor* IgnoreActor = nullptr) const;  // ★★★ 修正 (2025-11-11): AI待機問題修正のためIgnoreActor追加

    // ★★★ デバッグ用 ★★★
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Turn|DistanceField")
    bool bAllowDiagonal = true;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Turn|DistanceField")
    bool bPreventCornerCutting = true;

    // ★★★ 距離場座標系修正 ★★★
    struct FGridBounds
    {
        FIntPoint Min {0,0}, Max { -1,-1 }; // [Min, Max] を含む
        FORCEINLINE bool Contains(const FIntPoint& P) const {
            return (P.X >= Min.X && P.X <= Max.X && P.Y >= Min.Y && P.Y <= Max.Y);
        }
        FORCEINLINE int32 Width() const { return Max.X - Min.X + 1; }
    };

    // ★★★ 常に「絶対座標」で距離を取得できる関数 ★★★
    int32 GetDistanceAbs(const FIntPoint& Abs) const;
    bool EnsureCoverage(const FIntPoint& Abs);

private:
    bool IsWalkable(const FIntPoint& Cell, AActor* IgnoreActor = nullptr) const;  // ★★★ 修正 (2025-11-11): AI待機問題修正のためIgnoreActor追加
    bool CanMoveDiagonal(const FIntPoint& From, const FIntPoint& To) const;
    
    // ★★★ PathFinderのキャッシュ（初期化時に取得） ★★★
    UPROPERTY(Transient)
    TWeakObjectPtr<class AGridPathfindingLibrary> CachedPathFinder;

    // ★★★ 内部実装：共通ロジック ★★★
    void UpdateDistanceFieldInternal(const FIntPoint& PlayerCell,
        const TSet<FIntPoint>& OptionalTargets,
        int32 BoundsMargin);

    TMap<FIntPoint, int32> DistanceMap;
    TMap<FIntPoint, FIntPoint> NextStepMap;
    FIntPoint PlayerPosition;
    FGridBounds Bounds;  // ★★★ 距離場の絶対座標範囲 ★★★
    
    // ★★★ PathFinder取得ヘルパー ★★★
    class AGridPathfindingLibrary* GetPathFinder() const;
};
