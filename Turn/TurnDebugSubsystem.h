// ============================================================================
// TurnDebugSubsystem.h
// ターンシステムデバッグSubsystem
// GameTurnManagerBaseから分離（2025-11-09）
// ============================================================================

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "TurnDebugSubsystem.generated.h"

class UDebugObserverCSV;

/**
 * ターンシステムのデバッグ機能を担当するSubsystem
 * 責務: デバッグ情報の出力、ログ、CSV記録
 */
UCLASS()
class LYRAGAME_API UTurnDebugSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// ========== Subsystem Lifecycle ==========
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ========== Debug Output ==========

	/**
	 * ターン状態をダンプ
	 * @param TurnId ターンID
	 */
	UFUNCTION(BlueprintCallable, Category = "Turn|Debug")
	void DumpTurnState(int32 TurnId);

	/**
	 * フェーズ遷移をログ
	 * @param OldPhase 旧フェーズ
	 * @param NewPhase 新フェーズ
	 */
	UFUNCTION(BlueprintCallable, Category = "Turn|Debug")
	void LogPhaseTransition(FGameplayTag OldPhase, FGameplayTag NewPhase);

	/**
	 * デバッグ情報を画面に描画
	 */
	UFUNCTION(BlueprintCallable, Category = "Turn|Debug")
	void DrawDebugInfo();

	/**
	 * CSV記録を開始
	 * @param FilePath ファイルパス
	 */
	UFUNCTION(BlueprintCallable, Category = "Turn|Debug")
	void StartCSVRecording(const FString& FilePath);

	/**
	 * CSV記録を停止
	 */
	UFUNCTION(BlueprintCallable, Category = "Turn|Debug")
	void StopCSVRecording();

	/**
	 * デバッグログを記録
	 * @param Category カテゴリ
	 * @param Message メッセージ
	 */
	UFUNCTION(BlueprintCallable, Category = "Turn|Debug")
	void LogDebugMessage(const FString& Category, const FString& Message);

	/**
	 * パフォーマンス統計をダンプ
	 */
	UFUNCTION(BlueprintCallable, Category = "Turn|Debug")
	void DumpPerformanceStats();

	/**
	 * デバッグ描画を有効化/無効化
	 * @param bEnabled 有効化するか
	 */
	UFUNCTION(BlueprintCallable, Category = "Turn|Debug")
	void SetDebugDrawEnabled(bool bEnabled);

	/**
	 * デバッグログを有効化/無効化
	 * @param bEnabled 有効化するか
	 */
	UFUNCTION(BlueprintCallable, Category = "Turn|Debug")
	void SetDebugLogEnabled(bool bEnabled);

protected:
	// ========== Internal State ==========

	/** CSVロガー */
	UPROPERTY(Transient)
	TObjectPtr<UDebugObserverCSV> CSVLogger = nullptr;

	/** デバッグ描画が有効か */
	UPROPERTY(Transient)
	bool bDebugDrawEnabled = false;

	/** デバッグログが有効か */
	UPROPERTY(Transient)
	bool bDebugLogEnabled = true;

	/** フレーム時間の履歴 */
	UPROPERTY(Transient)
	TArray<float> FrameTimeHistory;

	/** 最大履歴サイズ */
	static constexpr int32 MaxHistorySize = 100;

	// ========== Internal Helpers ==========

	/**
	 * 統計情報を更新
	 * @param DeltaTime デルタタイム
	 */
	void UpdateStats(float DeltaTime);

	/**
	 * デバッグ文字列をフォーマット
	 * @param Category カテゴリ
	 * @param Message メッセージ
	 * @return フォーマット済み文字列
	 */
	FString FormatDebugMessage(const FString& Category, const FString& Message) const;
};
