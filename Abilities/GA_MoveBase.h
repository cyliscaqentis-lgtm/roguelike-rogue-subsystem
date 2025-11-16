// GA_MoveBase.h
// CodeRevision: INC-2025-00026-R1 (Fix garbled Japanese comments - Phase 1) (2025-11-16 00:00)
#pragma once

#include "CoreMinimal.h"
#include "GA_TurnActionBase.h"
#include "GameplayTagContainer.h"
#include "GA_MoveBase.generated.h"

// CodeRevision: INC-2025-00030-R1 (Migrate to UGridPathfindingSubsystem) (2025-11-16 23:55)
// Forward declarations
class UGridPathfindingSubsystem;
class AUnitBase;
class UGridOccupancySubsystem;
class AGameTurnManagerBase;

/**
 * UGA_MoveBase
 * 移動アビリティの基底クラス
 *
 * Phase 3 リファクタリング: State.Action.InProgress + Barrier 対応
 *
 * 機能:
 * - EnhancedInputから受け取った方向ベクトルをグリッド移動に変換
 * - State.Moving タグを管理し、ActivationBlockedTags で重複実行を防止
 * - State.Action.InProgress タグを管理し、他のアクションと競合しないようにする
 * - Barrier::RegisterAction() / CompleteAction() でアクションの完了を管理
 * - 移動完了時に Ability.Move.Completed イベントを発行し、TurnManager に通知
 * - 3軸移動（X/Y/Z）をサポートし、グリッド中心へのスナップ機能を提供
 * - TurnIdとWindowIdを検証し、古いターンのコマンドを無視
 *
 * 実装詳細:
 * - State.Action.InProgress タグを ActivationOwnedTags で管理
 * - Barrier システムと連携して RegisterAction / CompleteAction を呼び出し
 * - TurnManager の InProgress タグ管理と同期
 */
UCLASS(Abstract, Blueprintable)
class LYRAGAME_API UGA_MoveBase : public UGA_TurnActionBase
{
    GENERATED_BODY()

public:
    UGA_MoveBase(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

    //--------------------------------------------------------------------------
    // GameplayAbility オーバーライド
    //--------------------------------------------------------------------------

    virtual void ActivateAbility(
        const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,
        const FGameplayEventData* TriggerEventData
    ) override;

    virtual bool CanActivateAbility(
        const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayTagContainer* SourceTags = nullptr,
        const FGameplayTagContainer* TargetTags = nullptr,
        FGameplayTagContainer* OptionalRelevantTags = nullptr
    ) const override;

    virtual void ActivateAbilityFromEvent(
        const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,
        const FGameplayEventData* EventData
    );

    /**
     * ★★★ ChatGPT提案: イベント応答の軽量チェック (2025-11-11) ★★★
     * HandleGameplayEvent returned 0 問題を解決するため、
     * イベントに応答する前に軽量な検証を行う。
     * 重い検証はActivateAbilityで実行される。
     */
    virtual bool ShouldRespondToEvent(
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayEventData* Payload
    ) const;

    virtual void EndAbility(
        const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,
        bool bReplicateEndAbility,
        bool bWasCancelled
    ) override;

    virtual void CancelAbility(
        const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,
        bool bReplicateCancelAbility
    ) override;

protected:
    virtual void SendCompletionEvent(bool bTimedOut = false) override;

    //==========================================================================
    // Phase 6: デバッグ・計測
    //==========================================================================

    /**
     * アビリティ開始時刻（デバッグ用）
     */
    double AbilityStartTime = 0.0;

    //--------------------------------------------------------------------------
    // 移動パラメータ
    //--------------------------------------------------------------------------

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Move")
    float Speed = 600.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Move")
    float SpeedBuff = 1.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Move")
    float SpeedDebuff = 1.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Move")
    float GridSize = 100.0f;

    /** この距離以下の場合はアニメーションをスキップ（=0で無効化） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Move|Animation")
    float SkipAnimIfUnderDistance = 0.0f;  // 距離が短すぎる場合はアニメーションをスキップ
    UPROPERTY(EditDefaultsOnly, Category = "GAS|Tags")
    FGameplayTag StateMovingTag = FGameplayTag::RequestGameplayTag(TEXT("State.Moving"));

    //--------------------------------------------------------------------------
    // 状態変数（BlueprintReadOnly - 読み取り専用）
    //--------------------------------------------------------------------------

    UPROPERTY(BlueprintReadOnly, Category = "Move|State")
    FVector Direction = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "Move|State")
    FVector2D MoveDir2D = FVector2D::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "Move|State")
    FVector FirstLoc = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "Move|State")
    FVector NextTileStep = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "Move|State")
    float DesiredYaw = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Move|State")
    float CurrentSpeed = 0.0f;

    int32 CompletedTurnIdForEvent = INDEX_NONE;

    //--------------------------------------------------------------------------
    // C++内部関数
    //--------------------------------------------------------------------------

    /** EventDataからDirectionを抽出 */
    bool ExtractDirectionFromEventData(const FGameplayEventData* EventData, FVector& OutDirection);

    /** 8方向グリッドに量子化 */
    FVector2D QuantizeToGridDirection(const FVector& InDirection);

    /** 次のタイル位置を計算 */
    FVector CalculateNextTilePosition(const FVector& CurrentPosition, const FVector2D& Dir);

    /**
     * 移動先タイルが通行可能かチェック（3軸移動対応）
     */
    bool IsTileWalkable(const FVector& TilePosition, AUnitBase* Self = nullptr);

    /** タイル状態を更新 */
    void UpdateGridState(const FVector& Position, int32 Value);

    /** セル単位で通行可能かチェック（Occupancy/DistanceFieldを使用） */
    bool IsTileWalkable(const FIntPoint& Cell) const;

    /** ヨー角を45度単位に丸める */
    float RoundYawTo45Degrees(float Yaw);

    /** 移動完了デリゲートをバインド */
    void BindMoveFinishedDelegate();

    /** 移動完了時のコールバック */
    UFUNCTION()
    void OnMoveFinished(AUnitBase* Unit);

    /** 指定セルへの移動を開始（グリッド位置を正しく設定） */
    void StartMoveToCell(const FIntPoint& TargetCell);

    //--------------------------------------------------------------------------
    // 3軸移動・位置調整・デバッグヘルパー
    //--------------------------------------------------------------------------

    FVector SnapToCellCenter(const FVector& WorldPos) const;
    FVector SnapToCellCenterFixedZ(const FVector& WorldPos, float FixedZ) const;
    // CodeRevision: INC-2025-00030-R1 (Migrate to UGridPathfindingSubsystem) (2025-11-16 23:55)
    float ComputeFixedZ(const AUnitBase* Unit, const UGridPathfindingSubsystem* Pathfinding) const;
    FVector AlignZToGround(const FVector& WorldPos, float TraceUp = 200.0f, float TraceDown = 2000.0f) const;
    void DebugDumpAround(const FIntPoint& Center);

    //--------------------------------------------------------------------------
    // Blueprint実装可能イベント・アニメーション制御
    //--------------------------------------------------------------------------

    UFUNCTION(BlueprintImplementableEvent, Category = "Move|Animation")
    void ExecuteMoveAnimation(const TArray<FVector>& Path);

    UFUNCTION(BlueprintNativeEvent, Category = "Move|Animation")
    bool ShouldSkipAnimation();
    virtual bool ShouldSkipAnimation_Implementation();

private:
    //--------------------------------------------------------------------------
    // CodeRevision: INC-2025-00018-R3 (Remove barrier management - Phase 3) (2025-11-17)
    // Barrier management removed - handled by other systems
    //--------------------------------------------------------------------------

    //--------------------------------------------------------------------------
    // State.Moving タグ管理・キャッシュ
    //--------------------------------------------------------------------------

    UPROPERTY()
    FGameplayTag TagStateMoving;

    //--------------------------------------------------------------------------
    // CodeRevision: INC-2025-00030-R1 (Migrate to UGridPathfindingSubsystem) (2025-11-16 23:55)
    // Removed CachedPathFinder - now using subsystem-based access
    //--------------------------------------------------------------------------

    mutable TWeakObjectPtr<AGameTurnManagerBase> CachedTurnManager;

    /**
     * PathFinderへのアクセスを取得（キャッシュ付き）
     * CodeRevision: INC-2025-00030-R2 (Migrate to UGridPathfindingSubsystem) (2025-11-17 00:40)
     * @deprecated Use GetGridPathfindingSubsystem() instead
     */
    const class UGridPathfindingSubsystem* GetPathFinder() const;

    /** Get GridPathfindingSubsystem (new subsystem-based access) */
    class UGridPathfindingSubsystem* GetGridPathfindingSubsystem() const;

    AGameTurnManagerBase* GetTurnManager() const;

    bool bMoveFinishedDelegateBound = false;

    // CodeRevision: INC-2025-00030-R3 (Add Barrier sync to GA_MoveBase) (2025-11-17 01:00)
    // P1: Fix turn hang by synchronizing player move with TurnActionBarrierSubsystem
    FGuid MoveActionId;
    int32 MoveTurnId = -1;
    bool bBarrierRegistered = false;

    //--------------------------------------------------------------------------
    // 移動状態キャッシュ・GridOccupancy連携
    //--------------------------------------------------------------------------

    FVector CachedStartLocWS = FVector::ZeroVector;

    /** 移動先セル（OnMoveFinishedで正しい位置設定に使用） */
    FIntPoint CachedNextCell = FIntPoint(-1, -1);

    //--------------------------------------------------------------------------
    // キャッシュ変数（OnMoveFinished で使用）
    //--------------------------------------------------------------------------

    FGameplayAbilitySpecHandle CachedSpecHandle;
    FGameplayAbilityActorInfo CachedActorInfo;
    FGameplayAbilityActivationInfo CachedActivationInfo;
    FVector CachedFirstLoc = FVector::ZeroVector;

    // Sparky修正: 再終了防止フラグ
    bool bIsEnding = false;

    // ★★★ REMOVED: InProgressStack (2025-11-11) ★★★
    // ActivationOwnedTags を使用するため、手動カウントは不要
};
