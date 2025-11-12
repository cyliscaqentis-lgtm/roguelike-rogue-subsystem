// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "StableActorRegistry.h"
#include "AbilitySystemComponent.h"
#include "GameplayAbilitySpec.h"

// ★★★ .generated.hは必ず最後 ★★★
#include "TurnSystemTypes.generated.h"

//------------------------------------------------------------------------------
// 敵観測データ
//------------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct FEnemyObservation
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "Observation")
    int32 DistanceInTiles = 0;

    UPROPERTY(BlueprintReadWrite, Category = "Observation")
    FIntPoint GridPosition = FIntPoint::ZeroValue;

    UPROPERTY(BlueprintReadWrite, Category = "Observation")
    FIntPoint PlayerGridPosition = FIntPoint::ZeroValue;

    UPROPERTY(BlueprintReadWrite, Category = "Observation")
    float HPRatio = 1.0f;

    UPROPERTY(BlueprintReadWrite, Category = "Observation")
    FGameplayTagContainer EnvironmentTags;

    UPROPERTY(BlueprintReadWrite, Category = "Observation")
    TMap<FName, float> CustomStats;
};

//------------------------------------------------------------------------------
// 敵意図
//------------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct FEnemyIntent
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "Intent")
    FGameplayTag AbilityTag;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 BasePriority = 100;

    UPROPERTY(BlueprintReadWrite, Category = "Intent")
    FIntPoint CurrentCell = FIntPoint::ZeroValue;

    UPROPERTY(BlueprintReadWrite, Category = "Intent")
    FIntPoint NextCell = FIntPoint::ZeroValue;

    UPROPERTY(BlueprintReadWrite, Category = "Intent")
    TWeakObjectPtr<AActor> Target = nullptr;

    UPROPERTY(BlueprintReadWrite, Category = "Intent")
    float SpeedPriority = 0.0f;

    UPROPERTY(BlueprintReadWrite, Category = "Intent")
    TObjectPtr<AActor> TargetActor = nullptr;

    UPROPERTY(BlueprintReadWrite, Category = "Intent")
    TSubclassOf<UGameplayAbility> SelectedAbility = nullptr;

    UPROPERTY(BlueprintReadWrite, Category = "Intent")
    TWeakObjectPtr<AActor> Actor = nullptr;

    UPROPERTY(BlueprintReadWrite, Category = "Intent")
    int32 TimeSlot = 0;

    UPROPERTY(BlueprintReadWrite, Category = "Intent")
    int32 DistToPlayer = -1;

    UPROPERTY(BlueprintReadWrite, Category = "Intent")
    TObjectPtr<AActor> Owner = nullptr;

    // ★★★ FIX (2025-11-12): 実行済みフラグを追加（無限ループ防止） ★★★
    UPROPERTY(BlueprintReadWrite, Category = "Intent")
    bool bExecuted = false;

    FEnemyIntent() = default;

    FEnemyIntent(const FGameplayTag& InTag, const FIntPoint& InCell, AActor* InTarget = nullptr)
        : AbilityTag(InTag), NextCell(InCell), Target(InTarget)
    {
    }
};

//------------------------------------------------------------------------------
// 敵意図候補
//------------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct FEnemyIntentCandidate
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "Intent")
    FEnemyIntent Intent;

    UPROPERTY(BlueprintReadWrite, Category = "Intent")
    float Score = 0.0f;

    UPROPERTY(BlueprintReadWrite, Category = "Intent")
    int32 Priority = 0;

    UPROPERTY(BlueprintReadWrite, Category = "Intent")
    FGameplayTagContainer RequiredTags;

    UPROPERTY(BlueprintReadWrite, Category = "Intent")
    FGameplayTagContainer BlockedByTags;
};

//------------------------------------------------------------------------------
// 保留中の移動
//------------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct FPendingMove
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "Move")
    TObjectPtr<AActor> Mover = nullptr;

    UPROPERTY(BlueprintReadWrite, Category = "Move")
    FIntPoint From = FIntPoint::ZeroValue;

    UPROPERTY(BlueprintReadWrite, Category = "Move")
    FIntPoint To = FIntPoint::ZeroValue;

    UPROPERTY(BlueprintReadWrite, Category = "Move")
    float Priority = 0.0f;

    UPROPERTY(BlueprintReadWrite, Category = "Move")
    bool bCancelled = false;

    UPROPERTY(BlueprintReadWrite, Category = "Move")
    FEnemyIntent FallbackIntent;
};

//------------------------------------------------------------------------------
// 同時移動バッチ
//------------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct FSimulBatch
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "Batch")
    TArray<FPendingMove> Moves;
};

//------------------------------------------------------------------------------
// ボードスナップショット
//------------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct FBoardSnapshot
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "Snapshot")
    TMap<FIntPoint, TObjectPtr<AActor>> OccupancyMap;

    UPROPERTY(BlueprintReadWrite, Category = "Snapshot")
    TMap<FIntPoint, FGameplayTagContainer> TileTags;

    UPROPERTY(BlueprintReadWrite, Category = "Snapshot")
    TSet<FIntPoint> VisibleCells;
};

//------------------------------------------------------------------------------
// ★★★ コマンド適用結果 (2025-11-10) ★★★
//------------------------------------------------------------------------------

/**
 * コマンド適用の結果を表す列挙型
 * - Applied: 移動適用（ターン消費）
 * - RotatedNoTurn: 回転のみ適用（ターン不消費、ウィンドウ継続）
 * - RejectedCloseWindow: 不正リクエスト（ウィンドウを閉じる）
 */
UENUM(BlueprintType)
enum class ECommandApplyResult : uint8
{
    Applied              UMETA(DisplayName = "Applied (Turn Consumed)"),
    RotatedNoTurn        UMETA(DisplayName = "Rotated Only (No Turn)"),
    RejectedCloseWindow  UMETA(DisplayName = "Rejected (Close Window)")
};

//------------------------------------------------------------------------------
// プレイヤーコマンド
//------------------------------------------------------------------------------


USTRUCT(BlueprintType)
struct FPlayerCommand
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Command")
    FGameplayTag CommandTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Command")
    FIntPoint TargetCell = FIntPoint::ZeroValue;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Command")
    TObjectPtr<AActor> TargetActor = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Command")
    FVector Direction = FVector::ZeroVector;

    UPROPERTY(BlueprintReadWrite, Category = "Command")
    int32 TurnId = INDEX_NONE;

    // ★★★ 新規追加: WindowId フィールド
    UPROPERTY(BlueprintReadWrite, Category = "Command")
    int32 WindowId = 0;

    FPlayerCommand()
        : CommandTag(FGameplayTag())
        , TargetCell(FIntPoint::ZeroValue)
        , TargetActor(nullptr)
        , Direction(FVector::ZeroVector)
        , TurnId(INDEX_NONE)
        , WindowId(0)  // ★★★ コンストラクタに追加
    {
    }
};


//------------------------------------------------------------------------------
// ターンコンテキスト
//------------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct FTurnContext
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "Context")
    int32 TurnIndex = 0;

    UPROPERTY(BlueprintReadWrite, Category = "Context")
    TObjectPtr<AActor> PlayerActor = nullptr;

    UPROPERTY(BlueprintReadWrite, Category = "Context")
    TArray<TObjectPtr<AActor>> VisibleEnemies;

    UPROPERTY(BlueprintReadWrite, Category = "Context")
    FBoardSnapshot BoardState;
};

//------------------------------------------------------------------------------
// Phase 2追加：予約・解決構造体
//------------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct LYRAGAME_API FReservationEntry
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "Reservation")
    TObjectPtr<AActor> Actor = nullptr;

    UPROPERTY(BlueprintReadWrite, Category = "Reservation")
    FStableActorID ActorID;

    UPROPERTY(BlueprintReadWrite, Category = "Reservation")
    FIntPoint CurrentCell = FIntPoint(-1, -1);

    UPROPERTY(BlueprintReadWrite, Category = "Reservation")
    int32 TimeSlot = 0;

    UPROPERTY(BlueprintReadWrite, Category = "Reservation")
    FIntPoint Cell = FIntPoint(-1, -1);

    UPROPERTY(BlueprintReadWrite, Category = "Reservation")
    FGameplayTag AbilityTag;

    UPROPERTY(BlueprintReadWrite, Category = "Reservation")
    int32 ActionTier = 0;

    UPROPERTY(BlueprintReadWrite, Category = "Reservation")
    int32 BasePriority = 0;

    UPROPERTY(BlueprintReadWrite, Category = "Reservation")
    int32 DistanceReduction = 0;

    UPROPERTY(BlueprintReadWrite, Category = "Reservation")
    int32 GenerationOrder = 2147483647;
};

USTRUCT(BlueprintType)
struct LYRAGAME_API FResolvedAction
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite)
    TWeakObjectPtr<AActor> Actor;

    UPROPERTY(BlueprintReadWrite)
    FGameplayTag AbilityTag;

    UPROPERTY(BlueprintReadWrite)
    FGameplayAbilityTargetDataHandle TargetData;

    UPROPERTY(BlueprintReadWrite, Category = "Resolved")
    TObjectPtr<AActor> SourceActor = nullptr;

    UPROPERTY(BlueprintReadWrite, Category = "Resolved")
    FStableActorID ActorID;

    UPROPERTY(BlueprintReadWrite, Category = "Resolved")
    FIntPoint CurrentCell = FIntPoint(-1, -1);

    UPROPERTY(BlueprintReadWrite, Category = "Resolved")
    FIntPoint NextCell = FIntPoint(-1, -1);

    UPROPERTY(BlueprintReadWrite, Category = "Resolved")
    int32 AllowedDashSteps = 0;

    UPROPERTY(BlueprintReadWrite, Category = "Resolved")
    FGameplayTag FinalAbilityTag;

    UPROPERTY(BlueprintReadWrite, Category = "Resolved")
    bool bIsWait = false;

    UPROPERTY(BlueprintReadOnly, Category = "Resolved")
    int32 GenerationOrder = INT32_MAX;

    UPROPERTY(BlueprintReadOnly, Category = "Resolved")
    int32 TimeSlot = 0;

    UPROPERTY(BlueprintReadWrite, Category = "Resolved")
    FString ResolutionReason;
};

//------------------------------------------------------------------------------
// Phase 3追加：ダッシュ停止設定
//------------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct LYRAGAME_API FDashStopConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dash")
    bool bStopOnEnemyAdjacent = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dash")
    bool bStopOnDangerTile = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dash")
    bool bStopOnObstacle = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dash")
    int32 MaxDashDistance = 10;
};
