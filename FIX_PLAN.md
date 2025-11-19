## 0. INCIDENT_ID: UNKNOWN (Log Analysis - Round 2)

## 1. Summary of Incident & Apology

大変申し訳ありません、前回の分析は不正確でした。ご指摘の通り、`ABORT`エラーは解消されたものの、ターンが進行しない新たな問題が発生していました。
(I sincerely apologize, my previous analysis was incorrect. As you pointed out, although the `ABORT` error was resolved, a new issue where the turn does not advance has occurred.)

The new issue is a silent hang in Turn 1. After Turn 0 completes and Turn 1 begins, the game stops processing player input. A careful re-analysis of the log reveals that the ability completion logic from Turn 0 is being executed a *second time* during Turn 1. This stale, duplicated event corrupts the state of the turn management system, causing the hang.

## 2. Identified Root Cause

The root cause is that `UGA_MeleeAttack::OnMontageCompleted` is being called twice for a single attack action. This leads to `OnAttackCompleted()` and subsequently `EndAbility()` being called twice. The first call correctly ends the ability and advances the turn from 0 to 1. The second, delayed call happens *during* Turn 1 but carries the context of Turn 0 (`TurnId=0`). This stale `EndAbility(TurnId=0)` call is sent to the `TurnActionBarrier`, which is now managing Turn 1, corrupting its state and preventing any further actions from being processed in Turn 1.

The reason why `UAbilityTask_PlayMontageAndWait`'s `OnCompleted` delegate fires twice is unclear, but it is the direct cause of the bug.

## 3. Current Logic Flow (Mermaid Diagram/Pseudo-code)

```mermaid
graph TD
    A[Turn 0: Attack Ability Activates] --> B{OnMontageCompleted fires};
    B --> C{Call OnAttackCompleted -> EndAbility(TurnId=0)};
    C --> D[Turn correctly advances to 1];
    D --> E[Turn 1 Starts];
    subgraph Turn 1
        E --> F[Input Gate for Turn 1 Opens];
        F --x G{OnMontageCompleted fires AGAIN for Turn 0's attack};
        G --> H{Call OnAttackCompleted -> EndAbility(TurnId=0) AGAIN};
        H --> I{Stale EndAbility(TurnId=0) corrupts Turn 1's Barrier Subsystem};
        I --x J[Game silently hangs, no input processed];
    end
```

**Inconsistency:** The `OnMontageCompleted` event should only fire once per ability execution. Its repeated firing is the core anomaly.

## 4. Fix Roadmap

1.  **Make `OnMontageCompleted` Idempotent:** The most direct and robust fix is to prevent the logic inside `OnMontageCompleted` from running more than once. This will be achieved by adding a boolean guard flag to the `UGA_MeleeAttack` ability. The flag will be set to `false` on `ActivateAbility` and checked/set to `true` upon the first entry into `OnMontageCompleted`. Subsequent calls will be ignored.

## 5. Diff-level Correction Details

**File: `Source/LyraGame/Rogue/Abilities/GA_MeleeAttack.h`**
```diff
--- a/Source/LyraGame/Rogue/Abilities/GA_MeleeAttack.h
+++ b/Source/LyraGame/Rogue/Abilities/GA_MeleeAttack.h
@@ -52,4 +52,7 @@
 
     UPROPERTY(Transient)
     TWeakObjectPtr<AGameTurnManagerBase> CachedTurnManager;
+
+    // Fix for double-triggering OnMontageCompleted
+    bool bMontageCompleted = false;
 };
```

**File: `Source/LyraGame/Rogue/Abilities/GA_MeleeAttack.cpp`**
```diff
--- a/Source/LyraGame/Rogue/Abilities/GA_MeleeAttack.cpp
+++ b/Source/LyraGame/Rogue/Abilities/GA_MeleeAttack.cpp
@@ -107,6 +107,8 @@
     const FGameplayAbilityActivationInfo ActivationInfo,
     const FGameplayEventData* TriggerEventData)
 {
+    bMontageCompleted = false; // Reset the guard flag on activation
+
     UE_LOG(LogTemp, Error,
         TEXT("[GA_MeleeAttack] ===== ActivateAbility CALLED ===== Actor=%s"),
         *GetNameSafe(GetAvatarActorFromActorInfo()));
@@ -213,6 +215,13 @@
 
 void UGA_MeleeAttack::OnMontageCompleted()
 {
+    if (bMontageCompleted)
+    {
+        UE_LOG(LogTemp, Warning, TEXT("[GA_MeleeAttack] OnMontageCompleted called more than once, ignoring."));
+        return;
+    }
+    bMontageCompleted = true;
+
     UE_LOG(LogTemp, Error,
         TEXT("[GA_MeleeAttack] ===== OnMontageCompleted CALLED ===== Actor=%s"),
         *GetNameSafe(GetAvatarActorFromActorInfo()));

```

## 6. Test Policy

*   Execute the game in PIE.
*   Perform a melee attack with the player unit.
*   Verify that the turn advances correctly from Turn 0 to Turn 1.
*   **Crucially, verify that you can perform another action (move or attack) in Turn 1.**
*   Check the logs for the new `OnMontageCompleted called more than once, ignoring.` warning. If this log appears, it confirms the guard is working. The ideal state is that this log *doesn't* appear, meaning the underlying double-fire issue is gone, but the guard protects against it regardless.
*   Confirm the game does not hang and subsequent turns can be played.