// ============================================================================
// ファイル: Source/LyraGame/Rogue/Turn/TurnActionBarrierSubsystem.h
// 用途: ターンアクション完了の同期バリア（非同期GA_MOVE完了待機）
// 作成日: 2025-10-26
// 修正日: 2025-10-29 (Phase 1-6 統合版)
// ============================================================================

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TurnActionBarrierSubsystem.generated.h"

// ============================================================================
// ログカテゴリ
// ============================================================================
DECLARE_LOG_CATEGORY_EXTERN(LogTurnBarrier, Log, All);

// ============================================================================
// ★★★ デリゲート定義（Blueprint購読用） ★★★
// ============================================================================
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FOnAllMovesFinished,
    int32, TurnId
);

// ============================================================================
// ターンキー構造体（将来の拡張用）
// ============================================================================
USTRUCT(BlueprintType)
struct FTurnKey
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "TurnBarrier")
    int32 TurnId = -1;

    bool operator==(const FTurnKey& Other) const
    {
        return TurnId == Other.TurnId;
    }

    friend uint32 GetTypeHash(const FTurnKey& Key)
    {
        return ::GetTypeHash(Key.TurnId);
    }
};

// ============================================================================
// ★★★ Phase 1: ターン状態構造体（ActionID管理用）
// ============================================================================
USTRUCT()
struct FTurnState
{
    GENERATED_BODY()

    /** ペンディング中の ActionID 集合 */
    TSet<FGuid> PendingActionIds;

    /** Actor ごとの未完了 ActionID */
    TMap<TWeakObjectPtr<AActor>, FGuid> ActorToAction;

    /** ActionID から Actor への逆引き */
    TMap<FGuid, TWeakObjectPtr<AActor>> ActionToActor;

    /** Actor ごとの保留アクション配列（複数アクション対応） */
    TMap<TWeakObjectPtr<AActor>, TArray<FGuid>> PendingActions;

    /** ActionID開始時刻マップ（タイムアウト検出用） */
    TMap<FGuid, double> ActionStartTimes;

    /** ターン開始時刻 */
    double TurnStartTime = 0.0;
};



// ============================================================================
// TurnActionBarrierSubsystem
// ============================================================================
/**
 * ターンアクション完了の同期バリア
 *
 * Phase 1-6 統合版:
 * - Phase 1: ActionIDベースの管理
 * - Phase 2: WindowId検証（TurnManagerに移管）
 * - Phase 3: 3タグシステム（GAが管理）
 * - Phase 4: IsQuiescent()による二重鍵
 * - Phase 5: Gate再オープン（TurnManagerに移管）
 * - Phase 6: タイムアウトとGAキャンセル
 */
UCLASS(Config = Game)
class LYRAGAME_API UTurnActionBarrierSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    //==========================================================================
    // Lifecycle
    //==========================================================================

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    //==========================================================================
    // ★★★ Phase 1: ActionID管理
    //==========================================================================

    /** ターン開始 */
    void BeginTurn(int32 TurnId);

    /** アクション登録 */
    FGuid RegisterAction(AActor* Actor, int32 TurnId);

    /** アクション完了 */
    void CompleteAction(AActor* Actor, int32 TurnId, const FGuid& ActionId);

    /** 全アクション完了確認 */
    bool IsQuiescent(int32 TurnId) const;

    /** 未完了アクション数取得 */
    int32 GetPendingActionCount(int32 TurnId) const;

    /** 現在のターンID取得 */
    int32 GetCurrentTurnId() const { return CurrentKey.TurnId; }

    /** デバッグ用：ターン状態をダンプ */
    void DumpTurnState(int32 TurnId) const;

    /** 古いターンのクリーンアップ */
    void RemoveOldTurns(int32 CurrentTurn);

    //==========================================================================
    // ★★★ 冪等API: Token方式のBarrier管理（二重登録/完了を無害化）
    //==========================================================================

    /** 重複安全な登録（Ownerはログ表示目的） */
    UFUNCTION(BlueprintCallable, Category = "TurnBarrier")
    void RegisterActionOnce(AActor* Owner, FGuid& OutToken);

    /** 重複安全な完了 */
    UFUNCTION(BlueprintCallable, Category = "TurnBarrier")
    void CompleteActionToken(const FGuid& Token);

    //==========================================================================
    // ★★★ Phase 6: タイムアウト管理
    //==========================================================================

    /** タイムアウトチェック */
    void CheckTimeouts();
    void CompactTurnState(FTurnState& State);
    bool RemoveActionById(FTurnState& State, const FGuid& ActionId);

    //==========================================================================
    // ★★★ デリゲート（Blueprint購読用）
    //==========================================================================

    /**
     * 全移動完了時に発火するデリゲート
     * Blueprint側でこれをバインドして EndEnemyTurn を呼ぶ
     */
    UPROPERTY(BlueprintAssignable, Category = "TurnBarrier")
    FOnAllMovesFinished OnAllMovesFinished;

    //==========================================================================
    // 公開API（レガシー互換）
    //==========================================================================

    /**
     * 移動バッチの開始（サーバー専用）
     * ★★★ 非推奨: Phase 1 では BeginTurn() を使用推奨
     */
    UE_DEPRECATED(5.0, "Use BeginTurn() instead of StartMoveBatch()")
    UFUNCTION(BlueprintCallable, Category = "TurnBarrier", meta = (DeprecatedFunction, DeprecationMessage = "Use BeginTurn() instead"))
    void StartMoveBatch(int32 InCount, int32 InTurnId);

    /**
     * ★★★ 非推奨: 移動開始の通知（サーバー専用）
     * 注: Phase 1 では RegisterAction() を使用推奨
     */
    UE_DEPRECATED(5.0, "Use RegisterAction() instead of NotifyMoveStarted()")
    UFUNCTION(BlueprintCallable, Category = "TurnBarrier", meta = (DeprecatedFunction, DeprecationMessage = "Use RegisterAction() instead"))
    void NotifyMoveStarted(AActor* Unit, int32 InTurnId);

    /**
     * ★★★ 非推奨: 個別アクション完了の通知（サーバー専用）
     * 注: Phase 1 では CompleteAction() を使用推奨
     */
    UE_DEPRECATED(5.0, "Use CompleteAction() instead of NotifyMoveFinished()")
    UFUNCTION(BlueprintCallable, Category = "TurnBarrier", meta = (DeprecatedFunction, DeprecationMessage = "Use CompleteAction() instead"))
    void NotifyMoveFinished(AActor* Unit, int32 InTurnId);

    /**
     * 残りの待機アクション数を取得（デバッグ用）
     */
    UFUNCTION(BlueprintPure, Category = "TurnBarrier")
    int32 GetPendingMoves() const { return PendingMoves; }

    /**
     * バリアが有効かどうか
     */
    UFUNCTION(BlueprintPure, Category = "TurnBarrier")
    bool IsBarrierActive() const { return PendingMoves > 0; }

    //==========================================================================
    // デバッグAPI
    //==========================================================================

    /**
     * バリアを強制終了（デバッグ用）
     */
    UFUNCTION(BlueprintCallable, Category = "Debug")
    void ForceFinishBarrier();

    /**
     * 通知済みユニットの一覧を取得（デバッグ用）
     */
    UFUNCTION(BlueprintCallable, Category = "Debug")
    TArray<AActor*> GetNotifiedUnits() const;

    /**
     * 現在のターンで通知済みのアクター一覧を取得（デバッグ用）
     */
    UFUNCTION(BlueprintCallable, Category = "Debug")
    TArray<AActor*> GetNotifiedActorsThisTurn() const;

    //==========================================================================
    // 設定（Config）
    //==========================================================================

    /**
     * セーフティタイムアウト時間（秒）
     */
    UPROPERTY(Config, EditAnywhere, Category = "TurnBarrier")
    float SafetyTimeoutSeconds = 5.0f;

    /**
     * ★★★ Phase 6: アクションタイムアウト時間（秒）
     */
    UPROPERTY(Config, EditAnywhere, Category = "TurnBarrier")
    float ActionTimeoutSeconds = 5.0f;

    /**
     * ★★★ Phase 6: タイムアウト時にGAをキャンセルするか
     */
    UPROPERTY(Config, EditAnywhere, Category = "TurnBarrier")
    bool bCancelAbilitiesOnTimeout = true;

    /**
     * Verboseログを有効にするか（デバッグ用）
     */
    UPROPERTY(Config, EditAnywhere, Category = "TurnBarrier")
    bool bEnableVerboseLogging = false;

private:
    //==========================================================================
    // 内部状態
    //==========================================================================

    /** 現在のターンキー */
    FTurnKey CurrentKey;

    /** ★★★ 追加: 現在のターンID */
    int32 CurrentTurnId = -1;

    /** ★★★ Phase 1: ターン状態のマップ */
    TMap<int32, FTurnState> TurnStates;


    /** ★★★ レガシー: 待機中のアクション数 */
    int32 PendingMoves = 0;

    /** ★★★ レガシー: 既に通知済みのユニット（二重通知防止 - 永続的） */
    TSet<TWeakObjectPtr<AActor>> AlreadyNotified;

    /** ★★★ レガシー: 現在のターンで既に通知済みのアクター */
    TSet<TWeakObjectPtr<AActor>> NotifiedActorsThisTurn;

    /** セーフティタイムアウトハンドル */
    FTimerHandle SafetyTimeoutHandle;

    /** タイムアウトチェック用タイマー（Tick→Timer最適化） */
    FTimerHandle TimeoutCheckTimer;

    //==========================================================================
    // ★★★ Token方式の状態管理（冪等API用）
    //==========================================================================

    /** 有効トークン集合 */
    UPROPERTY(Transient)
    TSet<FGuid> ActiveTokens;

    /** デバッグ用：トークン→所有者 */
    UPROPERTY(Transient)
    TMap<FGuid, TWeakObjectPtr<AActor>> TokenOwners;

    //==========================================================================
    // 内部メソッド
    //==========================================================================

    /** サーバーかどうかを判定 */
    bool IsServer() const;

    /** 完了デリゲートを発火 */
    void FireAllFinished(int32 TurnId);

    /** タイムアウト時のコールバック */
    void OnSafetyTimeout();
};
