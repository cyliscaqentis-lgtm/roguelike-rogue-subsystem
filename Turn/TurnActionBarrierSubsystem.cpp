// ============================================================================
// ファイル: Source/LyraGame/Rogue/Turn/TurnActionBarrierSubsystem.cpp
// 用途: ターンアクション完了の同期バリア（ActionIDベース実装）
// 作成日: 2025-10-26
// 修正日: 2025-10-29 (ActionID方式に全面改修、3タグシステム対応)
// ============================================================================

#include "TurnActionBarrierSubsystem.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "TimerManager.h"
#include "GameFramework/GameModeBase.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "EngineUtils.h"

// ============================================================================
// ログカテゴリ定義
// ============================================================================
DEFINE_LOG_CATEGORY(LogTurnBarrier);

// ============================================================================
// StatId定義（Tick用）- REMOVED: Tickは使用されなくなりました
// ============================================================================
// DECLARE_CYCLE_STAT(TEXT("TurnBarrier Tick"), STAT_TurnBarrierTick, STATGROUP_Game);

// ============================================================================
// UTurnActionBarrierSubsystem 実装
// ============================================================================

void UTurnActionBarrierSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    UE_LOG(LogTurnBarrier, Log, TEXT("[Barrier] Subsystem Initialized"));
}

void UTurnActionBarrierSubsystem::Deinitialize()
{
    // 未完了のアクションがある場合は警告
    for (auto& TurnPair : TurnStates)
    {
        const int32 TurnId = TurnPair.Key;
        FTurnState& State = TurnPair.Value;
        // CompactTurnState(State);  // TODO: Implement if needed

        const int32 PendingCount = State.PendingActionIds.Num();
        if (PendingCount > 0)
        {
            UE_LOG(LogTurnBarrier, Error,
                TEXT("[Barrier] Deinitialize with pending actions: Turn=%d Count=%d"),
                TurnId, PendingCount);
        }
    }

    Super::Deinitialize();
}

bool UTurnActionBarrierSubsystem::IsServer() const
{
    UWorld* World = GetWorld();
    return World && World->GetAuthGameMode() != nullptr;
}

TStatId UTurnActionBarrierSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UTurnActionBarrierSubsystem, STATGROUP_Tickables);
}

// ============================================================================
// 公開API: BeginTurn
// ============================================================================

void UTurnActionBarrierSubsystem::BeginTurn(int32 TurnId)
{
    // サーバー専用
    if (!IsServer())
    {
        return;
    }

    // CurrentTurnIdを更新
    CurrentTurnId = TurnId;
    CurrentKey.TurnId = TurnId;

    // 新しいターンの状態を初期化
    FTurnState& State = TurnStates.FindOrAdd(TurnId);
    State.TurnStartTime = FPlatformTime::Seconds();
    State.PendingActionIds.Reset();
    State.ActorToAction.Reset();
    State.ActionToActor.Reset();
    State.ActionStartTimes.Reset();

    if (bEnableVerboseLogging)
    {
        UE_LOG(LogTurnBarrier, Log, TEXT("[Barrier] BeginTurn: Turn=%d"), TurnId);
    }

    // 古いターンの掃除（2ターン以前は削除）
    RemoveOldTurns(TurnId);

    // ★★★ 最適化: Tick→Timer変換（2025-11-09）
    // タイムアウトチェックを1秒ごとのタイマーで実行
    UWorld* World = GetWorld();
    if (World)
    {
        World->GetTimerManager().SetTimer(
            TimeoutCheckTimer,
            this,
            &UTurnActionBarrierSubsystem::CheckTimeouts,
            1.0f,  // 1秒ごと
            true   // ループ
        );
    }
}

// ============================================================================
// 公開API: RegisterAction
// ============================================================================

FGuid UTurnActionBarrierSubsystem::RegisterAction(AActor* Actor, int32 TurnId)
{
    // サーバー専用
    if (!IsServer())
    {
        return FGuid();
    }

    if (!Actor)
    {
        UE_LOG(LogTurnBarrier, Warning, TEXT("[Barrier] RegisterAction: null Actor"));
        return FGuid();
    }

    // 一意なActionIDを生成
    FGuid ActionId = FGuid::NewGuid();

    // ターン状態を取得または作成
    FTurnState& State = TurnStates.FindOrAdd(TurnId);

    // ActorのActionセットに追加
    State.PendingActions.FindOrAdd(Actor).Add(ActionId);

    // 登録時刻を記録（タイムアウト検出用）
    State.ActionStartTimes.Add(ActionId, FPlatformTime::Seconds());

    // 現在の保留アクション数を計算
    int32 TotalPending = GetPendingActionCount(TurnId);

    // Verbose: 個別の登録は冗長（合計数は別途ログ）
    UE_LOG(LogTurnBarrier, Verbose,
        TEXT("[Barrier] ✅ REGISTER: Turn=%d Actor=%s Action=%s (Total=%d)"),
        TurnId, *Actor->GetName(), *ActionId.ToString(), TotalPending);

    return ActionId;
}

// ============================================================================
// 公開API: CompleteAction
// ============================================================================

void UTurnActionBarrierSubsystem::CompleteAction(AActor* Actor, int32 TurnId, const FGuid& ActionId)
{
    // サーバー専用
    if (!IsServer())
    {
        return;
    }

    if (!Actor || !ActionId.IsValid())
    {
        return;
    }

    //==========================================================================
    // ★★★ (1) ターン状態の取得
    //==========================================================================
    FTurnState* State = TurnStates.Find(TurnId);
    if (!State)
    {
        // 古いターンの完了通知は無視（ログだけ出す）
        if (bEnableVerboseLogging)
        {
            UE_LOG(LogTurnBarrier, Verbose,
                TEXT("[Barrier] Complete(Ignored): Turn=%d Actor=%s Action=%s (Turn not found)"),
                TurnId, *Actor->GetName(), *ActionId.ToString());
        }
        return;
    }

    //==========================================================================
    // ★★★ (2) Actorの保留アクションセットを取得
    //==========================================================================
    TArray<FGuid>* ActionSet = State->PendingActions.Find(Actor);
    if (!ActionSet)
    {
        if (bEnableVerboseLogging)
        {
            UE_LOG(LogTurnBarrier, Verbose,
                TEXT("[Barrier] Complete(NoActor): Turn=%d Actor=%s Action=%s"),
                TurnId, *Actor->GetName(), *ActionId.ToString());
        }
        return;
    }

    //==========================================================================
    // ★★★ (3) ActionIDの削除（冪等性: 既に削除済みなら何もしない）
    //==========================================================================
    int32 RemovedCount = ActionSet->Remove(ActionId);
    if (RemovedCount > 0)
    {
        // 成功: ActionSetが空になったらActorも削除
        if (ActionSet->Num() == 0)
        {
            State->PendingActions.Remove(Actor);
        }

        // ActionStartTimesからも削除
        State->ActionStartTimes.Remove(ActionId);

        // 残りのアクション数
        int32 Remaining = GetPendingActionCount(TurnId);

        // Verbose: 個別の完了は冗長（Remaining=0は別途ログ）
        UE_LOG(LogTurnBarrier, Verbose,
            TEXT("[Barrier] ✅ COMPLETE: Turn=%d Actor=%s Action=%s (Remaining=%d)"),
            TurnId, *Actor->GetName(), *ActionId.ToString(), Remaining);

        // ★★★ 新規追加：0到達時に即通知（ActionIDモデル完成） ★★★
        if (Remaining == 0)
        {
            // Warning: 全アクション完了は重要イベントなので可視性保つ
            UE_LOG(LogTurnBarrier, Warning,
                TEXT("[Barrier] 🎉 Turn %d: ALL ACTIONS COMPLETED (Remaining=0) -> Broadcasting OnAllMovesFinished"),
                TurnId);
            OnAllMovesFinished.Broadcast(TurnId);  // ← ここで確実に発火
        }
    }
    else
    {
        // 重複完了は黙って無視（Verboseログのみ）
        if (bEnableVerboseLogging)
        {
            UE_LOG(LogTurnBarrier, Verbose,
                TEXT("[Barrier] Complete(Duplicate): Turn=%d Actor=%s Action=%s"),
                TurnId, *Actor->GetName(), *ActionId.ToString());
        }
    }
}

// ============================================================================
// 公開API: IsQuiescent
// ============================================================================

bool UTurnActionBarrierSubsystem::IsQuiescent(int32 TurnId) const
{
    const FTurnState* State = TurnStates.Find(TurnId);
    if (!State)
    {
        // ターン情報がなければ完了とみなす
        return true;
    }

    // 無効なWeakPtrを除外してカウント
    int32 ValidPendingCount = 0;
    for (const auto& Pair : State->PendingActions)
    {
        if (Pair.Key.IsValid() && Pair.Value.Num() > 0)
        {
            ValidPendingCount++;
        }
    }

    return ValidPendingCount == 0;
}

// ============================================================================
// 公開API: GetPendingActionCount
// ============================================================================

int32 UTurnActionBarrierSubsystem::GetPendingActionCount(int32 TurnId) const
{
    const FTurnState* State = TurnStates.Find(TurnId);
    if (!State)
    {
        return 0;
    }

    int32 Count = 0;
    for (const auto& Pair : State->PendingActions)
    {
        if (Pair.Key.IsValid())
        {
            Count += Pair.Value.Num();
        }
    }
    return Count;
}

// ============================================================================
// 冪等API: RegisterActionOnce
// ============================================================================

void UTurnActionBarrierSubsystem::RegisterActionOnce(AActor* Owner, FGuid& OutToken)
{
    if (!OutToken.IsValid())
    {
        OutToken = FGuid::NewGuid();
    }

    if (ActiveTokens.Contains(OutToken))
    {
        UE_LOG(LogTurnBarrier, VeryVerbose,
            TEXT("[RegisterActionOnce] Duplicate token=%s owner=%s"),
            *OutToken.ToString(),
            Owner ? *Owner->GetName() : TEXT("null"));
        return;
    }

    ActiveTokens.Add(OutToken);
    TokenOwners.Add(OutToken, Owner);

    UE_LOG(LogTurnBarrier, Verbose,
        TEXT("[RegisterActionOnce] token=%s owner=%s"),
        *OutToken.ToString(),
        Owner ? *Owner->GetName() : TEXT("null"));
}

// ============================================================================
// 冪等API: CompleteActionToken
// ============================================================================

void UTurnActionBarrierSubsystem::CompleteActionToken(const FGuid& Token)
{
    if (!Token.IsValid())
    {
        UE_LOG(LogTurnBarrier, VeryVerbose, TEXT("[CompleteActionToken] invalid token"));
        return;
    }

    if (!ActiveTokens.Remove(Token))
    {
        UE_LOG(LogTurnBarrier, VeryVerbose,
            TEXT("[CompleteActionToken] unknown token=%s"), *Token.ToString());
        return;
    }

    TokenOwners.Remove(Token);
    UE_LOG(LogTurnBarrier, Verbose,
        TEXT("[CompleteActionToken] token=%s completed"), *Token.ToString());
}

// ============================================================================
// Tick: タイムアウト監視（Phase 6で実装）
// ★★★ 最適化完了: Tick→Timer変換済み（2025-11-09）
// ============================================================================
/*
REMOVED: Tick is replaced with timer-based CheckTimeouts()
void UTurnActionBarrierSubsystem::Tick(float DeltaTime)
{
    SCOPE_CYCLE_COUNTER(STAT_TurnBarrierTick);
    if (!IsServer()) return;
    CheckTimeouts();
}
*/

// ============================================================================
// CheckTimeouts: タイムアウト検出と強制完了
// ★★★ 最適化: Tickから呼ばれていたが、現在は1秒ごとのタイマーで実行
// ============================================================================

void UTurnActionBarrierSubsystem::CheckTimeouts()
{
    double Now = FPlatformTime::Seconds();

    // 全ターンをチェック
    for (auto& TurnPair : TurnStates)
    {
        int32 TurnId = TurnPair.Key;
        FTurnState& State = TurnPair.Value;

        if (State.TurnStartTime <= 0.0)
        {
            continue;
        }

        double Elapsed = Now - State.TurnStartTime;
        if (Elapsed < ActionTimeoutSeconds)
        {
            continue; // まだタイムアウトしていない
        }

        //======================================================================
        // ★★★ タイムアウト処理: 個別Actionをチェック
        //======================================================================
        TArray<TWeakObjectPtr<AActor>> TimeoutActors;
        TArray<FGuid> TimeoutActions;

        for (const auto& ActorPair : State.PendingActions)
        {
            if (!ActorPair.Key.IsValid() || ActorPair.Value.Num() == 0)
            {
                continue;
            }

            AActor* Actor = ActorPair.Key.Get();

            // このActorの各Actionをチェック
            for (const FGuid& ActionId : ActorPair.Value)
            {
                double* StartTime = State.ActionStartTimes.Find(ActionId);
                if (!StartTime)
                {
                    continue;
                }

                double ActionElapsed = Now - *StartTime;
                if (ActionElapsed >= ActionTimeoutSeconds)
                {
                    // タイムアウト
                    TimeoutActors.Add(ActorPair.Key);
                    TimeoutActions.Add(ActionId);

                    UE_LOG(LogTurnBarrier, Error,
                        TEXT("[Barrier] Timeout: Turn=%d Actor=%s Action=%s Elapsed=%.2fs"),
                        TurnId, *Actor->GetName(), *ActionId.ToString(), ActionElapsed);
                }
            }
        }

        //======================================================================
        // ★★★ タイムアウトしたActionの処理
        //======================================================================
        for (int32 i = 0; i < TimeoutActors.Num(); ++i)
        {
            TWeakObjectPtr<AActor> ActorPtr = TimeoutActors[i];
            FGuid ActionId = TimeoutActions[i];

            if (!ActorPtr.IsValid())
            {
                continue;
            }

            AActor* Actor = ActorPtr.Get();

            // GAをキャンセル（設定で有効な場合）
            if (bCancelAbilitiesOnTimeout)
            {
                if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor))
                {
                    UE_LOG(LogTurnBarrier, Warning,
                        TEXT("[Barrier] Cancelling abilities: Actor=%s"),
                        *Actor->GetName());

                    // すべてのアビリティをキャンセル
                    ASC->CancelAbilities();
                }
            }

            // 強制完了
            CompleteAction(Actor, TurnId, ActionId);
        }
    }
}

// ============================================================================
// RemoveOldTurns: 古いターンのデータ削除
// ============================================================================

void UTurnActionBarrierSubsystem::RemoveOldTurns(int32 CurrentTurn)
{
    TArray<int32> KeysToRemove;
    for (const auto& Pair : TurnStates)
    {
        // 2ターン以前は削除
        if (Pair.Key < CurrentTurn - 1)
        {
            KeysToRemove.Add(Pair.Key);
        }
    }

    for (int32 Key : KeysToRemove)
    {
        int32 RemainingActions = GetPendingActionCount(Key);
        if (RemainingActions > 0)
        {
            UE_LOG(LogTurnBarrier, Warning,
                TEXT("[Barrier] Removing old turn with pending actions: Turn=%d Count=%d"),
                Key, RemainingActions);
        }
        TurnStates.Remove(Key);
    }

    if (KeysToRemove.Num() > 0)
    {
        UE_LOG(LogTurnBarrier, Verbose,
            TEXT("[Barrier] Cleaned up %d old turns"), KeysToRemove.Num());
    }
}


// ============================================================================
// デバッグAPI: DumpTurnState
// ============================================================================

void UTurnActionBarrierSubsystem::DumpTurnState(int32 TurnId) const
{
    const FTurnState* State = TurnStates.Find(TurnId);
    if (!State)
    {
        UE_LOG(LogTurnBarrier, Warning,
            TEXT("[Barrier] DumpTurnState: Turn=%d not found"), TurnId);
        return;
    }

    UE_LOG(LogTurnBarrier, Log,
        TEXT("[Barrier] ===== Turn %d State ====="), TurnId);
    UE_LOG(LogTurnBarrier, Log,
        TEXT("  TurnStartTime: %.2fs"), State->TurnStartTime);
    UE_LOG(LogTurnBarrier, Log,
        TEXT("  Total Pending: %d"), GetPendingActionCount(TurnId));

    for (const auto& ActorPair : State->PendingActions)
    {
        if (!ActorPair.Key.IsValid())
        {
            continue;
        }

        AActor* Actor = ActorPair.Key.Get();
        UE_LOG(LogTurnBarrier, Log,
            TEXT("  Actor: %s (Actions: %d)"),
            *Actor->GetName(), ActorPair.Value.Num());

        for (const FGuid& ActionId : ActorPair.Value)
        {
            const double* StartTime = State->ActionStartTimes.Find(ActionId);
            double Elapsed = StartTime ? (FPlatformTime::Seconds() - *StartTime) : 0.0;

            UE_LOG(LogTurnBarrier, Log,
                TEXT("    - Action: %s (Elapsed: %.2fs)"),
                *ActionId.ToString(), Elapsed);
        }
    }

    UE_LOG(LogTurnBarrier, Log,
        TEXT("[Barrier] ===== End Turn %d State ====="), TurnId);
}

//==============================================================================
// レガシーAPI実装（Phase 1互換用）
//==============================================================================

void UTurnActionBarrierSubsystem::StartMoveBatch(int32 InCount, int32 InTurnId)
{
    if (!IsServer())
    {
        return;
    }

    CurrentKey.TurnId = InTurnId;
    CurrentTurnId = InTurnId;
    PendingMoves = InCount;

    UE_LOG(LogTurnBarrier, Log,
        TEXT("Turn %d: StartMoveBatch Count=%d"),
        InTurnId, InCount);
}

void UTurnActionBarrierSubsystem::NotifyMoveStarted(AActor* Unit, int32 InTurnId)
{
    if (!IsServer() || !Unit)
    {
        return;
    }

    if (InTurnId != CurrentKey.TurnId)
    {
        UE_LOG(LogTurnBarrier, Warning,
            TEXT("Turn %d: IGNORE stale NotifyMoveStarted (current=%d)"),
            InTurnId, CurrentKey.TurnId);
        return;
    }

    UE_LOG(LogTurnBarrier, Verbose,
        TEXT("Turn %d: NotifyMoveStarted Actor=%s"),
        InTurnId, *GetNameSafe(Unit));
}

void UTurnActionBarrierSubsystem::NotifyMoveFinished(AActor* Unit, int32 InTurnId)
{
    if (!IsServer() || !Unit)
    {
        return;
    }

    if (InTurnId != CurrentKey.TurnId)
    {
        UE_LOG(LogTurnBarrier, Warning,
            TEXT("Turn %d: IGNORE stale NotifyMoveFinished (current=%d)"),
            InTurnId, CurrentKey.TurnId);
        return;
    }

    TWeakObjectPtr<AActor> WeakUnit(Unit);

    if (AlreadyNotified.Contains(WeakUnit))
    {
        UE_LOG(LogTurnBarrier, Warning,
            TEXT("Turn %d: DUPLICATE NotifyMoveFinished Actor=%s"),
            InTurnId, *GetNameSafe(Unit));
        return;
    }

    AlreadyNotified.Add(WeakUnit);
    NotifiedActorsThisTurn.Add(WeakUnit);

    --PendingMoves;

    UE_LOG(LogTurnBarrier, Log,
        TEXT("Turn %d: NotifyMoveFinished Actor=%s, Pending=%d"),
        InTurnId, *GetNameSafe(Unit), PendingMoves);

    if (PendingMoves <= 0)
    {
        FireAllFinished(InTurnId);
    }
}

void UTurnActionBarrierSubsystem::ForceFinishBarrier()
{
    if (!IsServer())
    {
        return;
    }

    UE_LOG(LogTurnBarrier, Warning,
        TEXT("Turn %d: ForceFinishBarrier (Pending=%d)"),
        CurrentKey.TurnId, PendingMoves);

    PendingMoves = 0;
    FireAllFinished(CurrentKey.TurnId);
}

TArray<AActor*> UTurnActionBarrierSubsystem::GetNotifiedUnits() const
{
    TArray<AActor*> Result;
    for (const TWeakObjectPtr<AActor>& WeakActor : AlreadyNotified)
    {
        if (AActor* Actor = WeakActor.Get())
        {
            Result.Add(Actor);
        }
    }
    return Result;
}

TArray<AActor*> UTurnActionBarrierSubsystem::GetNotifiedActorsThisTurn() const
{
    TArray<AActor*> Result;
    for (const TWeakObjectPtr<AActor>& WeakActor : NotifiedActorsThisTurn)
    {
        if (AActor* Actor = WeakActor.Get())
        {
            Result.Add(Actor);
        }
    }
    return Result;
}
//==============================================================================
// 内部ヘルパー: デリゲート発火
//==============================================================================

void UTurnActionBarrierSubsystem::FireAllFinished(int32 TurnId)
{
    if (!IsServer())
    {
        return;
    }

    UE_LOG(LogTurnBarrier, Log,
        TEXT("Turn %d: FireAllFinished - Broadcasting OnAllMovesFinished"),
        TurnId);

    // デリゲート発火（Blueprint購読用）
    OnAllMovesFinished.Broadcast(TurnId);

    // タイマークリア
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(SafetyTimeoutHandle);
    }

    // 次ターン用にクリア
    NotifiedActorsThisTurn.Empty();
}
