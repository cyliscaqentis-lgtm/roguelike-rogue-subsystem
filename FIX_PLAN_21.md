# 修正計画書: AI行動決定ロジックの改修（最適攻撃位置と代替パス）

## 1. 概要

敵AIの `ComputeIntent` ロジックを改修し、以下の2点を同時に実現する。
1.  **[新機能] 最適な攻撃位置への移動:** 攻撃範囲内にいても、より有利な特定のセル（例: プレイヤーの真上）が空いていれば、そちらへの移動を優先する。
2.  **[バグ修正] 代替パス探索の改善:** 移動先がブロックされていた場合に、プレイヤーから遠ざかるなどの不適切な移動を選択するバグを修正し、有効な次善策を選択するようにする。

## 2. 原因と修正方針

### Task 1: 最適な攻撃位置への移動 (新機能)

*   **現状:** `Distance <= Range` であれば、現在位置がどこであろうと即座に `ATTACK` を選択している。
*   **修正方針:** `ATTACK` を決定するロジックの前に、優先移動の評価を挟む。
    1.  「最適な攻撃位置」（例: `PlayerGrid + (0, 1)`）を定義する。
    2.  自分が攻撃範囲内にいる場合、まず自分がその最適位置にいるかチェックする。
    3.  最適位置にいない場合、そのセルが移動可能かチェックする。
    4.  移動可能であれば、インテントを `ATTACK` ではなく `MOVE` に設定し、移動先をその最適位置にする。
    5.  自分が既に最適位置にいるか、最適位置が埋まっている場合にのみ、現在地から `ATTACK` する。

### Task 2: 代替パス探索の改善 (バグ修正)

*   **現状:** `IsMoveValid` で移動が拒否された場合、代替案を探さず即座に `WAIT` を選択している。`Found alternative path` ログのロジックは、プレイヤーへの距離を考慮していないため、遠ざかる手を選ぶことがある。
*   **修正方針:** `IsMoveValid` が `false` を返した場合のフォールバック処理を、賢い代替探索ロジックに置き換える。
    1.  現在地に隣接する8方向のセルをループで評価する。
    2.  各セルについて `IsMoveValid` を実行し、移動可能か判定する。
    3.  移動可能なセルのうち、プレイヤーとの距離 (`ChebyshevDistance`) が最も小さくなるものを、最終的な `NextCell` として採用する。
    4.  移動可能なセルが一つもなければ、インテントを `WAIT` にする。

## 3. 実装詳細 (コード差分)

`EnemyThinkerBase.cpp` の `ComputeIntent_Implementation` を以下のように全面的に書き換える。

```cpp
// UEnemyThinkerBase::ComputeIntent_Implementation の新しい実装案

FEnemyIntent Intent;
// ... 初期化 ...

// --- 1. 変数準備 ---
const int32 DistanceToPlayer = FGridUtils::ChebyshevDistance(CurrentEnemyCell, PlayerGridCell);
UGridPathfindingSubsystem* PathFinder = World->GetSubsystem<UGridPathfindingSubsystem>();
if (!PathFinder) { /* エラー処理 */ return Intent; }

// --- 2. 最適な攻撃位置の定義 ---
const FIntPoint PreferredAttackCell = PlayerGridCell + FIntPoint(0, 1); // 例: プレイヤーの真上

// --- 3. 行動決定ロジック ---

// 3a. 攻撃範囲内にいる場合の優先度判定
if (DistanceToPlayer <= AttackRangeInTiles)
{
    if (CurrentEnemyCell == PreferredAttackCell)
    {
        // 最適位置にいるので攻撃
        Intent.AbilityTag = FGameplayTag::RequestGameplayTag(TEXT("AI.Intent.Attack"));
        Intent.NextCell = CurrentEnemyCell;
        Intent.TargetActor = PlayerActor;
        // ... ログ ...
        return Intent;
    }
    else
    {
        FString DummyReason;
        if (PathFinder->IsMoveValid(CurrentEnemyCell, PreferredAttackCell, GetOwner(), DummyReason))
        {
            // 最適位置が空いているので、そちらへ移動を優先
            Intent.AbilityTag = FGameplayTag::RequestGameplayTag(TEXT("AI.Intent.Move"));
            Intent.NextCell = PreferredAttackCell;
            // ... ログ ...
            return Intent;
        }
        else
        {
            // 最適位置が埋まっているので、現在地から攻撃
            Intent.AbilityTag = FGameplayTag::RequestGameplayTag(TEXT("AI.Intent.Attack"));
            Intent.NextCell = CurrentEnemyCell;
            Intent.TargetActor = PlayerActor;
            // ... ログ ...
            return Intent;
        }
    }
}

// 3b. 攻撃範囲外の場合の移動先決定
Intent.AbilityTag = FGameplayTag::RequestGameplayTag(TEXT("AI.Intent.Move"));
if (UDistanceFieldSubsystem* DistanceField = World->GetSubsystem<UDistanceFieldSubsystem>())
{
    Intent.NextCell = DistanceField->GetNextStepTowardsPlayer(CurrentEnemyCell, GetOwner());
}
else
{
    Intent.NextCell = CurrentEnemyCell;
}

// 3c. 移動検証と代替パス探索
if (Intent.NextCell != CurrentEnemyCell)
{
    FString FailureReason;
    if (!PathFinder->IsMoveValid(CurrentEnemyCell, Intent.NextCell, GetOwner(), FailureReason))
    {
        // 最適な移動先がブロックされている -> 代替パスを探す
        const FIntPoint NeighborOffsets[] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1} };
        FIntPoint BestAlternativeCell = CurrentEnemyCell;
        int32 BestDist = DistanceToPlayer;

        for (const FIntPoint& Offset : NeighborOffsets)
        {
            FIntPoint AltCell = CurrentEnemyCell + Offset;
            FString DummyReason;
            if (PathFinder->IsMoveValid(CurrentEnemyCell, AltCell, GetOwner(), DummyReason))
            {
                int32 AltDist = FGridUtils::ChebyshevDistance(AltCell, PlayerGridCell);
                if (AltDist < BestDist)
                {
                    BestDist = AltDist;
                    BestAlternativeCell = AltCell;
                }
            }
        }
        Intent.NextCell = BestAlternativeCell;
    }
}

// 3d. 最終的に移動しない場合はWAIT
if (Intent.NextCell == CurrentEnemyCell)
{
    Intent.AbilityTag = FGameplayTag::RequestGameplayTag(TEXT("AI.Intent.Wait"));
}

return Intent;
```
