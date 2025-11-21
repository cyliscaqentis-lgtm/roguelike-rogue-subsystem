# 次のリファクタリング候補

## 現状分析
- **現在の行数**: 3110行
- **完了したステップ**: Step 3-6 (コマンド処理、攻撃フェーズ、移動フェーズ、UnitTurnStateSubsystem)

## 次の大きなリファクタリング候補

### 優先度1: 大きな関数の分割

#### 1. `OnTurnStartedHandler` (210行: 1045-1254)
**責務が多すぎる問題**:
- ターンID設定
- 敵リストのリフレッシュ
- GridOccupancyの更新
- DistanceFieldの更新
- 敵Intentの事前生成

**提案**: 以下のサブシステムに分割
- `UTurnInitializationSubsystem`: ターン開始時の初期化処理を統括
  - `InitializeTurnState(int32 TurnId)`
  - `UpdateDistanceField(APawn* PlayerPawn, TArray<AActor*>& Enemies)`
  - `GeneratePreliminaryIntents()`

#### 2. `InitializeTurnSystem` (216行: 92-307)
**責務**: システム初期化
**提案**: 
- `UTurnSystemInitializer`: 初期化専用のヘルパークラス
  - サブシステムの取得と検証
  - イベントハンドラの登録
  - 初期状態の設定

#### 3. `OnPlayerMoveCompleted` (134行: 1497-1630)
**責務が多すぎる問題**:
- プレイヤー移動完了の処理
- 敵Intent再生成（条件付き）
- DistanceField更新
- 次フェーズへの遷移

**提案**: 
- `UPlayerMoveHandlerSubsystem`: プレイヤー移動後の処理を統括
  - `OnPlayerMoveCompleted(const FGameplayEventData* Payload)`
  - `RegenerateEnemyIntentsIfNeeded()`
  - `TransitionToNextPhase()`

#### 4. `ExecuteSimultaneousPhase` (83行: 1378-1460)
**提案**: 
- `USimultaneousPhaseExecutor`: 同時実行フェーズの管理
  - `ExecutePhase()`
  - `OnPhaseCompleted()`

#### 5. `RefreshEnemyRoster` (100行: 944-1043)
**既に`UnitTurnStateSubsystem`に一部移行済み**
**提案**: 完全に`UUnitTurnStateSubsystem`に統合

### 優先度2: ユーティリティ関数の抽出

#### `RogueActorUtils.h`に移動すべき関数
- `TryGetASC(const AActor* Actor)` (既に計画済み)
- `GetTeamIdOf(const AActor* Actor)` (既に計画済み)
- グリッド関連のヘルパー関数

### 優先度3: デバッグロジックの統合

#### `UTurnDebugSubsystem`への完全移行
- すべての`UE_LOG`呼び出しを`UTurnDebugSubsystem`経由に変更
- CSV出力ロジックの統合

## 推奨される次のステップ

### Step 7: `OnTurnStartedHandler`のリファクタリング
1. `UTurnInitializationSubsystem`を作成
2. DistanceField更新ロジックを移行
3. 敵Intent事前生成ロジックを移行
4. `OnTurnStartedHandler`を簡素化（20-30行程度に）

**期待される削減**: 約150-180行

### Step 8: `OnPlayerMoveCompleted`のリファクタリング
1. `UPlayerMoveHandlerSubsystem`を作成
2. Intent再生成ロジックを移行
3. フェーズ遷移ロジックを整理

**期待される削減**: 約100-120行

### Step 9: 初期化ロジックの整理
1. `InitializeTurnSystem`を複数の小さな関数に分割
2. 可能であれば`UTurnSystemInitializer`ヘルパーを作成

**期待される削減**: 約150-180行

## 総削減見込み
- **Step 7-9の合計**: 約400-480行削減
- **目標**: 2600-2700行程度まで削減

## 次のアクション
ユーザーに確認:
1. Step 7から開始するか？
2. 別の優先順位を希望するか？
3. 一度に複数のステップを実行するか？
