// ============================================================================
// File: Source/LyraGame/Rogue/Turn/TurnActionBarrierSubsystem.cpp
// Purpose: Turn action completion barrier (ActionID-based implementation)
// Created: 2025-10-26
// Updated: 2025-10-29 (Full ActionID migration, 3-tag system support)
// ============================================================================

#include "TurnActionBarrierSubsystem.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "TimerManager.h"
#include "GameFramework/GameModeBase.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "EngineUtils.h"
#include "Utility/TurnAuthorityUtils.h"

// ============================================================================
// Log category
// ============================================================================
DEFINE_LOG_CATEGORY(LogTurnBarrier);

// ============================================================================
// StatId for Tick (no longer used)
// ============================================================================
// DECLARE_CYCLE_STAT(TEXT("TurnBarrier Tick"), STAT_TurnBarrierTick, STATGROUP_Game);

// ============================================================================
// UTurnActionBarrierSubsystem implementation
// ============================================================================

void UTurnActionBarrierSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    UE_LOG(LogTurnBarrier, Log, TEXT("[Barrier] Subsystem Initialized"));
}

void UTurnActionBarrierSubsystem::Deinitialize()
{
    // Warn if there are any pending actions when the subsystem shuts down
    for (auto& TurnPair : TurnStates)
    {
        const int32 TurnId = TurnPair.Key;
        FTurnState& State = TurnPair.Value;

        const int32 PendingCount = GetPendingActionCount(TurnId);
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
    return IsAuthorityLike(GetWorld());
}

// ============================================================================
// Public API: BeginTurn
// ============================================================================

void UTurnActionBarrierSubsystem::BeginTurn(int32 TurnId)
{
    // Server-only
    if (!IsServer())
    {
        return;
    }

    // Update current turn tracking
    CurrentTurnId = TurnId;
    CurrentKey.TurnId = TurnId;

    // Initialize state for this turn
    FTurnState& State = TurnStates.FindOrAdd(TurnId);
    State.TurnStartTime = FPlatformTime::Seconds();
    State.PendingActions.Reset();
    State.ActionStartTimes.Reset();
    State.ActorToAction.Reset();
    State.ActionToActor.Reset();

    if (bEnableVerboseLogging)
    {
        UE_LOG(LogTurnBarrier, Log, TEXT("[Barrier] BeginTurn: Turn=%d"), TurnId);
    }

    // Cleanup old turns (keep only the last 2 turns)
    RemoveOldTurns(TurnId);

    // Timer-based timeout check (replaces Tick)
    UWorld* World = GetWorld();
    if (World)
    {
        World->GetTimerManager().SetTimer(
            TimeoutCheckTimer,
            this,
            &UTurnActionBarrierSubsystem::CheckTimeouts,
            1.0f,  // every 1 second
            true   // looping
        );
    }
}

// ============================================================================
// Public API: RegisterAction
// ============================================================================

FGuid UTurnActionBarrierSubsystem::RegisterAction(AActor* Actor, int32 TurnId)
{
    // Server-only
    if (!IsServer())
    {
        return FGuid();
    }

    if (!Actor)
    {
        UE_LOG(LogTurnBarrier, Warning, TEXT("[Barrier] RegisterAction: null Actor"));
        return FGuid();
    }

    // Generate a unique ActionId
    FGuid ActionId = FGuid::NewGuid();

    // Get or create the turn state
    FTurnState& State = TurnStates.FindOrAdd(TurnId);

    // Register this ActionId in the actor's pending set
    State.PendingActions.FindOrAdd(Actor).Add(ActionId);

    // Record registration time for timeout detection
    State.ActionStartTimes.Add(ActionId, FPlatformTime::Seconds());

    // Compute total pending count for logging
    const int32 TotalPending = GetPendingActionCount(TurnId);

    UE_LOG(LogTurnBarrier, Verbose,
        TEXT("[Barrier] REGISTER: Turn=%d Actor=%s Action=%s (Total=%d)"),
        TurnId, *GetNameSafe(Actor), *ActionId.ToString(), TotalPending);

    return ActionId;
}

// ============================================================================
// Public API: CompleteAction
// ============================================================================

void UTurnActionBarrierSubsystem::CompleteAction(AActor* Actor, int32 TurnId, const FGuid& ActionId)
{
    // Server-only
    if (!IsServer())
    {
        return;
    }

    if (!Actor || !ActionId.IsValid())
    {
        return;
    }

    //--------------------------------------------------------------------------
    // (1) Resolve turn state
    //--------------------------------------------------------------------------
    FTurnState* State = TurnStates.Find(TurnId);
    if (!State)
    {
        // Completion for an old / unknown turn is ignored (log only)
        if (bEnableVerboseLogging)
        {
            UE_LOG(LogTurnBarrier, Verbose,
                TEXT("[Barrier] Complete(Ignored): Turn=%d Actor=%s Action=%s (Turn not found)"),
                TurnId, *GetNameSafe(Actor), *ActionId.ToString());
        }
        return;
    }

    //--------------------------------------------------------------------------
    // (2) Lookup pending actions for this actor
    //--------------------------------------------------------------------------
    TArray<FGuid>* ActionSet = State->PendingActions.Find(Actor);
    if (!ActionSet)
    {
        if (bEnableVerboseLogging)
        {
            UE_LOG(LogTurnBarrier, Verbose,
                TEXT("[Barrier] Complete(NoActor): Turn=%d Actor=%s Action=%s"),
                TurnId, *GetNameSafe(Actor), *ActionId.ToString());
        }
        return;
    }

    //--------------------------------------------------------------------------
    // (3) Remove ActionId (idempotent; ignore if already removed)
    //--------------------------------------------------------------------------
    const int32 RemovedCount = ActionSet->Remove(ActionId);
    if (RemovedCount > 0)
    {
        // If the actor has no more actions, remove the actor entry
        if (ActionSet->Num() == 0)
        {
            State->PendingActions.Remove(Actor);
        }

        // Remove from ActionStartTimes as well
        State->ActionStartTimes.Remove(ActionId);

        // Remaining actions after this completion
        const int32 Remaining = GetPendingActionCount(TurnId);

        UE_LOG(LogTurnBarrier, Verbose,
            TEXT("[Barrier] COMPLETE: Turn=%d Actor=%s Action=%s (Remaining=%d)"),
            TurnId, *GetNameSafe(Actor), *ActionId.ToString(), Remaining);

        // If we reached zero, notify listeners on the next tick to prevent recursion
        if (Remaining == 0)
        {
            UE_LOG(LogTurnBarrier, Warning,
                TEXT("[Barrier] Turn %d: ALL ACTIONS COMPLETED (Remaining=0) -> Scheduling OnAllMovesFinished (NextTick)"),
                TurnId);

            if (UWorld* World = GetWorld())
            {
                World->GetTimerManager().SetTimerForNextTick([this, TurnId]()
                {
                    // Verify that the turn is still valid and we haven't moved past it (though unlikely in one tick)
                    // Actually, we just want to signal that *that* turn finished.
                    UE_LOG(LogTurnBarrier, Log, TEXT("[Barrier] Broadcasting OnAllMovesFinished for Turn %d"), TurnId);
                    OnAllMovesFinished.Broadcast(TurnId);
                });
            }
        }
    }
    else
    {
        // Duplicate completion is silently ignored (verbose only)
        if (bEnableVerboseLogging)
        {
            UE_LOG(LogTurnBarrier, Verbose,
                TEXT("[Barrier] Complete(Duplicate): Turn=%d Actor=%s Action=%s"),
                TurnId, *GetNameSafe(Actor), *ActionId.ToString());
        }
    }
}

// ============================================================================
// Public API: IsQuiescent
// ============================================================================

bool UTurnActionBarrierSubsystem::IsQuiescent(int32 TurnId) const
{
    const FTurnState* State = TurnStates.Find(TurnId);
    if (!State)
    {
        // No state for this turn means nothing is pending
        return true;
    }

    int32 ValidPendingCount = 0;
    for (const auto& Pair : State->PendingActions)
    {
        if (Pair.Key.IsValid() && Pair.Value.Num() > 0)
        {
            ++ValidPendingCount;
        }
    }

    return ValidPendingCount == 0;
}

// ============================================================================
// Public API: GetPendingActionCount
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
// Idempotent API: RegisterActionOnce
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
            Owner ? *GetNameSafe(Owner) : TEXT("null"));
        return;
    }

    ActiveTokens.Add(OutToken);
    TokenOwners.Add(OutToken, Owner);

    UE_LOG(LogTurnBarrier, Verbose,
        TEXT("[RegisterActionOnce] token=%s owner=%s"),
        *OutToken.ToString(),
        Owner ? *GetNameSafe(Owner) : TEXT("null"));
}

// ============================================================================
// Idempotent API: CompleteActionToken
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
            TEXT("[CompleteActionToken] unknown token=%s"),
            *Token.ToString());
        return;
    }

    TokenOwners.Remove(Token);
    UE_LOG(LogTurnBarrier, Verbose,
        TEXT("[CompleteActionToken] token=%s completed"), *Token.ToString());
}

// ============================================================================
// Timeout monitoring (timer-based; Tick removed)
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
// CheckTimeouts: detect and force-complete timed-out actions
// ============================================================================

void UTurnActionBarrierSubsystem::CheckTimeouts()
{
    const double Now = FPlatformTime::Seconds();

    // Check all active turns
    for (auto& TurnPair : TurnStates)
    {
        const int32 TurnId = TurnPair.Key;
        FTurnState& State = TurnPair.Value;

        if (State.TurnStartTime <= 0.0)
        {
            continue;
        }

        const double Elapsed = Now - State.TurnStartTime;
        if (Elapsed < ActionTimeoutSeconds)
        {
            continue; // Turn is not timed out yet
        }

        // Collect all actions that have exceeded the timeout
        TArray<TWeakObjectPtr<AActor>> TimeoutActors;
        TArray<FGuid> TimeoutActions;

        for (const auto& ActorPair : State.PendingActions)
        {
            if (!ActorPair.Key.IsValid() || ActorPair.Value.Num() == 0)
            {
                continue;
            }

            AActor* Actor = ActorPair.Key.Get();

            for (const FGuid& ActionId : ActorPair.Value)
            {
                double* StartTime = State.ActionStartTimes.Find(ActionId);
                if (!StartTime)
                {
                    continue;
                }

                const double ActionElapsed = Now - *StartTime;
                if (ActionElapsed >= ActionTimeoutSeconds)
                {
                    TimeoutActors.Add(ActorPair.Key);
                    TimeoutActions.Add(ActionId);

                    UE_LOG(LogTurnBarrier, Error,
                        TEXT("[Barrier] Timeout: Turn=%d Actor=%s Action=%s Elapsed=%.2fs"),
                        TurnId, *GetNameSafe(Actor), *ActionId.ToString(), ActionElapsed);
                }
            }
        }

        // Process all timed-out actions
        for (int32 i = 0; i < TimeoutActors.Num(); ++i)
        {
            TWeakObjectPtr<AActor> ActorPtr = TimeoutActors[i];
            const FGuid ActionId = TimeoutActions[i];

            if (!ActorPtr.IsValid())
            {
                continue;
            }

            AActor* Actor = ActorPtr.Get();

            // Optionally cancel active abilities on timeout
            if (bCancelAbilitiesOnTimeout)
            {
                if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor))
                {
                    UE_LOG(LogTurnBarrier, Warning,
                        TEXT("[Barrier] Cancelling abilities due to timeout: Actor=%s"),
                        *GetNameSafe(Actor));

                    ASC->CancelAbilities();
                }
            }

            // Force-complete the timed-out action
            CompleteAction(Actor, TurnId, ActionId);
        }
    }
}

// ============================================================================
// RemoveOldTurns: prune old turn data
// ============================================================================

void UTurnActionBarrierSubsystem::RemoveOldTurns(int32 CurrentTurn)
{
    TArray<int32> KeysToRemove;
    for (const auto& Pair : TurnStates)
    {
        // Remove turns older than CurrentTurn - 1
        if (Pair.Key < CurrentTurn - 1)
        {
            KeysToRemove.Add(Pair.Key);
        }
    }

    for (int32 Key : KeysToRemove)
    {
        const int32 RemainingActions = GetPendingActionCount(Key);
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
// Debug API: DumpTurnState
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
            *GetNameSafe(Actor), ActorPair.Value.Num());

        for (const FGuid& ActionId : ActorPair.Value)
        {
            const double* StartTime = State->ActionStartTimes.Find(ActionId);
            const double Elapsed = StartTime ? (FPlatformTime::Seconds() - *StartTime) : 0.0;

            UE_LOG(LogTurnBarrier, Log,
                TEXT("    - Action: %s (Elapsed: %.2fs)"),
                *ActionId.ToString(), Elapsed);
        }
    }

    UE_LOG(LogTurnBarrier, Log,
        TEXT("[Barrier] ===== End Turn %d State ====="), TurnId);
}

// ============================================================================
// Legacy API (Phase 1 compatibility - unit-count–based barrier)
// ============================================================================

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

// ============================================================================
// Internal helper: delegate firing for legacy API
// ============================================================================

void UTurnActionBarrierSubsystem::FireAllFinished(int32 TurnId)
{
    if (!IsServer())
    {
        return;
    }

    UE_LOG(LogTurnBarrier, Log,
        TEXT("Turn %d: FireAllFinished - Broadcasting OnAllMovesFinished"),
        TurnId);

    // Blueprint-facing delegate
    OnAllMovesFinished.Broadcast(TurnId);

    // Clear safety timeout timer
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(SafetyTimeoutHandle);
    }

    // Reset per-turn notification set
    NotifiedActorsThisTurn.Empty();
}
