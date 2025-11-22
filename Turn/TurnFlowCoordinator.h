// TurnFlowCoordinator.h

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Net/UnrealNetwork.h"
#include "TurnFlowCoordinator.generated.h"

// Log category
DECLARE_LOG_CATEGORY_EXTERN(LogTurnFlow, Log, All);

/**
 * UTurnFlowCoordinator
 *
 * 責務:
 * - ターンID・InputWindowIDの管理
 * - ターンの開始・終了・進行
 * - AP（アクションポイント）管理
 * - ターンインデックス管理
 */
UCLASS()
class LYRAGAME_API UTurnFlowCoordinator : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	//==========================================================================
	// ターンID・WindowID管理
	//==========================================================================

	/** 現在のターンIDを取得 */
	UFUNCTION(BlueprintPure, Category = "Turn")
	int32 GetCurrentTurnId() const { return CurrentTurnId; }

	/** 現在の入力ウィンドウIDを取得 */
	UFUNCTION(BlueprintPure, Category = "Turn")
	int32 GetCurrentInputWindowId() const { return InputWindowId; }

	/** 現在のターンインデックスを取得（レガシー互換用） */
	UFUNCTION(BlueprintPure, Category = "Turn")
	int32 GetCurrentTurnIndex() const { return CurrentTurnIndex; }

	/** 新しい入力ウィンドウを開く（サーバー権威のみ） */
	UFUNCTION(BlueprintCallable, Category = "Turn", meta = (BlueprintAuthorityOnly))
	void OpenNewInputWindow();

	//==========================================================================
	// ターン進行制御
	//==========================================================================

	/** 最初のターンを開始（初回のみ呼び出し） */
	UFUNCTION(BlueprintCallable, Category = "Turn|Flow", meta = (BlueprintAuthorityOnly))
	void StartFirstTurn();

	/** ターンを開始 */
	UFUNCTION(BlueprintCallable, Category = "Turn|Flow", meta = (BlueprintAuthorityOnly))
	void StartTurn();

	/** ターンを終了 */
	UFUNCTION(BlueprintCallable, Category = "Turn|Flow", meta = (BlueprintAuthorityOnly))
	void EndTurn();

	/** ターンを進める（TurnId++） */
	UFUNCTION(BlueprintCallable, Category = "Turn|Flow", meta = (BlueprintAuthorityOnly))
	void AdvanceTurn();

	/** ターン進行可能か判定 */
	UFUNCTION(BlueprintPure, Category = "Turn|Flow")
	bool CanAdvanceTurn(int32 TurnId) const;

	//==========================================================================
	// AP（アクションポイント）管理
	//==========================================================================

	/** プレイヤーAPを消費 */
	UFUNCTION(BlueprintCallable, Category = "Turn|AP", meta = (BlueprintAuthorityOnly))
	void ConsumePlayerAP(int32 Amount);

	/** プレイヤーAPを回復 */
	UFUNCTION(BlueprintCallable, Category = "Turn|AP", meta = (BlueprintAuthorityOnly))
	void RestorePlayerAP(int32 Amount);

	/** プレイヤーAPをターン開始時に全回復 */
	UFUNCTION(BlueprintCallable, Category = "Turn|AP", meta = (BlueprintAuthorityOnly))
	void ResetPlayerAPForTurn();

	/** APが十分か判定 */
	UFUNCTION(BlueprintPure, Category = "Turn|AP")
	bool HasSufficientAP(int32 Required) const;

	/** 現在のプレイヤーAPを取得 */
	UFUNCTION(BlueprintPure, Category = "Turn|AP")
	int32 GetPlayerAP() const { return PlayerAP; }

	/** 1ターンあたりの最大APを取得 */
	UFUNCTION(BlueprintPure, Category = "Turn|AP")
	int32 GetPlayerAPPerTurn() const { return PlayerAPPerTurn; }

	//==========================================================================
	// 敵フェーズキューイング
	//==========================================================================

	/** 敵フェーズをキューに追加 */
	UFUNCTION(BlueprintCallable, Category = "Turn|Enemy")
	void QueueEnemyPhase();

	/** 敵フェーズがキューされているか */
	UFUNCTION(BlueprintPure, Category = "Turn|Enemy")
	bool IsEnemyPhaseQueued() const { return bEnemyPhaseQueued; }

	/** 敵フェーズキューをクリア */
	UFUNCTION(BlueprintCallable, Category = "Turn|Enemy")
	void ClearEnemyPhaseQueue();

protected:
	//==========================================================================
	// レプリケーション設定
	//==========================================================================

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual bool IsSupportedForNetworking() const override { return true; }

	//==========================================================================
	// レプリケーション通知
	//==========================================================================

	UFUNCTION()
	void OnRep_CurrentTurnId();

	UFUNCTION()
	void OnRep_InputWindowId();

	//==========================================================================
	// メンバー変数
	//==========================================================================

	/** 現在のターンID（Replicated） */
	UPROPERTY(ReplicatedUsing = OnRep_CurrentTurnId, BlueprintReadOnly, Category = "Turn")
	int32 CurrentTurnId = 0;

	/** 現在の入力ウィンドウID（Replicated） */
	UPROPERTY(ReplicatedUsing = OnRep_InputWindowId, BlueprintReadOnly, Category = "Turn")
	int32 InputWindowId = 0;

	/** 現在のターンインデックス（レガシー互換用） */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Turn")
	int32 CurrentTurnIndex = 0;

	/** 現在のプレイヤーAP */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Turn|AP")
	int32 PlayerAP = 0;

	/** 1ターンあたりの最大AP */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Turn|AP")
	int32 PlayerAPPerTurn = 1;

	/** 敵フェーズがキューされているか */
	UPROPERTY()
	bool bEnemyPhaseQueued = false;

	/** 最初のターンが開始されたか（リトライ防止用） */
	UPROPERTY(Replicated)
	bool bFirstTurnStarted = false;
};
