# 現在の修正プラン

**最終更新**: 2025-11-23

---

## 🔍 問題: 敵がプレイヤーに詰めてこない（Y軸方向に移動しない）

### 根本的な問題

**ログから地形の状態や移動判断の理由が分からない** - これは設計上の重大な欠陥でした。

✅ **Phase 1完了**: 診断可能なログシステムを実装しました！

### 実装完了した改善

#### ✅ 1. GetNextStepTowardsPlayerの詳細ログ
`DistanceFieldSubsystem.cpp`に以下のログを追加：

- **開始ログ**: 現在位置、プレイヤー位置、現在の距離を常に出力
- **地形ブロックログ**: どのセルが地形でブロックされているか明示
- **斜め移動ブロックログ**: 両肩のどちらがブロックされているか詳細表示
- **距離改善なしログ**: 距離が改善しないセルを明示
- **候補評価ログ**: 各候補がなぜ選ばれたか/選ばれなかったかを記録
- **最終決定ログ**: 最終的な移動先と理由を出力

#### ✅ 2. log_summarizerの新プリセット
`log_summarizer.py`に`enemy_pathfinding`プリセットを追加：

```bash
python Tools\Log\log_summarizer.py enemy_path.txt --preset enemy_pathfinding
```

このプリセットは以下をフィルタリング：
- GetNextStepの詳細ログ
- 地形ブロック情報
- 候補評価プロセス
- 最終的な移動決定

### 期待されるログ出力例

改善後、以下のようなログが出力されます：

```
[GetNextStep] START: From=(32,16) Player=(48,18) CurrentDist=230
[GetNextStep] GoalDelta=(1,1) (direction to player)
[GetNextStep]   Neighbor (33,16): CANDIDATE ACCEPTED (Dist=230->220, Align=1, Diag=0) - better distance
[GetNextStep]   Neighbor (31,16): NO IMPROVEMENT (Dist=240, Current=230)
[GetNextStep]   Neighbor (32,17): BLOCKED BY TERRAIN
[GetNextStep]   Neighbor (32,15): BLOCKED BY TERRAIN
[GetNextStep]   Neighbor (33,17): DIAGONAL BLOCKED (Side1=(33,16):1, Side2=(32,17):0)
[GetNextStep]   Neighbor (33,15): candidate rejected (Dist=230->224, Align=1, Diag=1)
[GetNextStep]   Neighbor (31,17): DIAGONAL BLOCKED (Side1=(31,16):1, Side2=(32,17):0)
[GetNextStep]   Neighbor (31,15): NO IMPROVEMENT (Dist=244, Current=230)
[GetNextStep] RESULT: From=(32,16) -> Next=(33,16) (Dist=230->220, Candidates=2)
```

このログから以下が即座に分かります：
- ✅ Y=17の列が地形でブロックされている
- ✅ 斜め移動も片側がブロックされて使えない
- ✅ X軸方向のみが有効な候補
- ✅ なぜ(33,16)が選ばれたのか明確

---

## 次のステップ

### Phase 2: 診断実行（次回）
1. ⏳ ゲームをプレイして新しいログを取得
2. ⏳ `enemy_pathfinding`プリセットでログを抽出
3. ⏳ 敵がY方向に移動しない理由を特定
4. ⏳ 地形ブロックかロジック問題かを判断

### 使用方法

```bash
# 新しいログを取得した後、以下のコマンドを実行:
python Tools\Log\log_summarizer.py enemy_path_diagnosis.txt --preset enemy_pathfinding

# 特定のターンのみを分析:
python Tools\Log\log_summarizer.py enemy_path_turn5.txt --preset enemy_pathfinding --turn 5

# ターン範囲を指定:
python Tools\Log\log_summarizer.py enemy_path_turn3_5.txt --preset enemy_pathfinding --turn-range 3-5
```

### Phase 3: 問題修正（診断結果に基づく）
- [ ] 地形問題の場合: ダンジョン生成/マップ修正
- [ ] ロジック問題の場合: GetNextStep修正
- [ ] DistanceField問題の場合: Dijkstra修正

---

## コードリビジョンタグ

### ✅ Phase 1完了:
- `INC-2025-1123-LOG-R1`: Add detailed terrain and pathfinding logs to GetNextStepTowardsPlayer
- `INC-2025-1123-LOG-R4`: Add enemy_pathfinding preset to log_summarizer.py

### Phase 2-3（予定）:
- `INC-2025-1123-FIX-R1`: Fix terrain blocking enemy Y-axis movement
- `INC-2025-1123-FIX-R2`: Fix GetNextStep logic for Y-axis movement
- `INC-2025-1123-FIX-R3`: Fix DistanceField calculation for Y-axis pathfinding

---

## 完了した修正

以下の問題は解決済みです。詳細は `FIX_PLAN_COMPLETED_20251123.md` を参照してください。

1. **移動完了時のラグ** - GridOccupancy更新の即時化、Barrier通知の即時化
2. **歩行アニメーションが再生されない** - CharacterMovementComponent Velocityの同期
3. **ログから問題を診断できない設計上の欠陥** - 診断可能なログシステムの構築 ✅
