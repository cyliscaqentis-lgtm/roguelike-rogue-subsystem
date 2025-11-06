// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
// ★★★ 修正：相対パスで Turn フォルダを参照 ★★★
#include "../Turn/TurnSystemTypes.h"
#include "DebugVisualizerComponent.generated.h"

/**
 * UDebugVisualizerComponent: ターンシステムのデバッグ可視化（v2.2 第19条）
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class LYRAGAME_API UDebugVisualizerComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UDebugVisualizerComponent();

    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    /**
     * 予約テーブルを可視化
     */
    UFUNCTION(BlueprintCallable, Category = "Debug|Turn")
    void VisualizeReservations(const TArray<FReservationEntry>& Reservations);

    /**
     * 解決済みアクションを可視化
     */
    UFUNCTION(BlueprintCallable, Category = "Debug|Turn")
    void VisualizeResolvedActions(const TArray<FResolvedAction>& Actions);

    /**
     * StableIDを表示
     */
    UFUNCTION(BlueprintCallable, Category = "Debug|Turn")
    void VisualizeStableIDs(const TArray<AActor*>& Actors);

    /**
     * ダッシュ経路を表示
     */
    UFUNCTION(BlueprintCallable, Category = "Debug|Turn")
    void VisualizeDashPath(const TArray<FIntPoint>& Path, const FLinearColor& Color);

    /**
     * デバッグ表示のON/OFF
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    bool bEnableDebugDraw = true;

    /**
     * テキスト表示のON/OFF
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    bool bShowText = true;

    /**
     * グリッドサイズ（cm）
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    float GridSize = 100.0f;

private:
    // セルをワールド座標に変換
    FVector CellToWorld(const FIntPoint& Cell) const;

    // デバッグテキストを描画
    void DrawDebugText(const FVector& Location, const FString& Text,
        const FLinearColor& Color) const;
};
