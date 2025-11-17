# 修正計画書: AI代替パス探索における移動優先度の調整

## 1. 概要

敵AIがブロックされた際の代替パス探索ロジックを改善し、斜め移動よりも直線的（上下左右）な移動を優先させることで、より自然な挙動を実現する。

## 2. 原因と修正方針

*   **現状:** 移動検証に失敗した場合、隣接する8方向（上下左右＋斜め）を等しく評価し、プレイヤーに最も近づけるセルを代替移動先として選択している。これにより、AIが不自然に見える斜め移動を選択する場合がある。
*   **修正方針:** 代替パスの探索を2段階に分割する。
    1.  まず、上下左右の4方向を探索し、有効な移動先（移動可能かつプレイヤーに最も近づける）があれば、その時点で探索を終了し、そのセルを移動先とする。
    2.  上下左右に有効な移動先がなかった場合に限り、次に斜め4方向を探索し、最適な移動先を選択する。

## 3. 実装詳細 (コード差分)

`EnemyThinkerBase.cpp` の `ComputeIntent_Implementation` 内、`!bMoveValid` ブロックの代替パス探索部分を以下のように変更する。

```diff
--- a/Source/LyraGame/Rogue/AI/Enemy/EnemyThinkerBase.cpp
+++ b/Source/LyraGame/Rogue/AI/Enemy/EnemyThinkerBase.cpp
@@ -334,28 +334,47 @@
                         Intent.CurrentCell.X, Intent.CurrentCell.Y,
                         Intent.NextCell.X, Intent.NextCell.Y);
                     
-                    // 代替経路探索ロジック
-                    const FIntPoint NeighborOffsets[] = {
-                        {1, 0}, {-1, 0}, {0, 1}, {0, -1},
-                        {1, 1}, {1, -1}, {-1, 1}, {-1, -1}
-                    };
+                    // --- 代替経路探索ロジック (2段階評価に修正) ---
                     FIntPoint BestAlternativeCell = Intent.CurrentCell;
                     int32 BestAlternativeDist = DistanceToPlayer;
 
-                    for (const FIntPoint& Offset : NeighborOffsets)
+                    // 第1優先: 上下左右
+                    const FIntPoint OrthoOffsets[] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
+                    for (const FIntPoint& Offset : OrthoOffsets)
                     {
                         FIntPoint AlternativeCell = Intent.CurrentCell + Offset;
                         FString DummyReason;
                         if (PathFinder->IsMoveValid(Intent.CurrentCell, AlternativeCell, GetOwner(), DummyReason))
                         {
                             int32 AlternativeDist = FGridUtils::ChebyshevDistance(AlternativeCell, PlayerGridCell);
-                            if (AlternativeDist < BestAlternativeDist)
+                            if (AlternativeDist < BestAlternativeDist)
                             {
                                 BestAlternativeDist = AlternativeDist;
                                 BestAlternativeCell = AlternativeCell;
                             }
                         }
+                    }
+
+                    // 第2優先: 斜め (上下左右に有効な手がない場合のみ)
+                    if (BestAlternativeCell == Intent.CurrentCell)
+                    {
+                        const FIntPoint DiagOffsets[] = { {1, 1}, {1, -1}, {-1, 1}, {-1, -1} };
+                        for (const FIntPoint& Offset : DiagOffsets)
+                        {
+                            FIntPoint AlternativeCell = Intent.CurrentCell + Offset;
+                            FString DummyReason;
+                            if (PathFinder->IsMoveValid(Intent.CurrentCell, AlternativeCell, GetOwner(), DummyReason))
+                            {
+                                int32 AlternativeDist = FGridUtils::ChebyshevDistance(AlternativeCell, PlayerGridCell);
+                                if (AlternativeDist < BestAlternativeDist)
+                                {
+                                    BestAlternativeDist = AlternativeDist;
+                                    BestAlternativeCell = AlternativeCell;
+                                }
+                            }
+                        }
                     }
 
                     if (BestAlternativeCell != Intent.CurrentCell)
```
