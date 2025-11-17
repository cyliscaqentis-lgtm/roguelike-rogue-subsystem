# 修正計画書: AIの交通渋滞問題の解決

## 1. 概要

敵AIが移動先セルの占有状況を考慮できず、移動を諦めて待機 (`WAIT`) してしまい、結果として大半のユニットが動かなくなる「交通渋滞」問題を修正する。

## 2. 原因仮説

`EnemyThinkerBase::ComputeIntent_Implementation` のロジックに問題がある。

1.  **単純な移動先決定:** AIは `DistanceField` に基づいてプレイヤーへの最短方向の1マスを次の移動先として決定する。この際、他のユニット（プレイヤーの移動元を含む）の占有・予約状況を考慮しない。
2.  **不適切なフォールバック:** その後、`PathFinder->IsMoveValid` で移動を検証し、失敗した場合（セルが占有されている場合など）に、代替経路を探索せず、即座に行動を `WAIT` に決定してしまう。

これにより、複数のAIが同じセルを目指したり、プレイヤーが離れた直後のセルに入ろうとしたりして連鎖的に移動に失敗し、集団で待機状態に陥っている。

## 3. 修正方針

`ComputeIntent_Implementation` を修正し、移動検証に失敗した場合のフォールバック処理を改善する。

- **対象ファイル:** `Source/LyraGame/Rogue/AI/Enemy/EnemyThinkerBase.cpp`
- **対象関数:** `UEnemyThinkerBase::ComputeIntent_Implementation`
- **修正内容:**
  - `IsMoveValid` が `false` を返した場合、即座に `WAIT` にするのではなく、代替移動先を探索するロジックを追加する。
  - 隣接する8マスを探索し、`IsMoveValid` が `true` を返すセルのうち、最もプレイヤーに近づけるセルを新たな `NextCell` として採用する。
  - どの隣接セルにも移動できない場合に限り、最終的に `WAIT` を選択する。

## 4. 実装詳細 (コード差分)

```diff
--- a/Source/LyraGame/Rogue/AI/Enemy/EnemyThinkerBase.cpp
+++ b/Source/LyraGame/Rogue/AI/Enemy/EnemyThinkerBase.cpp
@@ -328,11 +328,39 @@
                         *GetNameSafe(GetOwner()), *FailureReason,
                         Intent.CurrentCell.X, Intent.CurrentCell.Y,
                         Intent.NextCell.X, Intent.NextCell.Y);
-
-                    // Fallback to wait if move is invalid
-                    Intent.NextCell = Intent.CurrentCell;
-                    Intent.AbilityTag = FGameplayTag::RequestGameplayTag(TEXT("AI.Intent.Wait"));
+                    
+                    // 代替経路探索ロジック
+                    const FIntPoint NeighborOffsets[] = {
+                        {1, 0}, {-1, 0}, {0, 1}, {0, -1},
+                        {1, 1}, {1, -1}, {-1, 1}, {-1, -1}
+                    };
+                    FIntPoint BestAlternativeCell = Intent.CurrentCell;
+                    int32 BestAlternativeDist = DistanceToPlayer;
+
+                    for (const FIntPoint& Offset : NeighborOffsets)
+                    {
+                        FIntPoint AlternativeCell = Intent.CurrentCell + Offset;
+                        FString DummyReason;
+                        if (PathFinder->IsMoveValid(Intent.CurrentCell, AlternativeCell, GetOwner(), DummyReason))
+                        {
+                            int32 AlternativeDist = FGridUtils::ChebyshevDistance(AlternativeCell, PlayerGridCell);
+                            if (AlternativeDist < BestAlternativeDist)
+                            {
+                                BestAlternativeDist = AlternativeDist;
+                                BestAlternativeCell = AlternativeCell;
+                            }
+                        }
+                    }
+
+                    if (BestAlternativeCell != Intent.CurrentCell)
+                    {
+                        Intent.NextCell = BestAlternativeCell;
+                    }
+                    else
+                    {
+                        Intent.NextCell = Intent.CurrentCell;
+                        Intent.AbilityTag = FGameplayTag::RequestGameplayTag(TEXT("AI.Intent.Wait"));
+                    }
                 }
             }
         }
```
