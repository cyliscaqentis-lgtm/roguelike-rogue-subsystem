# ログ分析結果 - 2025-11-23

## 問題の詳細

**報告**: 最後のターンの1ターン前で、プレイヤーに近づかなかった敵が2体いた

## ログ分析

### Turn 1の状況

**プレイヤー位置**: `(59, 34)`

**敵の意図生成**:
- `BP_EnemyUnitBase_C_1`: MOVE intent (距離4)
- `BP_EnemyUnitBase_C_2`: MOVE intent (距離2) → **WAIT に変換**
- `BP_EnemyUnitBase_C_3`: MOVE intent (距離4)
- `BP_EnemyUnitBase_C_4`: MOVE intent (距離2) → **WAIT に変換**

**WAIT変換の理由**:
- `BP_EnemyUnitBase_C_2`: "Blocked by non-moving unit at cell (58,34)"
- `BP_EnemyUnitBase_C_4`: "Success: Action (no conflict)" ← **これは不明瞭**

**実際の移動**:
- `BP_EnemyUnitBase_C_1`: `(61, 37)`に移動
- `BP_EnemyUnitBase_C_3`: `(56, 34)`に移動
- `BP_EnemyUnitBase_C_2`: WAIT（移動なし）
- `BP_EnemyUnitBase_C_4`: WAIT（移動なし）

### Turn 2の状況

**プレイヤー位置**: `(59, 35)`

**敵の意図生成**:
- `BP_EnemyUnitBase_C_1`: MOVE intent (距離2)
- `BP_EnemyUnitBase_C_2`: MOVE intent (距離2)
- `BP_EnemyUnitBase_C_3`: MOVE intent (距離3)
- `BP_EnemyUnitBase_C_4`: MOVE intent (距離2)

**予約**:
- `BP_EnemyUnitBase_C_2`: `(58, 34)`に予約
- `BP_EnemyUnitBase_C_1`: `(60, 36)`に予約

**実際の移動**:
- `BP_EnemyUnitBase_C_1`: 移動（詳細不明）
- `BP_EnemyUnitBase_C_2`: `(58, 34)`に移動
- `BP_EnemyUnitBase_C_3`: `(56, 35)`に移動
- `BP_EnemyUnitBase_C_4`: 移動の記録なし（WAIT？）

## 問題点

### 1. GetNextStepログが出力されていない

**原因**: `GetNextStep`が呼ばれる前に、`ConflictResolver`や他のロジックでWAITに変換されている可能性

**必要な対応**:
- `EnemyThinkerBase::ComputeIntent`にログ追加
- `ConflictResolverSubsystem`の決定理由をログ出力
- どの段階でWAITに変換されたのかを追跡

### 2. "Success: Action (no conflict)" の意味が不明

`BP_EnemyUnitBase_C_4`がWAITに変換された理由が"Success: Action (no conflict)"となっているが、これは矛盾している。

**推測**: ConflictResolverが何らかの理由でWAITを選択したが、理由が正しくログされていない

### 3. Enemy_4の移動記録がない

Turn 2で`BP_EnemyUnitBase_C_4`の移動完了ログがない。WAITになったのか、ログが欠落しているのか不明。

## 次のステップ

### Phase 1: EnemyThinkerにログ追加

`EnemyThinkerBase.cpp`の`ComputeIntent`に、`GetNextStep`呼び出しの前後でログを追加：

```cpp
// GetNextStepを呼ぶ前
UE_LOG(LogEnemyThinker, Log,
    TEXT("[ComputeIntent] %s: Calling GetNextStep from (%d,%d)"),
    *GetNameSafe(GetOwner()), CurrentCell.X, CurrentCell.Y);

const FIntPoint NextCell = DistanceField->GetNextStepTowardsPlayer(CurrentCell, GetOwner());

// GetNextStepの結果
UE_LOG(LogEnemyThinker, Log,
    TEXT("[ComputeIntent] %s: GetNextStep returned (%d,%d) (moved=%d)"),
    *GetNameSafe(GetOwner()), NextCell.X, NextCell.Y,
    (NextCell != CurrentCell) ? 1 : 0);
```

### Phase 2: ConflictResolverのログ改善

`ConflictResolverSubsystem`のWAIT変換理由を明確にする。

### Phase 3: 再テスト

ログを追加してから再度テストプレイし、詳細なログを取得。

## 暫定的な推測

**仮説**: `BP_EnemyUnitBase_C_2`と`BP_EnemyUnitBase_C_4`は、`GetNextStep`が呼ばれる前に`ConflictResolver`によってWAITに変換されている。

**理由の可能性**:
1. 目標セルが他の敵に占有されている
2. 移動経路が他の敵とコンフリクトしている
3. ConflictResolverのロジックに問題がある

**確認が必要**: `GetNextStep`が実際に呼ばれているか、呼ばれていないならなぜか。
