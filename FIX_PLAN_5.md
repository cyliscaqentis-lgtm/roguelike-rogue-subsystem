# 修正計画書: INC-2025-1117F (シーケンシャルモードにおけるムーブフェーズのスキップ問題)

## 1. 概要

- **インシデントID:** INC-2025-1117F
- **問題:** シーケンシャルモード（アタック→ムーブ）において、アタックフェーズが完了した直後に `TurnActionBarrier` が `OnAllMovesFinished` を誤ってブロードキャストする。これにより、ムーブフェーズが開始される前に `GameTurnManager` がターンを終了してしまい、プレイヤー（および敵）の移動が完全にスキップされる。

## 2. 原因仮説と根拠

- **仮説:** `TurnActionBarrier` は、アタックアクションとムーブアクションを区別せずに完了を待っている。シーケンシャルモードでは、まずアタックアクションのみがバリアに登録され、それらが完了するとバリアは「全アクション完了」と判断し `OnAllMovesFinished` を発行してしまう。`GameTurnManager` の `HandleMovePhaseCompleted` は、この誤った通知を防ぐためのガード条件を持っていない。
- **根拠:**
    - ログにおいて、アタックアクション完了直後に `[Barrier] ALL ACTIONS COMPLETED` が発生し、`OnAllMovesFinished` がブロードキャストされている。
    - その直後に `HandleMovePhaseCompleted` が呼ばれ、ターンが終了している。
    - 本来呼ばれるべき `HandleAttackPhaseCompleted` は、ターンが終了してしまった後に「Stale TurnId」として遅れて呼ばれており、ムーブフェーズを開始する `ExecuteMovePhase(true)` の呼び出しに到達できていない。

## 3. 修正方針案

`AGameTurnManagerBase::HandleMovePhaseCompleted` 関数にガード条件を追加し、シーケンシャルモードのアタックフェーズ中（＝ムーブフェーズがまだ開始されていない状態）は、バリアからの完了通知を無視するようにする。

- **修正対象ファイル:** `Source/LyraGame/Rogue/Turn/GameTurnManagerBase.cpp`
- **修正対象関数:** `AGameTurnManagerBase::HandleMovePhaseCompleted`
- **追加するフラグ:** `bSequentialMovePhaseStarted` を利用する。（`HandleAttackPhaseCompleted` で `true` にセットされる）

- **修正後 (イメージ):**
  ```cpp
  // GameTurnManagerBase.cpp

  void AGameTurnManagerBase::HandleMovePhaseCompleted(int32 FinishedTurnId)
  {
      // ★★★ START: 新しいガード処理 ★★★
      if (bSequentialModeActive && !bSequentialMovePhaseStarted)
      {
          UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] HandleMovePhaseCompleted: Ignoring barrier completion during sequential attack phase. Waiting for HandleAttackPhaseCompleted to start the move phase."), FinishedTurnId);
          return;
      }
      // ★★★ END: 新しいガード処理 ★★★

      if (!HasAuthority())
      {
          // (既存の処理が続く)
      }
      // ...
  }
  ```

## 4. 影響範囲

- **シーケンシャルモードの安定化:** アタックフェーズ後にムーブフェーズがスキップされる問題が解決され、プレイヤーや敵が意図通りに移動できるようになる。
- **副作用のリスク:**
    - 低い。このガードは `bSequentialModeActive` が `true` の場合にのみ機能するため、同時移動フェーズの動作には影響を与えない。
    - `bSequentialMovePhaseStarted` フラグの状態管理に依存するが、このフラグは `HandleAttackPhaseCompleted` で `true` にセットされ、ターンの終わりにリセットされるため、正しく機能すると期待される。

## 5. テスト方針

- **手動テスト:**
    - 敵が攻撃を選択する状況（シーケンシャルモードが発動する状況）でプレイヤーを移動させる。
    - アタックフェーズが実行された後、プレイヤーが正常に移動できることを確認する。
    - ログを確認し、`HandleMovePhaseCompleted: Ignoring barrier completion...` の新しいログが出力され、その後 `HandleAttackPhaseCompleted` が呼ばれ、最終的に再度 `HandleMovePhaseCompleted` が呼ばれてターンが正常に終了することを確認する。
    - 敵が攻撃を選択しない状況（同時移動モード）でも、移動が正常に動作することを確認する（デグレード防止）。
