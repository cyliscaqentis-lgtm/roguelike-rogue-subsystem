// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GridOccupancySubsystem.generated.h"

/**
 * UGridOccupancySubsystem: 繧ｰ繝ｪ繝・ラ縺ｮ蜊譛峨・騾夊｡悟庄蜷ｦ邂｡逅・
 * - 逾樣溷ｯｾ蠢懊・隕・ｼ哂ctor竊偵そ繝ｫ菴咲ｽｮ縺ｮ譛譁ｰ繝槭ャ繝励ｒ邂｡逅・
 * - Slot0縺ｮ遘ｻ蜍慕ｵ先棡縺郡lot1縺ｫ遒ｺ螳溘↓蜿肴丐縺輔ｌ繧・
 */
UCLASS()
class LYRAGAME_API UGridOccupancySubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // 笘・・笘・逾樣溷ｯｾ蠢懊・譬ｸ蠢・ｼ壹い繧ｯ繧ｿ繝ｼ縺ｮ迴ｾ蝨ｨ繧ｻ繝ｫ菴咲ｽｮ繧貞叙蠕・笘・・笘・
    UFUNCTION(BlueprintCallable, Category = "Turn|Occupancy")
    FIntPoint GetCellOfActor(AActor* Actor) const;

    // 笘・・笘・繧ｻ繝ｫ菴咲ｽｮ繧呈峩譁ｰ・育ｧｻ蜍募ｮ溯｡悟ｾ後↓蜻ｼ縺ｳ蜃ｺ縺呻ｼ・笘・・笘・
    UFUNCTION(BlueprintCallable, Category = "Turn|Occupancy")
    void UpdateActorCell(AActor* Actor, FIntPoint NewCell);

    /**
     * 謖・ｮ壹そ繝ｫ縺悟頃譛峨＆繧後※縺・ｋ縺句愛螳・
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

    // 笘・・笘・IsWalkable縺ｯPathFinder縺ｫ邨ｱ荳縺吶ｋ縺溘ａ蜑企勁 笘・・笘・
    // UFUNCTION(BlueprintPure, Category = "Turn|Occupancy")
    // bool IsWalkable(const FIntPoint& Cell) const;

    /**
     * 繧ｻ繝ｫ繧貞頃譛峨☆繧具ｼ医い繧ｯ繧ｿ繝ｼ驟咲ｽｮ譎ゅ↓蜻ｼ縺ｳ蜃ｺ縺呻ｼ・
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Occupancy")
    void OccupyCell(const FIntPoint& Cell, AActor* Actor);

    /**
     * 繧ｻ繝ｫ縺ｮ蜊譛峨ｒ隗｣髯､・医い繧ｯ繧ｿ繝ｼ蜑企勁譎ゅ↓蜻ｼ縺ｳ蜃ｺ縺呻ｼ・
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Occupancy")
    void ReleaseCell(const FIntPoint& Cell);

    /**
     * 繧｢繧ｯ繧ｿ繝ｼ縺ｮ逋ｻ骭ｲ繧貞炎髯､・域ｭｻ莠｡譎ゅ↓蜻ｼ縺ｳ蜃ｺ縺呻ｼ・
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
    // 笘・Actor 竊・繧ｻ繝ｫ菴咲ｽｮ縺ｮ繝槭ャ繝暦ｼ育･樣溷ｯｾ蠢懊・隕・ｼ・
    UPROPERTY()
    TMap<TObjectPtr<AActor>, FIntPoint> ActorToCell;

    // 繧ｻ繝ｫ 竊・蜊譛峨い繧ｯ繧ｿ繝ｼ縺ｮ繝槭ャ繝暦ｼ磯夊｡悟庄蜷ｦ蛻､螳夂畑・・
    UPROPERTY()
    TMap<FIntPoint, TWeakObjectPtr<AActor>> OccupiedCells;

    TMap<FIntPoint, TWeakObjectPtr<AActor>> ReservedCells;
    TMap<TWeakObjectPtr<AActor>, FIntPoint> ActorToReservation;
};
