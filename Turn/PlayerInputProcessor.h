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
	void OpenInputWindow(int32 TurnId);

	/** 入力ウィンドウを閉じる */
	UFUNCTION(BlueprintCallable, Category = "Input", meta = (BlueprintAuthorityOnly))
	void CloseInputWindow();

	/** 入力ウィンドウが開いているか */
	UFUNCTION(BlueprintPure, Category = "Input")
	bool IsInputWindowOpen() const { return bWaitingForPlayerInput; }

	/** サーバー側で入力受付中か（Authority専用） */
	UFUNCTION(BlueprintPure, Category = "Input")
	bool IsInputOpen_Server() const;

	//==========================================================================
	// コマンド検証
	//==========================================================================

	/** コマンドのWindowIdが有効か検証 */
	UFUNCTION(BlueprintCallable, Category = "Input")
	bool ValidateCommand(const FPlayerCommand& Command, int32 ExpectedWindowId);

	//==========================================================================
	// コマンド処理
	//==========================================================================

	/** プレイヤーコマンドを処理 */
	UFUNCTION(BlueprintCallable, Category = "Input", meta = (BlueprintAuthorityOnly))
	void ProcessPlayerCommand(const FPlayerCommand& Command);

	/** 入力受付通知 */
	UFUNCTION(BlueprintCallable, Category = "Input")
	void NotifyPlayerInputReceived();

	//==========================================================================
	// キャッシュ取得
	//==========================================================================

	/** キャッシュされたプレイヤーコマンドを取得 */
	UFUNCTION(BlueprintPure, Category = "Input")
	FPlayerCommand GetCachedPlayerCommand() const { return CachedPlayerCommand; }

<<<<<<< HEAD
	//==========================================================================
	// デリゲート
	//==========================================================================

	DECLARE_MULTICAST_DELEGATE(FOnPlayerInputReceived);
	FOnPlayerInputReceived OnPlayerInputReceived;

=======
>>>>>>> origin/claude/ue5-rogue-refactor-complete-011CUvsUqjPorTXvdbGRGcm4
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

	/** 入力ゲートを適用（ASCへのタグ追加/削除） */
	void ApplyWaitInputGate(bool bOpen);

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
};
