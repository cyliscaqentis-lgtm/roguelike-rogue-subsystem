// PlayerInputProcessor.h

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Net/UnrealNetwork.h"
#include "Turn/TurnSystemTypes.h"
#include "PlayerInputProcessor.generated.h"

/**
 * UPlayerInputProcessor
 *
 * 責務:
 * - 入力ウィンドウの開閉
 * - プレイヤーコマンドの検証
 * - WindowIdの整合性チェック
 * - 入力受付状態の管理
 */
UCLASS()
class LYRAGAME_API UPlayerInputProcessor : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	//==========================================================================
	// 入力ウィンドウ管理
	//==========================================================================

	/** 入力ウィンドウを開く */
	UFUNCTION(BlueprintCallable, Category = "Input", meta = (BlueprintAuthorityOnly))
	void OpenInputWindow(int32 TurnId, int32 WindowId);

	/** 入力ウィンドウを閉じる */
	UFUNCTION(BlueprintCallable, Category = "Input", meta = (BlueprintAuthorityOnly))
	void CloseInputWindow();

	/** 入力ウィンドウが開いているか */
	UFUNCTION(BlueprintPure, Category = "Input")
	bool IsInputWindowOpen() const { return bWaitingForPlayerInput; }

	/** サーバー側で入力受付中か（Authority専用） */
	UFUNCTION(BlueprintPure, Category = "Input")
	bool IsInputOpen_Server() const;

	/** 入力ゲートを適用（ASCへのタグ追加/削除） */
	void ApplyWaitInputGate(bool bOpen);

	//==========================================================================
	// Phase 7: Manager Delegation API
	//==========================================================================

	/** Open input window for a specific turn (delegated from Manager) */
	UFUNCTION(BlueprintCallable, Category = "Input", meta = (BlueprintAuthorityOnly))
	void OpenTurnInputWindow(class AGameTurnManagerBase* Manager, int32 TurnId);

	/** Close input window (delegated from Manager) */
	UFUNCTION(BlueprintCallable, Category = "Input", meta = (BlueprintAuthorityOnly))
	void CloseTurnInputWindow(class AGameTurnManagerBase* Manager);

	/** Handle player move finalization (delegated from Manager) */
	void OnPlayerMoveFinalized(class AGameTurnManagerBase* Manager, AActor* CompletedActor);

	/** Handle player possession (delegated from Manager) */
	void OnPlayerPossessed(class AGameTurnManagerBase* Manager, APawn* NewPawn);

	/** Check if input is open on server (delegated from Manager) */
	bool IsInputOpen_Server(const class AGameTurnManagerBase* Manager) const;

	/** Toggle State.Action.InProgress tag */
	void MarkMoveInProgress(bool bInProgress);

	/** Set player move state (direction and progress flag) */
	void SetPlayerMoveState(const FVector& Direction, bool bInProgress);

	//==========================================================================
	// コマンド検証
	//==========================================================================

	/** コマンドのTurnId/WindowIdが有効か検証 */
	UFUNCTION(BlueprintCallable, Category = "Input")
	bool ValidateCommand(const FPlayerCommand& Command);

	//==========================================================================
	// コマンド処理
	//==========================================================================

	/** プレイヤーコマンドを処理 */
	UFUNCTION(BlueprintCallable, Category = "Input", meta = (BlueprintAuthorityOnly))
	void ProcessPlayerCommand(const FPlayerCommand& Command);

	//==========================================================================
	// キャッシュ取得
	//==========================================================================

	/** キャッシュされたプレイヤーコマンドを取得 */
	UFUNCTION(BlueprintPure, Category = "Input")
	FPlayerCommand GetCachedPlayerCommand() const { return CachedPlayerCommand; }

protected:
	//==========================================================================
	// レプリケーション
	//==========================================================================

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual bool IsSupportedForNetworking() const override { return true; }

	UFUNCTION()
	void OnRep_WaitingForPlayerInput();

	//==========================================================================
	// 内部ヘルパー
	//==========================================================================

	/** WindowIdが有効か検証 */
	bool IsValidWindowId(int32 WindowId) const;

	/** サーバー権限で入力受付状態を設定 */
	void SetWaitingForPlayerInput_ServerLike(bool bNew);

	//==========================================================================
	// メンバー変数
	//==========================================================================

	/** 入力待機中か（Replicated） */
	UPROPERTY(ReplicatedUsing = OnRep_WaitingForPlayerInput, BlueprintReadOnly, Category = "Input")
	bool bWaitingForPlayerInput = false;

	/** キャッシュされたプレイヤーコマンド */
	UPROPERTY(BlueprintReadOnly, Category = "Input")
	FPlayerCommand CachedPlayerCommand;

	/** 現在の入力ウィンドウに対応するTurnId */
	UPROPERTY()
	int32 CurrentInputWindowTurnId = 0;

	/** 現在受け付けているTurnId */
	UPROPERTY()
	int32 CurrentAcceptedTurnId = -1;

	/** 現在受け付けているWindowId */
	UPROPERTY()
	int32 CurrentAcceptedWindowId = -1;

	/** 入力ウィンドウが開いているか */
	UPROPERTY()
	bool bInputWindowOpen = false;
};
