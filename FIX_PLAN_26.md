# 修正計画書: `CoreResolvePhase`のアクション消失バグに対するフォールバック実装

## 1. 概要

`Simultaneous Move Phase`（同時移動フェーズ）において、競合解決を担当する`CoreResolvePhase`が、稀に特定のユニットのアクションを解決できずに消失させてしまう問題を修正する。これにより、ユニットがインテントを持っていたにも関わらず、ターン中に何も行動しない、という状況が発生している。

## 2. 原因

`CoreResolvePhase`（および内部で呼び出される`ConflictResolverSubsystem`）に、複雑な移動経路が交差した場合に解決に失敗し、一部のアクションを返り値のリストに含めない、という潜在的なバグが存在する。
呼び出し元である`GameTurnManagerBase`は、現在この失敗を検知するすべがなく、不完全なアクションリストを基にディスパッチを行っているため、一部のユニットがターンから脱落している。

## 3. 修正方針

`CoreResolvePhase`のバグを直接修正するのではなく、`GameTurnManagerBase`側でこの失敗を検知し、安全に処理を続行するためのフォールバック機構を導入する。

- **対象ファイル:** `Source/LyraGame/Rogue/Turn/GameTurnManagerBase.cpp`
- **対象関数:** `ExecuteSimultaneousPhase`

- **修正内容:**
  1.  **失敗検知:** `CoreResolvePhase`を呼び出した後、インプアウトとして渡したアクションの数と、アウトプットとして受け取った解決済みアクションの数を比較する。
  2.  **フォールバック処理:**
      - もし数が一致しない場合、`CoreResolvePhase`が失敗したとみなし、その結果（`ResolvedActions`）を破棄する。
      - 代わりに、元のインテントリスト（`AllIntents`）を基に、ユニットを1体ずつ順番にディスパッチする、より安全な「逐次ディスパッチ」処理に切り替える。
      - この逐次処理では、各ユニットをディスパッチする直前に、`GridOccupancySubsystem`を用いて目的地の通行可能性を再チェックする。これにより、最低限の衝突回避を保証する。
  3.  **正常処理:**
      - もし数が一致した場合は、これまで通り`ResolvedActions`を基に全ユニットの移動をディスパッチする。

## 4. 実装詳細 (コード差分)

`GameTurnManagerBase.cpp` の `ExecuteSimultaneousPhase` 関数を以下のように変更する。

```diff
--- a/Source/LyraGame/Rogue/Turn/GameTurnManagerBase.cpp
+++ b/Source/LyraGame/Rogue/Turn/GameTurnManagerBase.cpp
@@ -XXX,XX +XXX,XX @@
 void UGameTurnManagerBase::ExecuteSimultaneousPhase(TArray<FProposedAction>& AllIntents)
 {
     // ... (既存のコード) ...
-
     const int32 IntentCount = AllIntents.Num();
     FResolvedActionList ResolvedActions = TurnCorePhaseManager->CoreResolvePhase(AllIntents, CurrentTurnId);
+    const int32 ResolvedCount = ResolvedActions.Num();
 
-    // ... (既存のディスパッチループ) ...
+    if (IntentCount != ResolvedCount)
+    {
+        UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] CoreResolvePhase FAILED! IntentCount=%d, ResolvedCount=%d. Switching to failsafe sequential dispatch."), CurrentTurnId, IntentCount, ResolvedCount);
+
+        // FAILSAFE SEQUENTIAL DISPATCH
+        UGridOccupancySubsystem* GridOccupancySubsystem = GetWorld()->GetSubsystem<UGridOccupancySubsystem>();
+
+        // Player move is handled separately
+        FProposedAction PlayerAction;
+        bool bPlayerActionFound = false;
+        for (const FProposedAction& Intent : AllIntents)
+        {
+            if (Intent.Actor == GetPlayerPawn())
+            {
+                PlayerAction = Intent;
+                bPlayerActionFound = true;
+                break;
+            }
+        }
+
+        // Dispatch non-player moves first
+        for (const FProposedAction& Intent : AllIntents)
+        {
+            if (Intent.Actor == GetPlayerPawn()) continue;
+
+            if (GridOccupancySubsystem && !GridOccupancySubsystem->IsCellOccupied(Intent.DestinationCell))
+            {
+                GridOccupancySubsystem->ReserveCell(Intent.DestinationCell, Intent.Actor, CurrentTurnId);
+                DispatchResolvedMove(Intent.Actor, Intent.DestinationCell);
+            }
+            else
+            {
+                UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] Failsafe: %s is waiting, destination (%d,%d) is blocked."), CurrentTurnId, *GetNameSafe(Intent.Actor), Intent.DestinationCell.X, Intent.DestinationCell.Y);
+            }
+        }
+
+        // Dispatch player move
+        if (bPlayerActionFound)
+        {
+            TriggerPlayerMove(PlayerAction.DestinationCell, PlayerAction.Magnitude);
+        }
+    }
+    else
+    {
+        // NORMAL SIMULTANEOUS DISPATCH
+        UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] CoreResolvePhase SUCCESS. Dispatching %d resolved actions."), CurrentTurnId, ResolvedCount);
+        for (const FResolvedAction& ResolvedAction : ResolvedActions)
+        {
+            if (ResolvedAction.Actor == GetPlayerPawn())
+            {
+                TriggerPlayerMove(ResolvedAction.DestinationCell, ResolvedAction.Magnitude);
+            }
+            else
+            {
+                DispatchResolvedMove(ResolvedAction.Actor, ResolvedAction.DestinationCell);
+            }
+        }
+    }
 }
```
