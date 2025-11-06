// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GridOccupancySubsystem.generated.h"

/**
 * UGridOccupancySubsystem: グリッドの占有・通行可否管理
 * - 神速対応の要：Actor→セル位置の最新マップを管理
 * - Slot0の移動結果がSlot1に確実に反映される
 */
UCLASS()
class LYRAGAME_API UGridOccupancySubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // ★★★ 神速対応の核心：アクターの現在セル位置を取得 ★★★
    UFUNCTION(BlueprintCallable, Category = "Turn|Occupancy")
    FIntPoint GetCellOfActor(AActor* Actor) const;

    // ★★★ セル位置を更新（移動実行後に呼び出す） ★★★
    UFUNCTION(BlueprintCallable, Category = "Turn|Occupancy")
    void UpdateActorCell(AActor* Actor, FIntPoint NewCell);

    /**
     * 指定セルが占有されているか判定
     */
    UFUNCTION(BlueprintPure, Category = "Turn|Occupancy")
    bool IsCellOccupied(const FIntPoint& Cell) const;

    // ★★★ IsWalkableはPathFinderに統一するため削除 ★★★
    // UFUNCTION(BlueprintPure, Category = "Turn|Occupancy")
    // bool IsWalkable(const FIntPoint& Cell) const;

    /**
     * セルを占有する（アクター配置時に呼び出す）
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Occupancy")
    void OccupyCell(const FIntPoint& Cell, AActor* Actor);

    /**
     * セルの占有を解除（アクター削除時に呼び出す）
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Occupancy")
    void ReleaseCell(const FIntPoint& Cell);

    /**
     * アクターの登録を削除（死亡時に呼び出す）
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Occupancy")
    void UnregisterActor(AActor* Actor);

private:
    // ★ Actor → セル位置のマップ（神速対応の要）
    UPROPERTY()
    TMap<TObjectPtr<AActor>, FIntPoint> ActorToCell;

    // セル → 占有アクターのマップ（通行可否判定用）
    UPROPERTY()
    TMap<FIntPoint, TWeakObjectPtr<AActor>> OccupiedCells;
};
