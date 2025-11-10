// ============================================================================
// TurnCommandHandler.h
// プレイヤーコマンド処理Subsystem
// GameTurnManagerBaseから分離（2025-11-09）
// ============================================================================

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Turn/TurnSystemTypes.h"
#include "TurnCommandHandler.generated.h"

class APlayerControllerBase;

/**
 * プレイヤーコマンド処理を担当するSubsystem
 * 責務: コマンド検証、受理、適用
 */
UCLASS()
class LYRAGAME_API UTurnCommandHandler : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// ========== Subsystem Lifecycle ==========
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ========== Command Processing ==========

	/**
	 * プレイヤーコマンドを処理
	 * @param Command 処理するコマンド
	 * @return 成功した場合true
	 */
	UFUNCTION(BlueprintCallable, Category = "Turn|Command")
	bool ProcessPlayerCommand(const FPlayerCommand& Command);

	/**
	 * コマンドの妥当性を検証
	 * @param Command 検証するコマンド
	 * @return 妥当な場合true
	 */
	UFUNCTION(BlueprintCallable, Category = "Turn|Command")
	bool ValidateCommand(const FPlayerCommand& Command) const;

	/**
	 * コマンドを適用
	 * @param Command 適用するコマンド
	 */
	UFUNCTION(BlueprintCallable, Category = "Turn|Command")
	void ApplyCommand(const FPlayerCommand& Command);

	/**
	 * ★★★ コマンドを"受理済み"としてマーク (2025-11-10) ★★★
	 * MovePrecheck成功後にのみ呼ばれる。
	 * これにより、拒否時の再入力がDuplicate扱いされなくなる。
	 * @param Command 受理するコマンド
	 */
	UFUNCTION(BlueprintCallable, Category = "Turn|Command")
	void MarkCommandAsAccepted(const FPlayerCommand& Command);

	/**
	 * 入力ウィンドウを開始
	 * @param WindowId ウィンドウID
	 */
	UFUNCTION(BlueprintCallable, Category = "Turn|Command")
	void BeginInputWindow(int32 WindowId);

	/**
	 * 入力ウィンドウを終了
	 */
	UFUNCTION(BlueprintCallable, Category = "Turn|Command")
	void EndInputWindow();

	/**
	 * 最後に受理されたコマンドを取得
	 * @param TurnId ターンID
	 * @param OutCommand 出力先
	 * @return コマンドが存在した場合true
	 */
	UFUNCTION(BlueprintCallable, Category = "Turn|Command")
	bool GetLastAcceptedCommand(int32 TurnId, FPlayerCommand& OutCommand) const;

	/**
	 * 現在の入力ウィンドウIDを取得
	 */
	UFUNCTION(BlueprintPure, Category = "Turn|Command")
	int32 GetCurrentInputWindowId() const { return CurrentInputWindowId; }

	/**
	 * すべてのコマンド履歴をクリア
	 */
	UFUNCTION(BlueprintCallable, Category = "Turn|Command")
	void ClearCommandHistory();

protected:
	// ========== Internal State ==========

	/** 最後に受理されたコマンド（TurnId -> Command） */
	UPROPERTY(Transient)
	TMap<int32, FPlayerCommand> LastAcceptedCommands;

	/** 現在の入力ウィンドウID */
	UPROPERTY(Transient)
	int32 CurrentInputWindowId = 0;

	/** 入力ウィンドウが開いているか */
	UPROPERTY(Transient)
	bool bInputWindowOpen = false;

	// ========== Internal Helpers ==========

	/**
	 * コマンドがタイムアウトしていないか確認
	 * @param Command 確認するコマンド
	 * @return タイムアウトしていない場合true
	 */
	bool IsCommandTimely(const FPlayerCommand& Command) const;

	/**
	 * コマンドの重複チェック
	 * @param Command 確認するコマンド
	 * @return 重複していない場合true
	 */
	bool IsCommandUnique(const FPlayerCommand& Command) const;
};
