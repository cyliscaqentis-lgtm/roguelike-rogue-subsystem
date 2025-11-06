// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "Turn/TurnSystemTypes.h"
#include "AttackPhaseExecutorSubsystem.generated.h"

class UAbilitySystemComponent;

DECLARE_LOG_CATEGORY_EXTERN(LogAttackPhase, Log, All);

/**
 * 完了デリゲート（Dynamic Delegate）
 * 攻撃フェーズ完了時にBlueprintに通知
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnAttackPhaseFinished,
	int32, TurnId
);

/**
 * UAttackPhaseExecutorSubsystem
 *
 * 攻撃の逐次実行とASC完了イベント購読を管理。
 *
 * 【Lumina提言A2対応】
 * - Blueprint側の StartAttackQueue / DispatchNextAttack / WaitForCurrentAbilityCompletion を全廃
 * - ASC完了イベント購読による決定論的な逐次実行
 * - タイムアウト/ポーリングを排除し、無限ループを撲滅
 *
 * 使用例:
 *   AttackPhaseExecutor->BeginSequentialAttacks(ResolvedAttacks, TurnId);
 *   → OnFinished イベント購読で次フェーズへ遷移
 */
UCLASS()
class LYRAGAME_API UAttackPhaseExecutorSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	//--------------------------------------------------------------------------
	// Subsystem Lifecycle
	//--------------------------------------------------------------------------

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	//--------------------------------------------------------------------------
	// 🌟 攻撃逐次実行API（Lumina提言A2）
	//--------------------------------------------------------------------------

	/**
	 * 逐次攻撃の開始
	 *
	 * キューに登録された攻撃を順番に実行し、各攻撃の完了を
	 * ASC完了イベント購読で監視。すべて完了時に OnFinished を発火。
	 *
	 * @param AttackActions 攻撃アクション配列（ResolvedAction）
	 * @param InTurnId ターンID
	 */
	UFUNCTION(BlueprintCallable, Category = "Turn|Exec")
	void BeginSequentialAttacks(
		const TArray<FResolvedAction>& AttackActions,
		int32 InTurnId
	);

	/**
	 * 攻撃完了通知（GA_Attackから直接呼ばれる場合の互換API）
	 *
	 * GA_Attack内でこの関数を呼ぶことで、ASCイベント購読と
	 * 同等の完了通知を送ることができる。
	 *
	 * @param Attacker 攻撃者
	 */
	UFUNCTION(BlueprintCallable, Category = "Turn|Exec")
	void NotifyAttackCompleted(AActor* Attacker);

	/**
	 * 完了デリゲート
	 * すべての攻撃完了時にBroadcast
	 */
	UPROPERTY(BlueprintAssignable, Category = "Turn|Exec")
	FOnAttackPhaseFinished OnFinished;

	/**
	 * 現在実行中かどうか
	 */
	UFUNCTION(BlueprintPure, Category = "Turn|Exec")
	bool IsExecuting() const { return CurrentIndex < Queue.Num(); }

	/**
	 * キューに残っている攻撃数
	 */
	UFUNCTION(BlueprintPure, Category = "Turn|Exec")
	int32 GetRemainingAttackCount() const
	{
		return FMath::Max(0, Queue.Num() - CurrentIndex);
	}

private:
	//--------------------------------------------------------------------------
	// 内部実装
	//--------------------------------------------------------------------------

	/**
	 * 次の攻撃を送出
	 */
	void DispatchNext();

	/**
	 * ASC完了イベントのバインド
	 *
	 * @param ASC バインド対象のAbilitySystemComponent
	 */
	void BindASC(UAbilitySystemComponent* ASC);

	/**
	 * ASC完了イベントのコールバック
	 *
	 * GA_Attackが SendCompletionEvent() を呼ぶと、この関数が実行される。
	 *
	 * @param EventTag 完了イベントタグ（Turn.Ability.Completed）
	 * @param Payload イベントペイロード
	 */
	void OnAbilityCompleted(const FGameplayEventData* Payload);

	/**
	 * デリゲート解除
	 *
	 * 次の攻撃に移る前、または完了時に既存のバインドを解除
	 */
	void UnbindCurrentASC();

	//--------------------------------------------------------------------------
	// 内部状態
	//--------------------------------------------------------------------------

	/** 攻撃キュー */
	UPROPERTY()
	TArray<FResolvedAction> Queue;

	/** 現在のインデックス */
	int32 CurrentIndex = 0;

	/** ターンID */
	int32 TurnId = -1;

	/** 現在待機中のASC */
	UPROPERTY()
	TWeakObjectPtr<UAbilitySystemComponent> WaitingASC;

	/** ASC完了イベントのデリゲートハンドル */
	FDelegateHandle AbilityCompletedHandle;
};
