# 修正計画書: ソフトロックと同時移動失敗のバグ修正

## 1. 概要

現在確認されている以下の2つの重大なバグを修正する。
1.  **[Bug 1] ソフトロック:** `Sequential mode` において、攻撃フェーズ完了後に敵の移動フェーズがスキップされ、ゲームが進行不能になる。
2.  **[Bug 2] 同時移動失敗:** `Simultaneous move mode` において、解決されたアクションがディスパッチされず、全ユニットが移動しないままターンが終了する。

## 2. 原因仮説と修正方針

### Task 1: ソフトロック (Bug 1)

*   **原因:** `GameTurnManagerBase::HandleAttackPhaseCompleted` 関数が、状態フラグ `bSequentialMovePhaseStarted` をチェックしている。このフラグは、それ以前のプレイヤー移動フェーズ完了時に `true` に設定されており、リセットされないまま攻撃フェーズ完了時にも `true` のため、後続の敵移動フェーズが「既に開始済み」と誤認され、スキップされてしまっていた。
*   **修正方針:** `HandleAttackPhaseCompleted` 内の `if (bSequentialMovePhaseStarted)` のチェックを削除する。攻撃フェーズ完了のコールバックであり、後続の移動フェーズは常にここから開始されるべきであるため、このチェックは不要かつ有害。

### Task 2: 同時移動失敗 (Bug 2)

*   **原因:** `GameTurnManagerBase::ExecuteSimultaneousPhase` 関数が、`CoreResolvePhase` で全ユニットの行動を解決した後、その結果（`ResolvedActions`）をディスパッチする処理が完全に欠落している。
*   **修正方針:** `ExecuteSimultaneousPhase` 関数内の `CoreResolvePhase` 呼び出しの後に、返された `ResolvedActions` をループ処理し、各アクションを `DispatchResolvedMove` を使してディスパッチするコードブロックを追加する。

## 3. 実装詳細 (コード差分)

### `GameTurnManagerBase.cpp`

#### Task 1: ソフトロック修正
```diff
--- a/Source/LyraGame/Rogue/Turn/GameTurnManagerBase.cpp
+++ b/Source/LyraGame/Rogue/Turn/GameTurnManagerBase.cpp
@@ -1883,13 +1883,8 @@
 
     if (bSequentialModeActive)
     {
-        if (bSequentialMovePhaseStarted)
-        {
-            UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] HandleAttackPhaseCompleted: Move-only phase already active"), FinishedTurnId);
-            return;
-        }
-
-        bSequentialMovePhaseStarted = true;
+        // 攻撃フェーズ完了時は、常に後続の移動フェーズを開始する。
+        // bSequentialMovePhaseStarted のチェックは不要かつ、誤った状態遷移の原因となるため削除。
         UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] Sequential attack phase complete, dispatching move-only phase..."), FinishedTurnId);
         ExecuteEnemyMoves_Sequential();
     }

```

#### Task 2: 同時移動失敗修正
```diff
--- a/Source/LyraGame/Rogue/Turn/GameTurnManagerBase.cpp
+++ b/Source/LyraGame/Rogue/Turn/GameTurnManagerBase.cpp
@@ -1798,6 +1798,15 @@
     TArray<FResolvedAction> ResolvedActions = PhaseManager->CoreResolvePhase(AllIntents);
     UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] CoreResolvePhase produced %d actions for simultaneous move"), CurrentTurnId, ResolvedActions.Num());
 
+    // 修正点: 解決されたアクションをディスパッチするロジックが欠落していた
+    // これにより、全ユニットが移動しない問題が発生していた
+    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] Dispatching %d resolved actions for simultaneous execution..."), CurrentTurnId, ResolvedActions.Num());
+    for (const FResolvedAction& Action : ResolvedActions)
+    {
+        // DispatchResolvedMove は内部でアビリティをトリガーし、
+        // アビリティが自身をTurnBarrierに登録するため、これでバリアが正しく機能するようになる
+        DispatchResolvedMove(Action);
+    }
+
     if (UWorld* World = GetWorld())
     {
         if (UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>())
```
