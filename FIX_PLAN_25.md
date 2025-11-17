# 修正計画書: 逐次モードにおけるユニット重なりバグの修正

## 1. 概要

ターン制ゲームの逐次モード（Sequential mode）において、移動ユニットが攻撃ユニットの占有マスに移動しようとして重なってしまうバグを修正する。

## 2. 原因

1.  **`GameTurnManagerBase::bHasAttack`フラグの不整合:**
    `GameTurnManagerBase`内で、`ATTACK`インテントが存在するにも関わらず、`bHasAttack`フラグが`FALSE`と誤って評価され、`Simultaneous movement`（同時移動モード）が発動してしまう場合がある。これにより、本来逐次処理されるべきターンが同時処理され、予期せぬ衝突が発生する。
2.  **`CoreResolvePhase`（競合解決フェーズ）のロジック不足:**
    `Sequential mode`が正しく発動した場合でも、`CoreResolvePhase`を担う`ConflictResolverSubsystem`が、`ATTACK`インテントを持つユニットがそのマスに留まることを考慮していない。結果として、移動ユニットが攻撃ユニットの現在地を目的地として予約してしまい、衝突が発生する。

## 3. 修正方針

`ConflictResolverSubsystem`を改修し、`Sequential mode`における移動解決ロジックを強化する。また、`GameTurnManagerBase`の`bHasAttack`フラグの計算ロジックを検証する。

- **対象ファイル:**
    - `Source/LyraGame/Rogue/Turn/ConflictResolverSubsystem.cpp`
    - `Source/LyraGame/Rogue/Turn/GameTurnManagerBase.cpp`

- **修正内容:**

### Task 1: `ConflictResolverSubsystem`の改修

`ConflictResolverSubsystem::ResolveActions`関数内で、以下の処理を追加する。

1.  **攻撃ユニットの特定とマス占有:**
    - `Sequential mode`が有効な場合（`bHasAttack`フラグが`true`の場合）、入力されたすべてのアクション（`FProposedAction`）を走査し、`ATTACK`インテントを持つユニットを特定する。
    - これらの攻撃ユニットが現在占有しているマス（`CurrentCell`）を、`GridOccupancySubsystem`などを利用して一時的に「移動不可」または「占有済み」としてマークする。これにより、移動ユニットがこれらのマスを目的地として予約できないようにする。
    - この「占有済み」情報は、移動ユニットの目的地解決ロジックに影響を与えるようにする。

2.  **移動ユニットの目的地再評価:**
    - 移動ユニットの目的地を解決する際、上記でマークされたマスを避けるようにする。もし移動ユニットの提案された目的地が攻撃ユニットのマスと衝突する場合、その移動ユニットは代替パスを探すか、`WAIT`を選択するように強制される。

## 4. 実装詳細 (コード差分)

### `ConflictResolverSubsystem.cpp` の `ResolveActions` 関数

```diff
--- a/Source/LyraGame/Rogue/Turn/ConflictResolverSubsystem.cpp
+++ b/Source/LyraGame/Rogue/Turn/ConflictResolverSubsystem.cpp
@@ -XX,6 +XX,7 @@
 #include "Turn/ConflictResolverSubsystem.h"
 #include "Grid/GridOccupancySubsystem.h"
 #include "Turn/TurnSystemTypes.h"
+#include "GameTurnManagerBase.h" // GameTurnManagerBaseからbHasAttackフラグを取得するため

 // ... (既存のコード) ...

 FResolvedActionList UConflictResolverSubsystem::ResolveActions(const TArray<FProposedAction>& ProposedActions, int32 CurrentTurnId)
 {
     FResolvedActionList ResolvedActions;
     UGridOccupancySubsystem* OccupancySubsystem = GetWorld()->GetSubsystem<UGridOccupancySubsystem>();
+    UGameTurnManagerBase* TurnManager = GetWorld()->GetSubsystem<UGameTurnManagerBase>(); // TurnManagerを取得

     if (!OccupancySubsystem)
     {
         UE_LOG(LogTemp, Error, TEXT("ConflictResolverSubsystem: GridOccupancySubsystem not found!"));
         return ResolvedActions;
     }

+    // Sequential modeの場合、攻撃ユニットの現在地を一時的にブロックする
+    TSet<FIntPoint> AttackingUnitPositions;
+    if (TurnManager && TurnManager->HasAttack()) // bHasAttackフラグを使用
+    {
+        for (const FProposedAction& ProposedAction : ProposedActions)
+        {
+            if (ProposedAction.ActionTag.MatchesTag(FGameplayTag::RequestGameplayTag(TEXT("AI.Intent.Attack"))))
+            {
+                AttackingUnitPositions.Add(ProposedAction.CurrentCell);
+                UE_LOG(LogTemp, Log, TEXT("ConflictResolver: Attacking unit at (%d,%d) will block its cell."), ProposedAction.CurrentCell.X, ProposedAction.CurrentCell.Y);
+            }
+        }
+    }
+
     // Step 1: Process all actions and attempt to reserve destinations
     for (const FProposedAction& ProposedAction : ProposedActions)
     {
         FResolvedAction ResolvedAction = FResolvedAction(ProposedAction);
         ResolvedAction.Status = EActionResolutionStatus::Unresolved;

         // 攻撃ユニットのマスを移動ユニットが予約しないようにする
+        if (AttackingUnitPositions.Contains(ProposedAction.DestinationCell) &&
+            ProposedAction.ActionTag.MatchesTag(FGameplayTag::RequestGameplayTag(TEXT("AI.Intent.Move"))))
+        {
+            ResolvedAction.Status = EActionResolutionStatus::Rejected_Occupied;
+            ResolvedAction.DestinationCell = ProposedAction.CurrentCell; // 移動を拒否し、現在地に留まる
+            UE_LOG(LogTemp, Warning, TEXT("ConflictResolver: Move to (%d,%d) rejected for %s due to attacking unit."),
+                ProposedAction.DestinationCell.X, ProposedAction.DestinationCell.Y, *GetNameSafe(ProposedAction.Actor));
+        }
+        else if (OccupancySubsystem->ReserveCell(ProposedAction.DestinationCell, ProposedAction.Actor, CurrentTurnId))
+        {
+            ResolvedAction.Status = EActionResolutionStatus::Success;
+            // ... (既存の成功処理) ...
+        }
+        else
+        {
+            ResolvedAction.Status = EActionResolutionStatus::Rejected_Occupied;
+            ResolvedAction.DestinationCell = ProposedAction.CurrentCell; // 予約失敗時は現在地に留まる
+            UE_LOG(LogTemp, Warning, TEXT("ConflictResolver: ReserveCell failed for %s to (%d,%d). Staying put."),
+                *GetNameSafe(ProposedAction.Actor), ProposedAction.DestinationCell.X, ProposedAction.DestinationCell.Y);
+        }
+
         // ... (既存の予約処理と衝突解決ロジック) ...
     }

     // ... (残りの衝突解決ロジック) ...

     return ResolvedActions;
 }
```

### `GameTurnManagerBase.cpp` の `RegenerateEnemyIntents` 関数

`bHasAttack`フラグの計算ロジックを検証し、必要であれば修正する。

```diff
--- a/Source/LyraGame/Rogue/Turn/GameTurnManagerBase.cpp
+++ b/Source/LyraGame/Rogue/Turn/GameTurnManagerBase.cpp
@@ -200,6 +200,14 @@
     CachedEnemyIntents = EnemyAISubsystem->CollectIntents(Observations);
     EnemyTurnDataSubsystem->SaveIntents(CachedEnemyIntents, CurrentTurnId);
 
+    // bHasAttackフラグを再評価
+    bHasAttack = false;
+    for (const FEnemyIntent& Intent : CachedEnemyIntents)
+    {
+        if (Intent.AbilityTag.MatchesTag(GameplayTags.AI_Intent_Attack))
+        {
+            bHasAttack = true;
+            break;
+        }
+    }
+
     UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] Intents regenerated: %d intents (with player destination)"), CurrentTurnId, CachedEnemyIntents.Num());
 }
```
