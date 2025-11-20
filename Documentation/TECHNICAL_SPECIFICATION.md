## 1. 概要

* 本仕様書は、既存のタクティカル RPG 基盤に対して実施するリファクタリング内容を、**恒久仕様**として定義するものである。
* 主題は以下の 3 点である。

  * グリッド・経路探索のサブシステム統合
  * ターン進行とアクション同期（Barrier）基盤の確立
  * 敵 AI（観測 → 意図 → 行動）の責務分離と安定化

---

## 2. グリッド／経路探索仕様

### 2.1 サブシステム

* グリッド関連の中核 API は **`UGridPathfindingSubsystem`** とし、以下を提供すること。

  * `WorldToGrid(const FVector&) -> FIntPoint`
  * `GridToWorld(const FIntPoint&, float Z) -> FVector`
  * `IsCellWalkableIgnoringActor(const FIntPoint&, const AActor* IgnoreActor) -> bool`
  * `FindPathIgnoreEndpoints(const FVector& Start, const FVector& End, TArray<FVector>& OutPath, ... ) -> bool`
  * `GetNavPlaneZ() -> float`
  * `SetGridCostAtWorldPosition(const FVector& WorldPos, int32 Cost)`
* 旧 `AGridPathfindingLibrary` 由来の機能呼び出しは全て `UGridPathfindingSubsystem` 経由に置き換えること。

### 2.2 距離の定義

* タイル距離は原則、**チェビシェフ距離**とする。

  * 実装は `FGridUtils::ChebyshevDistance(const FIntPoint&, const FIntPoint&)` を使用すること。
  * 敵 AI、攻撃レンジ、観測等はこの距離を基準とする。

### 2.3 歩行可能判定

* 歩行可能判定は、**「地形のみ」**と**「地形＋占有」**を用途で明確に分けること。

  * 地形のみチェック: `IsCellWalkableIgnoringActor(Cell, nullptr)`

    * AI デバッグ、周辺診断、レベル設計向け。
  * 自身を除く占有チェック: `IsCellWalkableIgnoringActor(Cell, SelfActor)`

    * 実際の移動可否判定、移動アビリティなど。
* グリッド占有情報の実体は **`UGridOccupancySubsystem`** が管理し、個々のアビリティからは直接 Occupancy を更新しないこと。

  * Occupancy 更新は **`UnitMovementComponent::FinishMovement()`** に集約する。

---

## 3. ターン進行と Barrier 仕様

### 3.1 ターン ID 管理

* 現行ターン ID は `UTurnFlowCoordinator` または `AGameTurnManagerBase` により一元管理される。

  * 原則 `UTurnFlowCoordinator::GetCurrentTurnId()` を優先すること。
  * フォールバックとして `AGameTurnManagerBase::GetCurrentTurnId()` を利用してよい。

### 3.2 TurnActionBarrierSubsystem

* **`UTurnActionBarrierSubsystem`** は「ターン内アクションの完了同期」を担う。
* 登録／完了 API:

  * `FGuid RegisterAction(AActor* Owner, int32 TurnId)`
  * `void CompleteAction(AActor* Owner, int32 TurnId, FGuid ActionId)`
* 仕様:

  * 各アクション（Move / Attack / Wait 等）は **必ずターン ID とセットで登録／完了**すること。
  * Barrier に登録された全 Action が `CompleteAction` を受け取った時点で、そのターンは「行動完了」と見なされる。

---

## 4. 敵 AI 仕様

### 4.1 敵収集

* **`UEnemyAISubsystem::CollectAllEnemies`** は、プレイヤー以外の敵ユニットを以下の条件で収集する。

  * Team ID: 2 または 255（暫定敵チーム）
  * GameplayTag: `Faction.Enemy` を保有する
  * ActorTag: `"Enemy"` を持つ
* 収集結果はログ出力（`LogEnemyAI`）で確認可能とする。

### 4.2 観測フェーズ

* **`UEnemyAISubsystem::BuildObservations`** は以下を行う。

  * `PlayerGrid`（プレイヤー位置のグリッド座標）を受け取り、`UTurnCorePhaseManager::CoreObservationPhase(PlayerGrid)` を呼び出し、距離場を更新する。
  * 各 Enemy について `WorldToGrid` で位置を求め、`ChebyshevDistance(Enemy, Player)` で距離を計算し、`FEnemyObservation` に格納する。
* 観測結果は `TArray<FEnemyObservation>` として出力される。

### 4.3 意図決定

* **`UEnemyAISubsystem::CollectIntents`** は観測配列と敵配列を 1:1 対応で受け取り、各敵の **意図** を生成する。

  * 敵ごとの意図は `UEnemyThinkerBase::ComputeIntent(const FEnemyObservation&)` に委譲する。
* **`UEnemyThinkerBase`**:

  * `AttackRangeInTiles` を持ち、`Observation.DistanceInTiles` との比較で

    * 射程内: `AI.Intent.Attack`
    * 射程外: `AI.Intent.Move` または `AI.Intent.Wait`
  * Move の場合は `UDistanceFieldSubsystem::GetNextStepTowardsPlayer` により `NextCell` を決定する。

---

## 5. 行動アビリティ仕様

### 5.1 共通基盤: GA_TurnActionBase

* `UGA_TurnActionBase` はターン行動系アビリティの基底クラスとする。

  * `State_Ability_Executing` タグの付与／削除を責務とする。
  * `MaxExecutionTime` に基づくタイムアウト管理を行う。
  * タイムアウト時:

    * `Effect_Turn_AbilityTimeout` を付与
    * `CompletionEventTag`（`Gameplay_Event_Turn_Ability_Completed`）を送信
    * アビリティをキャンセルする。

### 5.2 移動: UGA_MoveBase

* 特徴:

  * トリガー: `GameplayEvent_Intent_Move`（`TurnCommandEncoding` による方向 or 事前解決セル）
  * アクション登録: `UTurnActionBarrierSubsystem::RegisterAction` を自身で呼び出し、`OnMoveFinished` で `CompleteAction` する。
  * 経路生成: `UGridPathfindingSubsystem::FindPathIgnoreEndpoints` を使用し、開始点を削ったパスを `Unit->MoveUnit(PathPoints)` に渡す。
* 歩行不可時:

  * `IsCellWalkableIgnoringActor(TargetCell, Unit)` が false の場合、ログ出力の上、即 `EndAbility`。
* `OnMoveFinished`:

  * Barrier の `CompleteAction` 実行後に `EndAbility` を呼ぶ。

### 5.3 攻撃基底: UGA_AttackBase

* トリガー:

  * `GameplayEvent_Intent_Attack`（`AbilityTriggers` に登録）
  * `PostInitProperties` で BP による上書きを検出し、必要であれば Trigger を再登録する。
* Barrier 連携:

  * Attack 自身は Barrier に対して **登録は行わない**。

    * 事前に `UAttackPhaseExecutorSubsystem` が全攻撃アクションを Barrier に登録する。
  * `ActivateAbility` では `AttackExecutor->GetActionIdForActor(Avatar)` により **事前登録済み ActionId を取得**し、`AttackTurnId` とともに保持する。
  * `EndAbility` で `CompleteAction(Avatar, AttackTurnId, AttackActionId)` を必ず呼び、終了後に ID をリセットする。

### 5.4 近接攻撃: UGA_MeleeAttack

* 特性:

  * `UGA_AttackBase` 派生。Damage と `RangeInTiles` を持つ。
  * `ActivateAbility` でターゲット決定:

    * EventData の SingleTargetHit
    * 取得できない場合は `AUnitBase::GetAdjacentPlayers()` による近接探索。
  * 攻撃開始時にプレイヤー操作を一時無効化（`SetIgnoreMoveInput(true)`）。

* 射程判定:

  * `ApplyDamageToTarget` 内で、`UGridPathfindingSubsystem` を用いて Attacker/Target のグリッド座標を算出し、チェビシェフ距離 `DistanceInTiles` を求める。
  * `DistanceInTiles > RangeInTiles` の場合、ダメージを適用せずログのみ出力する。

* ダメージ処理:

  * サーバー権限 (`HasAuthority`) のみで実行。
  * 攻撃者の `ULyraCombatSet::BaseDamage` を一時的に `Damage`（またはデフォルト 28.0f）に設定し、`MeleeAttackEffect` を Target の ASC に適用する。
  * 適用後、`BaseDamage` を元の値に戻す。

* 完了フロー:

  * モンタージ完了時 `OnMontageCompleted` → `ApplyDamageToTarget` → **`OnAttackCompleted()` を呼び出す**。

    * `OnAttackCompleted()` は `UGA_AttackBase` 実装であり、サーバー側で

      * `SendCompletionEvent(false)`
      * `EndAbility(...)`
        を実行する。

### 5.5 待機: UGA_WaitBase

* 特性:

  * `Ability_Wait` タグを持つ単純なターン消費アクション。
* Barrier 連携:

  * `ActivateAbility` 内で Barrier へ `RegisterAction`。
  * `OnWaitCompleted` で `EndAbility` を呼ぶ。
  * `EndAbility` 内で `CompleteBarrierAction` とログ出力を行い、最後に内部状態をリセットする。

---

## 6. ログ／診断仕様

* 以下のカテゴリでログ出力を行うこと。

  * `LogEnemyAI` : 敵収集・観測・意図決定
  * `LogTurnManager` : ターン進行、Barrier 完了、コマンドデコード
  * `LogMoveVerbose` : 移動経路、歩行判定、周辺デバッグダンプ
  * `LogAttackAbility` : 攻撃アクションと Barrier 連携
* 重大な異常（例: 距離場の異常値、PathFinder 未取得、無効ターン ID など）は `Error` レベルで出力し、原因調査に必要なコンテキスト（Actor 名、セル座標、ターン ID、ActionId）を必ず含めること。

---

## 7. 移行・互換要件（高レベル）

* 旧実装からの移行方針:

  * グリッド／経路探索:

    * 全て `AGridPathfindingLibrary` 呼び出しを `UGridPathfindingSubsystem` API に置き換え、`IsCellWalkable` 系は `IsCellWalkableIgnoringActor` に統一する。
  * Occupancy:

    * Occupancy の直接更新コードを削除し、移動完了時のみ `UnitMovementComponent` を通じて更新する。
  * ターン／アビリティ:

    * 行動アビリティは必ず `GA_TurnActionBase` を継承するか、それと同等のタイムアウト／イベント送信を実装する。
    * Move / Attack / Wait は本仕様の Barrier フローに従うこと。

---

以上が、現在のリファクタリング計画を「仕様」として定義した形です。
不足している観点（UI 特性、ネットワークレプリケーションの詳細など）があれば、その章を追加で切り出して定義できます。