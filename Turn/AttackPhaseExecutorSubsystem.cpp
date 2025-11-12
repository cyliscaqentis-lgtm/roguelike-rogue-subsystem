// Copyright Epic Games, Inc. All Rights Reserved.

#include "Turn/AttackPhaseExecutorSubsystem.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Utility/RogueGameplayTags.h"

DEFINE_LOG_CATEGORY(LogAttackPhase);

//--------------------------------------------------------------------------
// Subsystem Lifecycle
//--------------------------------------------------------------------------

void UAttackPhaseExecutorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	CurrentIndex = 0;
	TurnId = -1;
	Queue.Empty();

	UE_LOG(LogAttackPhase, Log, TEXT("[AttackPhaseExecutor] Initialized"));
}

void UAttackPhaseExecutorSubsystem::Deinitialize()
{
	// デリゲート解除
	UnbindCurrentASC();

	UE_LOG(LogAttackPhase, Log, TEXT("[AttackPhaseExecutor] Deinitialized"));
	Super::Deinitialize();
}

//--------------------------------------------------------------------------
// 🌟 攻撃逐次実行API（Lumina提言A2）
//--------------------------------------------------------------------------

void UAttackPhaseExecutorSubsystem::BeginSequentialAttacks(
	const TArray<FResolvedAction>& AttackActions,
	int32 InTurnId)
{
	// 既存の実行をクリーンアップ
	UnbindCurrentASC();

	Queue = AttackActions;
	TurnId = InTurnId;
	CurrentIndex = 0;

	UE_LOG(LogAttackPhase, Log,
		TEXT("[Turn %d] BeginSequentialAttacks: %d attacks queued"),
		TurnId, Queue.Num());

	// 攻撃がない場合は即座に完了
	if (Queue.Num() == 0)
	{
		UE_LOG(LogAttackPhase, Warning,
			TEXT("[Turn %d] No attacks to execute"), TurnId);
		OnFinished.Broadcast(TurnId);
		return;
	}

	// 最初の攻撃を送出
	DispatchNext();
}

void UAttackPhaseExecutorSubsystem::NotifyAttackCompleted(AActor* Attacker)
{
	// GA_Attackから直接呼ばれる場合の互換API
	if (Attacker)
	{
		UE_LOG(LogAttackPhase, Verbose,
			TEXT("[Turn %d] NotifyAttackCompleted: %s"),
			TurnId, *Attacker->GetName());
	}

	// ASC完了イベントと同じ処理
	OnAbilityCompleted(nullptr);
}

//--------------------------------------------------------------------------
// 内部実装
//--------------------------------------------------------------------------

void UAttackPhaseExecutorSubsystem::DispatchNext()
{
	// キュー終端チェック
	if (CurrentIndex >= Queue.Num())
	{
		UE_LOG(LogAttackPhase, Log,
			TEXT("[Turn %d] All %d attacks completed"),
			TurnId, Queue.Num());

		// デリゲート解除
		UnbindCurrentASC();

		// 完了通知
		OnFinished.Broadcast(TurnId);
		return;
	}

	const FResolvedAction& Action = Queue[CurrentIndex];

	// Actorバリデーション
	if (!Action.Actor.IsValid())
	{
		UE_LOG(LogAttackPhase, Warning,
			TEXT("[Turn %d] Invalid actor at index %d, skipping"),
			TurnId, CurrentIndex);
		CurrentIndex++;
		DispatchNext(); // 再帰呼び出しで次へ
		return;
	}

	AActor* Attacker = Action.Actor.Get();
	UAbilitySystemComponent* ASC =
		UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Attacker);

	// ASCバリデーション
	if (!ASC)
	{
		UE_LOG(LogAttackPhase, Warning,
			TEXT("[Turn %d] %s has no ASC, skipping"),
			TurnId, *Attacker->GetName());
		CurrentIndex++;
		DispatchNext(); // 再帰呼び出しで次へ
		return;
	}

	// 🌟 ASC完了イベント購読（Lumina提言：ポーリング廃止）
	BindASC(ASC);

	// GameplayEvent送出
	FGameplayEventData Payload;
	Payload.EventTag = Action.AbilityTag;
	Payload.Instigator = Attacker;
	Payload.TargetData = Action.TargetData;

	// ★★★ CRITICAL DIAGNOSTIC (2025-11-12): Ability診断 ★★★
	UE_LOG(LogAttackPhase, Warning,
		TEXT("[Turn %d] %s: ASC has %d activatable abilities"),
		TurnId, *Attacker->GetName(), ASC->GetActivatableAbilities().Num());

	for (int32 i = 0; i < ASC->GetActivatableAbilities().Num(); ++i)
	{
		const FGameplayAbilitySpec& Spec = ASC->GetActivatableAbilities()[i];
		if (Spec.Ability)
		{
			// ★★★ AbilityTriggersはprotectedなので、クラス名と基本情報のみ出力 ★★★
			UE_LOG(LogAttackPhase, Warning,
				TEXT("  [%d] Ability=%s (Class=%s), Level=%d, InputID=%d, IsActive=%d"),
				i, *Spec.Ability->GetName(), *Spec.Ability->GetClass()->GetName(),
				Spec.Level, Spec.InputID, Spec.IsActive());

			// アセットタグを確認（攻撃アビリティかどうか）
			FGameplayTagContainer AssetTags;
			Spec.Ability->GetAssetTags(AssetTags);
			UE_LOG(LogAttackPhase, Warning,
				TEXT("    AssetTags: %s"),
				*AssetTags.ToStringSimple());
		}
	}

	UE_LOG(LogAttackPhase, Warning,
		TEXT("[Turn %d] Sending GameplayEvent: Tag=%s (to %s)"),
		TurnId, *Action.AbilityTag.ToString(), *Attacker->GetName());

	const int32 TriggeredCount = ASC->HandleGameplayEvent(Payload.EventTag, &Payload);

	if (TriggeredCount > 0)
	{
		UE_LOG(LogAttackPhase, Log,
			TEXT("[Turn %d] Dispatched attack %d/%d: %s (Tag=%s)"),
			TurnId, CurrentIndex + 1, Queue.Num(),
			*Attacker->GetName(), *Action.AbilityTag.ToString());
	}
	else
	{
		UE_LOG(LogAttackPhase, Warning,
			TEXT("[Turn %d] %s: %s failed to trigger any abilities"),
			TurnId, *Attacker->GetName(), *Action.AbilityTag.ToString());

		// トリガーに失敗した場合は即座に次へ
		UnbindCurrentASC();
		CurrentIndex++;
		DispatchNext();
	}
}

void UAttackPhaseExecutorSubsystem::BindASC(UAbilitySystemComponent* ASC)
{
	if (!ASC)
	{
		UE_LOG(LogAttackPhase, Error, TEXT("[BindASC] ASC is null"));
		return;
	}

	// 既存のバインド解除
	UnbindCurrentASC();

	WaitingASC = ASC;

	// 🌟 ASC完了イベント購読（Lumina提言：決定論的な完了検出）
	// GA_Attackが SendCompletionEvent(Turn.Ability.Completed) を送ると
	// OnAbilityCompleted が呼ばれる
	AbilityCompletedHandle = ASC->GenericGameplayEventCallbacks.FindOrAdd(
		RogueGameplayTags::Gameplay_Event_Turn_Ability_Completed
	).AddUObject(this, &UAttackPhaseExecutorSubsystem::OnAbilityCompleted);

	UE_LOG(LogAttackPhase, Verbose,
		TEXT("[Turn %d] Bound to ASC: %s"),
		TurnId, *ASC->GetOwner()->GetName());
}

void UAttackPhaseExecutorSubsystem::UnbindCurrentASC()
{
	if (WaitingASC.IsValid() && AbilityCompletedHandle.IsValid())
	{
		UAbilitySystemComponent* ASC = WaitingASC.Get();

		if (ASC)
		{
			ASC->GenericGameplayEventCallbacks.FindOrAdd(
				RogueGameplayTags::Gameplay_Event_Turn_Ability_Completed
			).Remove(AbilityCompletedHandle);

			UE_LOG(LogAttackPhase, Verbose,
				TEXT("[Turn %d] Unbound from ASC: %s"),
				TurnId, *ASC->GetOwner()->GetName());
		}

		AbilityCompletedHandle.Reset();
	}

	WaitingASC.Reset();
}

void UAttackPhaseExecutorSubsystem::OnAbilityCompleted(
	const FGameplayEventData* Payload)
{
	UE_LOG(LogAttackPhase, Log,
		TEXT("[Turn %d] Ability completed at index %d"),
		TurnId, CurrentIndex);

	// 完了したので次の攻撃へ
	UnbindCurrentASC();
	CurrentIndex++;
	DispatchNext();
}
