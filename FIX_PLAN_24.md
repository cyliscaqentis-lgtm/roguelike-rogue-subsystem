# 修正計画書: AI思考ロジックの簡素化によるデッドロック解消

## 1. 概要

敵AIが移動先を事前に検証しすぎた結果、他のユニットとの衝突を恐れて移動を諦めてしまい、結果的に動かなくなる問題を修正する。

## 2. 原因

`EnemyThinkerBase::ComputeIntent` 内に実装された「移動先の事前検証」および「代替パス探索」ロジックが過剰に働いている。
本来、各AIは「行きたい場所」を宣言するに留め、ユニット間の衝突回避などの交通整理は、すべてのインテントが揃った後に行われる中央の `CoreResolvePhase` が担当すべきである。
現状では、`CoreResolvePhase` が賢く交通整理を行う前に、個々のAIが悲観的な判断で `WAIT` を選択してしまい、`CoreResolvePhase` に解決の機会を与えていない。

## 3. 修正方針

`EnemyThinkerBase` から事前検証ロジックを削除し、AIの役割を「最適な移動先の提案のみ」に限定する。

- **対象ファイル:** `Source/LyraGame/Rogue/AI/Enemy/EnemyThinkerBase.cpp`
- **対象関数:** `UEnemyThinkerBase::ComputeIntent_Implementation`
- **修正内容:**
  - `PathFinder->IsMoveValid` の呼び出しと、それに関連する分岐をすべて削除する。
  - `GetNextStepTowardsPlayer` の結果を検証するロジックを削除する。
  - 代替パス（隣接8マス）を探索するロジックを削除する。
  - `DistanceField` から得られた `NextCell` を、検証せずにそのまま `MOVE` インテントとして返す。`NextCell` が現在位置と同じ場合にのみ `WAIT` を選択する、という最もシンプルなロジックに戻す。

## 4. 実装詳細 (コード差分)

`EnemyThinkerBase.cpp` の `ComputeIntent_Implementation` を以下のように、事前検証ロジックが追加される前のシンプルな状態に戻す。

```diff
--- a/Source/LyraGame/Rogue/AI/Enemy/EnemyThinkerBase.cpp
+++ b/Source/LyraGame/Rogue/AI/Enemy/EnemyThinkerBase.cpp
@@ -303,51 +303,12 @@
         {
             Intent.NextCell = DistanceField->GetNextStepTowardsPlayer(Intent.CurrentCell, GetOwner());
         }
         else
         {
             Intent.NextCell = Intent.CurrentCell;
-            UE_LOG(LogTemp, Warning,
-                TEXT("[ComputeIntent] %s: DistanceField not available, staying put"),
-                *GetNameSafe(GetOwner()));
+            UE_LOG(LogTemp, Warning, TEXT("[ComputeIntent] %s: DistanceField not available, staying put"), *GetNameSafe(GetOwner()));
         }
 
-        // ★★★ CodeRevision: INC-2025-00016-R1 (Add IsMoveValid validation) (2025-11-16 14:00) ★★★
-        // CodeRevision: INC-2025-00030-R2 (Migrate to UGridPathfindingSubsystem) (2025-11-17 00:40)
-        // Validate move using unified API before committing to intent
-        if (Intent.NextCell != Intent.CurrentCell)
-        {
-            UGridPathfindingSubsystem* PathFinder = World->GetSubsystem<UGridPathfindingSubsystem>();
-
-            if (PathFinder)
-            {
-                FString FailureReason;
-                const bool bMoveValid = PathFinder->IsMoveValid(
-                    Intent.CurrentCell,
-                    Intent.NextCell,
-                    GetOwner(),
-                    FailureReason);
-
-                if (!bMoveValid)
-                {
-                    UE_LOG(LogTurnManager, Warning,
-                        TEXT("[ComputeIntent] %s: MOVE rejected by validation: %s | From=(%d,%d) To=(%d,%d)"),
-                        *GetNameSafe(GetOwner()), *FailureReason,
-                        Intent.CurrentCell.X, Intent.CurrentCell.Y,
-                        Intent.NextCell.X, Intent.NextCell.Y);
-
-                    // Fallback to wait if move is invalid
-                    Intent.NextCell = Intent.CurrentCell;
-                    Intent.AbilityTag = FGameplayTag::RequestGameplayTag(TEXT("AI.Intent.Wait"));
-                }
-            }
-        }
-
         if (Intent.NextCell == Intent.CurrentCell)
         {
             Intent.AbilityTag = FGameplayTag::RequestGameplayTag(TEXT("AI.Intent.Wait"));
-            UE_LOG(LogTurnManager, Log,
-                TEXT("[ComputeIntent] %s WAIT - NextCell identical to current (TileDistance=%d)"),
-                *GetNameSafe(GetOwner()), DistanceToPlayer);
+            UE_LOG(LogTurnManager, Log, TEXT("[ComputeIntent] %s WAIT - NextCell identical to current (TileDistance=%d)"), *GetNameSafe(GetOwner()), DistanceToPlayer);
         }
         else
         {
-            UE_LOG(LogTemp, Log,
-                TEXT("[ComputeIntent] %s: MOVE intent (TileDistance=%d > Range=%d)"),
-                *GetNameSafe(GetOwner()), DistanceToPlayer, AttackRangeInTiles);
+            UE_LOG(LogTemp, Log, TEXT("[ComputeIntent] %s: MOVE intent (TileDistance=%d > Range=%d)"), *GetNameSafe(GetOwner()), DistanceToPlayer, AttackRangeInTiles);
         }
     }
 
```
