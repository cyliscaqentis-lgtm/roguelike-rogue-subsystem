# 修正計画書: AIの不合理な移動選択の修正

## 1. 概要

敵AIが、プレイヤーに接近しようとしているにも関わらず、プレイヤーから遠ざかるセルへ移動することがある問題を修正する。

## 2. 原因

`EnemyThinkerBase::ComputeIntent_Implementation` が、移動先の提案を `DistanceFieldSubsystem::GetNextStepTowardsPlayer` 関数に依存している。この関数が、稀にプレイヤーから遠ざかるセルを「最適な次の一手」として返すことがある、という潜在的なバグが存在する。
`EnemyThinker` は現在、この提案を検証せずに受け入れているため、不合理な移動が発生している。

## 3. 修正方針

`EnemyThinker` 側に、`GetNextStepTowardsPlayer` から受け取った提案を検証するロジックを追加し、不適切な移動を拒否できるようにする。

- **対象ファイル:** `Source/LyraGame/Rogue/AI/Enemy/EnemyThinkerBase.cpp`
- **対象関数:** `UEnemyThinkerBase::ComputeIntent_Implementation`
- **修正内容:**
  1. `GetNextStepTowardsPlayer` で `NextCell` を取得した直後、現在地からプレイヤーへの距離 (`CurrentDist`) と、`NextCell` からプレイヤーへの距離 (`ProposedDist`) を計算する。
  2. `ProposedDist >= CurrentDist` となる場合（移動しても近づかない、または遠ざかる場合）、その提案は不適切であると判断する。
  3. 不適切な提案だった場合は、`NextCell` を `CurrentCell` に強制的に戻す。
  4. これにより、後続の `if (Intent.NextCell != Intent.CurrentCell)` の条件が偽となり、既存の「代替パス探索ロジック」が起動する。結果として、隣接セルの中からより適切な移動先が選択される。

## 4. 実装詳細 (コード差分)

`EnemyThinkerBase.cpp` の `ComputeIntent_Implementation` 内、`GetNextStepTowardsPlayer` 呼び出し後を以下のように変更する。

```diff
--- a/Source/LyraGame/Rogue/AI/Enemy/EnemyThinkerBase.cpp
+++ b/Source/LyraGame/Rogue/AI/Enemy/EnemyThinkerBase.cpp
@@ -303,6 +303,22 @@
         if (UDistanceFieldSubsystem* DistanceField = World->GetSubsystem<UDistanceFieldSubsystem>())
         {
             Intent.NextCell = DistanceField->GetNextStepTowardsPlayer(Intent.CurrentCell, GetOwner());
+
+            // ★★★ 新しい検証ロジック ★★★
+            // GetNextStepTowardsPlayerが、プレイヤーから遠ざかるような不適切な手を返さないか検証する
+            const int32 CurrentDist = FGridUtils::ChebyshevDistance(CurrentEnemyCell, PlayerGridCell);
+            const int32 ProposedDist = FGridUtils::ChebyshevDistance(Intent.NextCell, PlayerGridCell);
+
+            if (ProposedDist >= CurrentDist && Intent.NextCell != CurrentEnemyCell)
+            {
+                UE_LOG(LogTurnManager, Warning,
+                    TEXT("[ComputeIntent] %s: GetNextStepTowardsPlayer suggested a non-improving move from (%d,%d) to (%d,%d). Dist %d -> %d. Rejecting and finding alternative."),
+                    *GetNameSafe(GetOwner()), CurrentEnemyCell.X, CurrentEnemyCell.Y, Intent.NextCell.X, Intent.NextCell.Y, CurrentDist, ProposedDist);
+
+                // 提案を拒否し、後続の代替パス探索ロジックに処理を移譲する
+                Intent.NextCell = CurrentEnemyCell;
+            }
         }
         else
         {
```
