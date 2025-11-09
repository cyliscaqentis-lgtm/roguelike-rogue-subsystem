// ============================================================================
// TurnEventDispatcher.h
// ターンイベント配信Subsystem
// GameTurnManagerBaseから分離（2025-11-09）
// ============================================================================

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "TurnEventDispatcher.generated.h"

class AGameTurnManagerBase;
class URogueDungeonSubsystem;

// ========== デリゲート定義 ==========

/** ターン開始イベント */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTurnStarted, int32, TurnIndex);

/** プレイヤー入力受信イベント */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPlayerInputReceived);

/** フロア準備完了イベント */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFloorReady, URogueDungeonSubsystem*, DungeonSubsystem);

/** フェーズ変更イベント */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPhaseChanged, FGameplayTag, OldPhase, FGameplayTag, NewPhase);

/** ターン終了イベント */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTurnEnded, int32, TurnIndex);

/** アクション実行イベント */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnActionExecuted, int32, UnitID, FGameplayTag, ActionTag, bool, bSuccess);

/**
 * ターンイベントの配信を担当するSubsystem
 * 責務: イベント配信、デリゲート管理
 */
UCLASS()
class LYRAGAME_API UTurnEventDispatcher : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// ========== Subsystem Lifecycle ==========
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ========== イベントデリゲート ==========

	/** ターン開始時に呼ばれる */
	UPROPERTY(BlueprintAssignable, Category = "Turn|Events")
	FOnTurnStarted OnTurnStarted;

	/** プレイヤー入力受信時に呼ばれる */
	UPROPERTY(BlueprintAssignable, Category = "Turn|Events")
	FOnPlayerInputReceived OnPlayerInputReceived;

	/** フロア準備完了時に呼ばれる */
	UPROPERTY(BlueprintAssignable, Category = "Turn|Events")
	FOnFloorReady OnFloorReady;

	/** フェーズ変更時に呼ばれる */
	UPROPERTY(BlueprintAssignable, Category = "Turn|Events")
	FOnPhaseChanged OnPhaseChanged;

	/** ターン終了時に呼ばれる */
	UPROPERTY(BlueprintAssignable, Category = "Turn|Events")
	FOnTurnEnded OnTurnEnded;

	/** アクション実行時に呼ばれる */
	UPROPERTY(BlueprintAssignable, Category = "Turn|Events")
	FOnActionExecuted OnActionExecuted;

	// ========== イベント配信メソッド ==========

	/**
	 * ターン開始イベントを配信
	 * @param TurnIndex ターンインデックス
	 */
	UFUNCTION(BlueprintCallable, Category = "Turn|Events")
	void BroadcastTurnStarted(int32 TurnIndex);

	/**
	 * プレイヤー入力受信イベントを配信
	 */
	UFUNCTION(BlueprintCallable, Category = "Turn|Events")
	void BroadcastPlayerInputReceived();

	/**
	 * フロア準備完了イベントを配信
	 * @param DungeonSubsystem ダンジョンSubsystem
	 */
	UFUNCTION(BlueprintCallable, Category = "Turn|Events")
	void BroadcastFloorReady(URogueDungeonSubsystem* DungeonSubsystem);

	/**
	 * フェーズ変更イベントを配信
	 * @param OldPhase 旧フェーズ
	 * @param NewPhase 新フェーズ
	 */
	UFUNCTION(BlueprintCallable, Category = "Turn|Events")
	void BroadcastPhaseChanged(FGameplayTag OldPhase, FGameplayTag NewPhase);

	/**
	 * ターン終了イベントを配信
	 * @param TurnIndex ターンインデックス
	 */
	UFUNCTION(BlueprintCallable, Category = "Turn|Events")
	void BroadcastTurnEnded(int32 TurnIndex);

	/**
	 * アクション実行イベントを配信
	 * @param UnitID ユニットID
	 * @param ActionTag アクションタグ
	 * @param bSuccess 成功したか
	 */
	UFUNCTION(BlueprintCallable, Category = "Turn|Events")
	void BroadcastActionExecuted(int32 UnitID, FGameplayTag ActionTag, bool bSuccess);

	/**
	 * すべてのデリゲートをクリア
	 */
	UFUNCTION(BlueprintCallable, Category = "Turn|Events")
	void ClearAllDelegates();

protected:
	// ========== Internal State ==========

	/** イベント配信が有効か */
	UPROPERTY(Transient)
	bool bEventsEnabled = true;
};
