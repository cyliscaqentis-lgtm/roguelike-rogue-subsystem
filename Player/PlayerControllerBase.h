#pragma once

#include "CoreMinimal.h"
#include "Player/LyraPlayerController.h"
#include "InputActionValue.h"
#include "Turn/TurnSystemTypes.h"
#include "PlayerControllerBase.generated.h"

// 前方宣言
class AGameTurnManagerBase;
class AGridPathfindingLibrary;
class UInputAction;
class UInputMappingContext;

//------------------------------------------------------------------------------
// ★★★ 削除: ECmdReject enum（ACK/NACKシステム廃止に伴い削除）
//------------------------------------------------------------------------------
/*
UENUM(BlueprintType)
enum class ECmdReject : uint8
{
    StaleWindow     UMETA(DisplayName = "Stale Window ID"),
    FutureWindow    UMETA(DisplayName = "Future Window ID"),
    Throttled       UMETA(DisplayName = "Throttled"),
    NotAdjacent     UMETA(DisplayName = "Not Adjacent Cell"),
    NoManager       UMETA(DisplayName = "No Turn Manager"),
    NoPathFinder    UMETA(DisplayName = "No PathFinder")
};
*/

/**
 * APlayerControllerBase
 * TBSゲーム用のプレイヤーコントローラー
 *
 * 主な責務:
 * - Enhanced Input システムによる入力処理
 * - GameTurnManager との同期（WindowId方式）
 * - クライアント→サーバーのコマンド送信
 *
 * ★★★ Phase 2 修正履歴:
 * - ACK/NACKシステムを削除（WindowId検証に一本化）
 * - InputGuard関連コードを削除
 * - 時間スロットリング（50ms制限）を削除
 * - bSentThisInputWindow フラグを削除
 * - LastMoveCmdServerTime を削除
 *
 * 【新しい設計】
 * - クライアント: CurrentInputWindowIdを保持し、RPCに同封
 * - サーバー: TurnManagerがWindowId検証を一元管理
 * - 重複送信: サーバー側のLastAcceptedWindowIdsで検出
 */
UCLASS()
class LYRAGAME_API APlayerControllerBase : public ALyraPlayerController
{
    GENERATED_BODY()

public:
    APlayerControllerBase();

    //--------------------------------------------------------------------------
    // ★★★ 削除: RPC Functions - Client通知（ACK/NACKシステム）
    //--------------------------------------------------------------------------
    /*
    UFUNCTION(Client, Reliable)
    void Client_NotifyCommandAccepted(int32 WindowId);

    UFUNCTION(Client, Reliable)
    void Client_NotifyCommandRejected(int32 WindowId, ECmdReject Reason);
    */

    /** デバッグ用：グリッドシステム動作確認 */
    UFUNCTION(Exec)
    void GridSmokeTest();

    /** グリッドパスファインダーへの参照（UnitManagerからアクセス可能） */
    UPROPERTY(BlueprintReadWrite, Category = "TBS|Turn")
    TObjectPtr<AGridPathfindingLibrary> PathFinder = nullptr;

protected:
    //--------------------------------------------------------------------------
    // 入力ウィンドウ制御（WindowId方式）
    //--------------------------------------------------------------------------

    /**
     * 現在の入力ウィンドウID
     * TurnManagerのInputWindowIdと同期される（レプリケート経由）
     * privateセクションに移動
     */

    //--------------------------------------------------------------------------
    // ★★★ 削除: 入力ゲート関連フラグ
    //--------------------------------------------------------------------------
    /*
    UPROPERTY()
    bool bSentThisInputWindow = false;
    */

    /** WaitingForPlayerInput の前回値（Tick内での変化検出用） */
    UPROPERTY()
    bool bPrevWaitingForPlayerInput = false;

    /** 初期同期完了フラグ */
    UPROPERTY()
    bool bPrevInitSynced = false;

    //--------------------------------------------------------------------------
    // Lifecycle Overrides
    //--------------------------------------------------------------------------

    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;
    virtual void OnPossess(APawn* InPawn) override;

    /**
     * ★★★ 修正: Tickで入力ウィンドウの監視
     * OnInputWindowOpenedの呼び出しは削除（サーバーが管理）
     */
    virtual void Tick(float DeltaTime) override;

    //--------------------------------------------------------------------------
    // EnhancedInput - Input Actions & Mapping Context
    //--------------------------------------------------------------------------

    /** 移動入力アクション（8方向） */
    UPROPERTY(EditDefaultsOnly, Category = "Input|Actions")
    TObjectPtr<UInputAction> IA_Move;

    /** 向き変更入力アクション */
    UPROPERTY(EditDefaultsOnly, Category = "Input|Actions")
    TObjectPtr<UInputAction> IA_TurnFacing;

    /** TBS専用入力マッピングコンテキスト */
    UPROPERTY(EditDefaultsOnly, Category = "Input|Mapping")
    TObjectPtr<UInputMappingContext> IMC_TBS;

    //--------------------------------------------------------------------------
    // Input Handlers
    //--------------------------------------------------------------------------

    /**
     * ★★★ 修正: 移動入力トリガー時の処理
     * - InputGuardによる受付判定を削除
     * - bSentThisInputWindowを削除
     * - 8方向量子化とコマンド送信のみ
     */
    void Input_Move_Triggered(const FInputActionValue& Value);

    /** 移動入力キャンセル時 */
    void Input_Move_Canceled(const FInputActionValue& Value);

    /** 移動入力完了時 */
    void Input_Move_Completed(const FInputActionValue& Value);

    /** 向き変更入力開始時 */
    void Input_TurnFacing_Started(const FInputActionValue& Value);

    /** 向き変更入力トリガー時 */
    void Input_TurnFacing_Triggered(const FInputActionValue& Value);

    //--------------------------------------------------------------------------
    // Utility Functions
    //--------------------------------------------------------------------------

    /**
     * 8方向量子化（クライアント側）
     * アナログスティック入力を8方向のグリッド座標に変換
     */
    UFUNCTION(BlueprintPure, Category = "TBS|Utility")
    FIntPoint Quantize8Way(const FVector2D& Direction, float Threshold = 0.5f) const;

    /**
     * カメラ相対方向計算
     */
    UFUNCTION(BlueprintPure, Category = "TBS|Utility")
    FVector CalculateCameraRelativeDirection(const FVector2D& InputAxis) const;

    /**
     * 現在のグリッドセルを取得（クライアント側）
     */
    UFUNCTION(BlueprintPure, Category = "TBS|Utility")
    FIntPoint GetCurrentGridCell() const;

    /** EnhancedInput 初期化 */
    void InitializeEnhancedInput();

    /**
     * ★★★ 8近傍チェック（サーバー側検証用）
     * TurnManager側に移行を推奨（Phase 3以降）
     */
    bool IsAdjacent(const FIntPoint& FromCell, const FIntPoint& ToCell) const;

    /** サーバー側での現在セル取得 */
    FIntPoint ServerGetCurrentCell() const;

    //--------------------------------------------------------------------------
    // RPC Functions - サーバー送信
    //--------------------------------------------------------------------------

    /**
     * ★★★ 修正: WindowId対応版：移動コマンド送信
     * クライアント側:
     * - FPlayerCommandにWindowIdを設定
     * - Server_SubmitCommandを呼ぶ
     *
     * サーバー側:
     * - TurnManager::ServerSubmitCommand_Implementationに委譲
     * - WindowId検証はTurnManagerが一元管理
     * - 時間スロットリング（50ms制限）は削除
     */
    UFUNCTION(Server, Reliable)
    void Server_SubmitCommand(const FPlayerCommand& Command);

    /**
     * ★★★ 向き変更コマンド（Server→TurnManagerへ直接委譲）
     */
    UFUNCTION(Server, Reliable)
    void Server_TurnFacing(FVector2D Direction); // そのまま（使うなら）

protected:
    /** 最後に入力された向き変更方向 */
    UPROPERTY(BlueprintReadWrite, Category = "TBS|Input")
    FVector2D LastTurnDirection = FVector2D::ZeroVector;

    /** 現在の入力方向キャッシュ */
    UPROPERTY(BlueprintReadWrite, Category = "TBS|Input")
    FVector2D CachedInputDirection = FVector2D::ZeroVector;

    /** ゲーム内の唯一のTurnManagerへの参照 */
    UPROPERTY(BlueprintReadWrite, Category = "TBS|Turn")
    TObjectPtr<AGameTurnManagerBase> CachedTurnManager = nullptr;

    /** アナログスティック軸の閾値 */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "TBS|Input")
    float AxisThreshold = 0.5f;

    /** デッドゾーン閾値 */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "TBS|Input")
    float DeadzoneThreshold = 0.1f;

    /** 移動コマンド用のGameplayTag */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "TBS|Input")
    FGameplayTag MoveInputTag;

    /** 向き変更コマンド用のGameplayTag */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "TBS|Input")
    FGameplayTag TurnInputTag;

private:
    /** 現在の入力ウィンドウID */
    int32 CurrentInputWindowId = 0;
    
    /** Input_Move_Completed の冪等化用（マルチ環境対応） */
    UPROPERTY()
    int32 LastProcessedWindowId = INDEX_NONE;
    
    /** クライアント側：入力ウィンドウでの重複送信防止 */
    bool bSentThisInputWindow = false;
    
    // クライアント側ではタグを触らない（OnRepはUI専用）
    void ApplyWaitInputGate_Client(bool bOpen) {} // ダミー/削除可：呼ばれないように

    /** Server側：TurnFacing重複防止用 */
    FIntPoint ServerLastTurnDirectionQuantized = FIntPoint::ZeroValue;

    ////--------------------------------------------------------------------------
    //// ★★★ 削除: OnInputWindowOpened（サーバー側管理に移行）
    ////--------------------------------------------------------------------------
    /*
    void OnInputWindowOpened();
    */
};
