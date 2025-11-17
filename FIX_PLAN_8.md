# リファクタリング計画書: INC-2025-1117H (コード重複の解消)

## 1. 目的

前回のターン構造修正（`FIX_PLAN_7.md`）で導入された新しいヘルパー関数（`ExecuteEnemyTurn_Sequential` など）と、元から存在していた `ExecuteAttacks` や `ExecuteMovePhase` との間のロジックの重複を解消する。責務を再整理し、コードの再利用性を高め、保守性を向上させる。

## 2. アーキテクチャ変更の概要

-   **攻撃処理の統合:** `ExecuteAttacks` 関数を、アクションリストを直接受け取れるように拡張し、`ExecuteEnemyTurn_Sequential` を廃止する。
-   **移動処理の統合:** `ExecuteMovePhase` と `ExecuteEnemyMoves_Sequential` の両方から利用できる、共通の「移動ディスパッチ関数」を新設する。

## 3. 修正方針

### 3.1. `ExecuteAttacks` の汎用化と `ExecuteEnemyTurn_Sequential` の廃止

1.  **`ExecuteAttacks` のシグネチャ変更:**
    -   `AGameTurnManagerBase.h` で、`ExecuteAttacks` のシグネチャを以下のように変更する。
        ```cpp
        // 変更前
        // void ExecuteAttacks();

        // 変更後
        void ExecuteAttacks(const TArray<FResolvedAction>& PreResolvedAttacks = TArray<FResolvedAction>());
        ```
        デフォルト引数を設定することで、既存の呼び出し元への影響をなくす。

2.  **`ExecuteAttacks` の実装修正:**
    -   `AGameTurnManagerBase.cpp` で、`ExecuteAttacks` の実装を修正する。
    -   関数の冒頭で、引数 `PreResolvedAttacks` が空かどうかをチェックする。
    -   **空の場合:** 従来通り、`EnemyData->Intents` から攻撃アクションを生成するロジックを実行する。
    -   **空でない場合:** 引数で渡された `PreResolvedAttacks` を、後続の `AttackPhaseExecutorSubsystem` に渡す処理に直接使用する。インテントからのアクション生成ロジックはスキップする。

3.  **`ExecuteEnemyTurn_Sequential` の廃止:**
    -   `ExecuteEnemyTurn_Sequential` 関数を `AGameTurnManagerBase.cpp` と `.h` から削除する。
    -   `OnPlayerMoveCompleted` 内の `ExecuteEnemyTurn_Sequential()` の呼び出しを、`CachedResolvedActions` から攻撃アクションを抽出して新しい `ExecuteAttacks()` に渡す呼び出しに置き換える。

### 3.2. 移動ディスパッチ処理の共通化

1.  **新しいヘルパー関数 `DispatchMoveActions` の作成:**
    -   `AGameTurnManagerBase.h` / `.cpp` に、新しいプライベート関数 `void DispatchMoveActions(const TArray<FResolvedAction>& ActionsToDispatch);` を作成する。
    -   この関数の実装は、現在の `CoreExecutePhase` のように、渡されたアクションの配列をループし、`DispatchResolvedMove` を呼び出すロジックとする。

2.  **`ExecuteMovePhase` のリファクタリング:**
    -   `ExecuteMovePhase` 内の `PhaseManager->CoreExecutePhase(ResolvedActions);` の呼び出しを、新しく作成した `DispatchMoveActions(ResolvedActions);` の呼び出しに置き換える。

3.  **`ExecuteEnemyMoves_Sequential` のリファクタリングと改名:**
    -   `ExecuteEnemyMoves_Sequential` を `DispatchEnemyMoves_Sequential` のような名前に変更するのが望ましい（責務がディスパッチになるため）。
    -   この関数内のロジックを、`CachedResolvedActions` から敵の移動アクションを抽出し、新設した `DispatchMoveActions()` にそのリストを渡すだけのシンプルなものに修正する。

## 4. 影響範囲

-   `AGameTurnManagerBase` クラス全体。
-   コードの重複が解消され、`ExecuteAttacks` と移動のディスパッチ処理が、異なるゲームモード（同時/逐次）で再利用されるようになる。
-   ロジックの変更はなく、あくまでコード構造のリファクタリングであるため、ゲームの動作自体に変化はないはずだが、呼び出し関係の変更に伴うデグレードのリスクには注意が必要。

## 5. テスト方針

-   **リグレッションテスト:**
    -   **同時実行モード:** 敵の攻撃がない状況で、プレイヤーと敵が正常に同時移動できることを確認する。
    -   **逐次実行モード:** 敵の攻撃がある状況で、「①プレイヤー移動 → ②敵の攻撃 → ③敵の移動」のシーケンスが正しく実行されることを確認する。
    -   両方のモードで、移動や攻撃がドロップされたり、ターンが意図せず終了したりしないことを確認する。
-   **コードレビュー:**
    -   `ExecuteAttacks` が、引数あり/なしの両方のケースで正しく動作することを確認する。
    -   `DispatchMoveActions` が、複数の呼び出し元から正しく利用されていることを確認する。
    -   不要になった古いヘルパー関数が完全に削除されていることを確認する。
