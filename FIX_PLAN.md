## 0. INCIDENT_ID: RECURSION_HANG (Log Analysis - Round 7)

## 1. Summary of Incident & Update

The previous fix (Version 6) added a "Stale Turn Check" to `GA_AttackBase`. However, the logs showed that the check did *not* trigger, yet the "Completed attack in Barrier" log appeared *after* Turn 1 started.

**Analysis of the logs revealed a critical sequence:**
1.  `GA_AttackBase::EndAbility` (Turn 0) calls `Barrier->CompleteAction`.
2.  `Barrier->CompleteAction` sees `Remaining=0` and **immediately** broadcasts `OnAllMovesFinished`.
3.  `GameTurnManager` responds to the broadcast and calls `AdvanceTurnAndRestart`.
4.  `AdvanceTurnAndRestart` starts Turn 1 (`Barrier->BeginTurn(1)`).
5.  **Recursion Unwinds**: `Barrier->CompleteAction` returns, and `GA_AttackBase::EndAbility` resumes.
6.  `GA_AttackBase` logs "Completed attack in Barrier".

**Result:**
-   Turn 1 starts *inside* the execution stack of Turn 0's `EndAbility`.
-   The "Stale Turn Check" failed because at the moment it ran, the turn was still 0.
-   The "Late Completion" log appeared in Turn 1 because it printed after the recursive call returned.
-   **The Hang**: Starting a new turn (and opening input windows) while deep in the stack of the previous turn's ability cleanup is highly dangerous and likely caused the input system or GAS to enter an invalid state.

## 2. Identified Root Cause

**Synchronous Turn Transition Recursion.**
The `UTurnActionBarrierSubsystem` broadcasts `OnAllMovesFinished` synchronously within `CompleteAction`. This causes the entire next turn's initialization logic to run nested within the previous turn's completion logic.

## 3. Proposed Solution (Fix Version 7)

**Decouple the Turn Transition.**
We will modify `UTurnActionBarrierSubsystem::CompleteAction` to **schedule the broadcast for the next tick** instead of firing immediately.

**Benefits:**
1.  **Breaks Recursion**: `EndAbility` will finish completely before Turn 1 starts.
2.  **Clean State**: The previous turn's stack will unwind, ensuring all tags and states are finalized.
3.  **Correct Logging**: The logs will appear in the natural order (Completion -> Turn Start).
4.  **Safety**: Prevents "Zombie" abilities from affecting the new turn, as they will have finished execution before the new turn begins.

## 4. Fix Roadmap

1.  **Modify `UTurnActionBarrierSubsystem::CompleteAction`**:
    -   When `Remaining == 0`, use `GetWorld()->GetTimerManager().SetTimerForNextTick(...)` to broadcast `OnAllMovesFinished`.

2.  **Keep `GA_AttackBase` Fix**:
    -   The "Stale Turn Check" from Version 6 is still a good safety net for *actual* zombie abilities (e.g., network lag), so we will keep it.

## 5. Diff-level Correction Details

**File: `Source/LyraGame/Rogue/Turn/TurnActionBarrierSubsystem.cpp`**

```cpp
void UTurnActionBarrierSubsystem::CompleteAction(...)
{
    // ... (Removal logic) ...

    if (Remaining == 0)
    {
        UE_LOG(LogTurnBarrier, Warning,
            TEXT("[Barrier] Turn %d: ALL ACTIONS COMPLETED (Remaining=0) -> Scheduling OnAllMovesFinished (NextTick)"),
            TurnId);

        // NEW FIX: Broadcast on next tick to prevent recursion
        if (UWorld* World = GetWorld())
        {
            World->GetTimerManager().SetTimerForNextTick([this, TurnId]()
            {
                UE_LOG(LogTurnBarrier, Log, TEXT("[Barrier] Broadcasting OnAllMovesFinished for Turn %d"), TurnId);
                OnAllMovesFinished.Broadcast(TurnId);
            });
        }
    }
}
```

## 6. Test Policy

*   **Recompile and Run**.
*   **Observe Logs**:
    *   Verify that "Completed attack in Barrier" appears *before* "Barrier::BeginTurn(1)".
    *   Verify that the game no longer hangs after Turn 1.
