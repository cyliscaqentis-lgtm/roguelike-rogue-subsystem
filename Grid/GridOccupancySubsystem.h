// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GridOccupancySubsystem.generated.h"

/**
 * UGridOccupancySubsystem: グリチE��の占有�E通行可否管琁E
 * - 神速対応�E要E��Actor→セル位置の最新マップを管琁E
 * - Slot0の移動結果がSlot1に確実に反映されめE
 */
UCLASS()
class LYRAGAME_API UGridOccupancySubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // ☁E�E☁E神速対応�E核忁E��アクターの現在セル位置を取征E☁E�E☁E
    UFUNCTION(BlueprintCallable, Category = "Turn|Occupancy")
    FIntPoint GetCellOfActor(AActor* Actor) const;

    // ☁E�E☁Eセル位置を更新�E�移動実行後に呼び出す！E☁E�E☁E
    UFUNCTION(BlueprintCallable, Category = "Turn|Occupancy")
    void UpdateActorCell(AActor* Actor, FIntPoint NewCell);

    /**
     * 持E��セルが占有されてぁE��か判宁E
     */
    UFUNCTION(BlueprintPure, Category = "Turn|Occupancy")
    bool IsCellOccupied(const FIntPoint& Cell) const;

    UFUNCTION(BlueprintPure, Category = "Turn|Occupancy")
    AActor* GetActorAtCell(const FIntPoint& Cell) const;

    UFUNCTION(BlueprintPure, Category = "Turn|Occupancy")
    bool IsCellReserved(const FIntPoint& Cell) const;

    UFUNCTION(BlueprintPure, Category = "Turn|Occupancy")
    AActor* GetReservationOwner(const FIntPoint& Cell) const;

    UFUNCTION(BlueprintPure, Category = "Turn|Occupancy")
    bool IsReservationOwnedByActor(AActor* Actor, const FIntPoint& Cell) const;

    /**
     * Get the reserved cell for an actor
     * Returns the reserved destination cell, or (-1,-1) if no reservation exists
     */
    UFUNCTION(BlueprintPure, Category = "Turn|Occupancy")
    FIntPoint GetReservedCellForActor(AActor* Actor) const;

    // ☁E�E☁EIsWalkableはPathFinderに統一するため削除 ☁E�E☁E
    // UFUNCTION(BlueprintPure, Category = "Turn|Occupancy")
    // bool IsWalkable(const FIntPoint& Cell) const;

    /**
     * セルを占有する（アクター配置時に呼び出す！E
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Occupancy")
    void OccupyCell(const FIntPoint& Cell, AActor* Actor);

    /**
     * セルの占有を解除�E�アクター削除時に呼び出す！E
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Occupancy")
    void ReleaseCell(const FIntPoint& Cell);

    /**
     * アクターの登録を削除�E�死亡時に呼び出す！E
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Occupancy")
    void UnregisterActor(AActor* Actor);
    UFUNCTION(BlueprintCallable, Category = "Turn|Occupancy")
    void ReserveCellForActor(AActor* Actor, const FIntPoint& Cell);

    UFUNCTION(BlueprintCallable, Category = "Turn|Occupancy")
    void ReleaseReservationForActor(AActor* Actor);

    UFUNCTION(BlueprintCallable, Category = "Turn|Occupancy")
    void ClearAllReservations();


private:
    // ☁EActor ↁEセル位置のマップ（神速対応�E要E��E
    UPROPERTY()
    TMap<TObjectPtr<AActor>, FIntPoint> ActorToCell;

    // セル ↁE占有アクターのマップ（通行可否判定用�E�E
    UPROPERTY()
    TMap<FIntPoint, TWeakObjectPtr<AActor>> OccupiedCells;

    TMap<FIntPoint, TWeakObjectPtr<AActor>> ReservedCells;
    TMap<TWeakObjectPtr<AActor>, FIntPoint> ActorToReservation;
};
