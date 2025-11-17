# リファクタリング計画書: INC-2025-1117G (ターン構造の根本的修正)

## 1. 目的

`GameTurnManager` のターン進行ロジックを、新たに確定したゲームデザイン仕様に合わせて根本的に修正する。現在の「攻撃が先、移動が後」になっている逐次実行モードを、**「①プレイヤー移動 → ②敵の攻撃 → ③敵の移動」** という正しい順序に修正する。

## 2. アーキテクチャ変更の概要

現在の `ExecuteSequentialPhase` が即座に `ExecuteAttacks` を呼び出す構造を廃止し、プレイヤーの移動完了をトリガーとして、後続の敵フェーズ（攻撃→移動）が実行されるように、状態遷移を全面的に再設計する。

## 3. 修正方針

### 3.1. 新しいメンバー変数の追加

-   `AGameTurnManagerBase.h` に、解決済みのアクションをターン内で一時的に保持するためのメンバー変数を追加する。
    ```cpp
    TArray<FResolvedAction> CachedResolvedActions;
    ```

### 3.2. `OnPlayerCommandAccepted_Implementation` のロジック修正

-   この関数は、ターン全体の司令塔となる。
-   **`bHasAttack == true` (逐次実行モード) の場合:**
    1.  `CoreResolvePhase` を呼び出して、全ユニット（プレイヤー＋敵）の `ResolvedActions` を生成する。
    2.  生成した `ResolvedActions` を、新しく追加した `CachedResolvedActions` メンバー変数にキャッシュする。
    3.  `CachedResolvedActions` の中から**プレイヤーのアクションのみ**を検索する。
    4.  見つかったプレイヤーのアクションに対してのみ `DispatchResolvedMove` を呼び出し、プレイヤーの移動を開始させる。
    5.  **この時点では、敵のアクションは一切実行しない。**
-   **`bHasAttack == false` (同時実行モード) の場合:**
    -   現在のロジックを維持し、`ExecuteSimultaneousPhase` を呼び出す。

### 3.3. `OnPlayerMoveCompleted` の役割変更

-   この関数は、プレイヤーの移動アビリティ (`GA_MoveBase`) が完了したときに呼び出されるデリゲート。
-   逐次実行モードにおける、**次のフェーズへの重要なトリガー**となる。
-   関数の内部に `if (bSequentialModeActive)` の分岐を追加する。
-   `true` の場合（＝プレイヤーの単独移動が完了した場合）、敵のフェーズを開始するための新しい関数 `ExecuteEnemyTurn_Sequential()` を呼び出す。

### 3.4. 新しい関数 `ExecuteEnemyTurn_Sequential` の作成

-   `AGameTurnManagerBase.h` / `.cpp` に新しいプライベート関数 `void ExecuteEnemyTurn_Sequential();` を作成する。
-   **処理内容:**
    1.  `CachedResolvedActions` メンバー変数から、**「攻撃」**アクションのみを抽出する。
    2.  抽出した攻撃アクションを `AttackPhaseExecutorSubsystem` に渡して、敵の攻撃フェーズを実行する (`ExecuteAttacks` に似たロジック）。

### 3.5. `HandleAttackPhaseCompleted` のロジック修正

-   この関数は、敵の攻撃フェーズが完了したときに呼び出される。
-   逐次実行モードの場合 (`if (bSequentialModeActive)`), 敵の移動フェーズを開始するための新しい関数 `ExecuteEnemyMoves_Sequential()` を呼び出すように修正する。

### 3.6. 新しい関数 `ExecuteEnemyMoves_Sequential` の作成

-   `AGameTurnManagerBase.h` / `.cpp` に新しいプライベート関数 `void ExecuteEnemyMoves_Sequential();` を作成する。
-   **処理内容:**
    1.  `CachedResolvedActions` メンバー変数から、**「移動」**アクション（敵のもののみ）を抽出する。
    2.  抽出した移動アクションに対して `DispatchResolvedMove` を呼び出し、敵の移動フェーズを実行する。

### 3.7. `HandleMovePhaseCompleted` のロジック修正

-   この関数は、移動フェーズが完了したときに呼び出される。
-   逐次実行モードの場合、これは「敵の移動完了」を意味するため、ここで `EndEnemyTurn()` を呼び出してターンを正常に終了させる。

## 4. 影響範囲

-   `AGameTurnManagerBase` の状態遷移ロジック全体。
-   ターン進行の順序が、ゲームデザインの仕様と完全に一致するようになる。
-   プレイヤーの移動がドロップされる問題、および関連するすべての同期問題が根本的に解決される。
-   副作用のリスク：中〜高。大規模なリファクタリングであるため、各状態遷移が正しく実装されているか慎重なテストが必要。特に、`CachedResolvedActions` のクリアタイミングや、各完了ハンドラが意図したモードでのみ動作することの確認が重要。
