# 移動ロジック リファクタリング計画書

## 1. 全体方針

### 問題点

1.  **責務の混乱とレガシーコードの混在:**
    *   ユニットの物理的な移動ロジックが、最新の `UnitMovementComponent` と、旧式で `Tick` 駆動の `UnitBase` の両方に散在しています。これにより、予期せぬ動作や競合が発生しています。

2.  **AIの不合理な意思決定:**
    *   AIが移動距離を計算する際、8方向移動のグリッドに適さない「マンハッタン距離」を用いています。これにより、AIは移動コストを誤って評価し、非効率なルートを選択しています。

3.  **不完全な衝突解決ロジック:**
    *   自ユニットと敵ユニットが互いの場所に入れ替わろうとする「すれ違い」の状況で、システムがこれを単純な「衝突」と誤認し、一方（特にプレイヤー）の移動を不当にキャンセルしてしまいます。

4.  **検証ロジックの分散:**
    *   「そのタイルに移動できるか？」を判定するロジックが、AI、プレイヤー入力処理など、各所に個別に実装されています。これにより、ルール変更が困難で、一貫性がありません。

### 修正方針

1.  **移動実行ロジックの集約:**
    *   物理的な移動に関する責務を `UUnitMovementComponent` に完全に統一します。`UnitBase` から関連する古いコード（`Tick`での移動処理など）をすべて削除し、`UnitBase` は移動命令をコンポーネントに中継するだけの役割にします。

2.  **意思決定の標準化と修正:**
    *   AIの距離計算式を、グリッドのルールに合致した「チェビシェフ距離」に修正します。さらに、後述の「検証API」をAIに利用させ、ルールに基づいた移動判断を強制します。

3.  **衝突解決の高度化:**
    *   `ConflictResolverSubsystem` を改修し、「すれ違い」パターンを正しく検出させます。検出した場合は、両者の移動を許可し、スムーズなゲームプレイを実現します。

4.  **移動検証APIの確立:**
    *   移動の可否を判定する全てのルール（地形・占有・特殊ルール）を、`GridPathfinding`関連のライブラリに `IsMoveValid` のような単一のAPIとして集約します。以降、AIもプレイヤーも、すべての移動はこのAPIで検証します。

---

## 2. 詳細リファクタリング計画

#### **目的**
`UnitBase` と `GameTurnManagerBase` に存在する、APIを無視した自前の移動・検証ロジックを完全に排除する。これにより、責務を明確に分離し、距離計算の誤り、移動コンフリクト、ターン遅延といった一連のバグを根本的に解決する。

---

#### **2.1. `UnitBase` → `UUnitMovementComponent` への移行計画**

`UnitBase` から移動ロジックを完全に分離し、`UUnitMovementComponent` に責務を移譲する。

*   **`UnitBase` から削除するロジックとメンバー変数**:
    *   **対象関数**:
        *   `Tick(float DeltaSeconds)`: `UpdateMove` の呼び出しを削除します。
        *   `UpdateMove(float DeltaSeconds)`: 関数自体を完全に削除します。
        *   `StartNextLeg()`: 関数自体を完全に削除します。
    *   **対象メンバー変数**:
        *   `PathArray`: 移動パスの保持はコンポーネントの役割です。
        *   `MoveCounter`: パスの進捗管理はコンポーネントの役割です。
        *   `MoveStatus`: 移動状態の管理はコンポーネントの役割です。
        *   `LegStart`, `LegEnd`, `LegAlpha`: フレームごとの移動計算用変数は不要になります。
        *   `PixelsPerSec`, `MinPixelsPerSec`, `MaxPixelsPerSec`: 移動速度はコンポーネントで管理します。

*   **APIの呼び出し連携**:
    *   `UnitBase::MoveUnit(const TArray<FVector>& InPath)` の実装を、`MovementComp->MoveUnit(InPath);` の一行に置き換えます。`UnitBase` は移動の実行をコンポーネントに委譲するだけのラッパーとなります。
    *   `GA_MoveBase` や `TurnManager`（または関連サブシステム）は、引き続き `AUnitBase::MoveUnit` を呼び出しますが、その実体はコンポーネントが担うことになります。

*   **`TurnBarrier` との連携**:
    *   移動の完了通知は `UUnitMovementComponent::OnMoveFinished` デリゲートが担います。
    *   `TurnCorePhaseManager` や `AttackPhaseExecutorSubsystem` のような、移動をディスパッチする上位のサブシステムが、各ユニットの `MovementComp->OnMoveFinished` にバインドします。
    *   このデリゲートが発火した際、バインドされた関数内で `UTurnActionBarrierSubsystem` に移動完了を通知します。これにより、`State.Action.InProgress` タグの管理とバリア同期が、コンポーネントベースのイベント駆動型に移行され、`Tick` への依存がなくなります。

---

#### **2.2. 検証ロジックの API 化計画**

`GameTurnManagerBase` にハードコードされた移動検証ロジックを、再利用可能なAPIとして `AGridPathfindingLibrary` に集約する。

*   **`AGridPathfindingLibrary` に切り出す検証ロジック**:
    *   `GameTurnManagerBase::OnPlayerCommandAccepted_Implementation` 内に実装されている以下のチェック項目を一つの関数にまとめます。
        1.  目標セルが通行可能か (`IsCellWalkableIgnoringActor`)
        2.  斜め移動時の「角抜け（Corner Cutting）」防止チェック（隣接2セルが通行可能か）
        3.  `UGridOccupancySubsystem` を利用した目標セルの占有チェック

*   **新しいAPIのシグネチャ提案**:
    *   `AGridPathfindingLibrary` に以下の関数を追加します。
    ```cpp
    /**
     * 指定されたセルへの移動が有効か、全てのルール（地形、占有、角抜け）を検証します。
     * @param From 開始セル
     * @param To 目標セル
     * @param MovingActor 移動するアクター
     * @param OutFailureReason (Optional) 検証が失敗した場合の理由を格納します。
     * @return 移動が有効な場合はtrue
     */
    UFUNCTION(BlueprintCallable, Category = "Pathfinding|Validation")
    bool IsMoveValid(const FIntPoint& From, const FIntPoint& To, AActor* MovingActor, FString& OutFailureReason);
    ```

*   **（最重要）Chebyshev 距離計算の保証**:
    *   ログで観測されたAIの距離計算ミスは、移動コストが均一なグリッドにおいて不適切なマンハッタン距離（`dx + dy`）を使用していたことが原因です。8方向移動のコストは **Chebyshev 距離（`max(dx, dy)`）** で測るのが適切です。
    *   **手順1**: `UGridUtils` に、`static int32 GetChebyshevDistance(const FIntPoint& A, const FIntPoint& B);` というユーティリティ関数を新規に作成します。
    *   **手順2**: 新しい `IsMoveValid` API内では、まず `GetChebyshevDistance(From, To)` が `1` であることを確認します。これにより、このAPIは常に1マス移動の検証に限定されます。
    *   **手順3**: コードベース全体で "Manhattan" や `FGridUtils::ManhattanDistance` を検索し、AIの行動決定や距離計算に関わる箇所を、新しく作成した `UGridUtils::GetChebyshevDistance` を使用するように修正します。

---

#### **2.3. `ComputeIntent`（AI）の修正計画**

AIの行動決定ロジックが、上記で標準化された移動ルールと距離計算を尊重するように修正する。

*   **手順**:
    1.  `UEnemyAISubsystem` または `UEnemyThinkerBase` 内で、AIが次の移動先候補セルを決定するロジックを特定します。
    2.  各候補セルに対して移動を決定する前に、必ず **`AGridPathfindingLibrary::IsMoveValid()`** を呼び出し、移動がルール上可能であることを検証します。
    3.  プレイヤーまでの距離を計算する全ての箇所（例: `BuildObservations`）で、マンハッタン距離ではなく `UGridUtils::GetChebyshevDistance` を使用するように修正します。これにより、AIはプレイヤーと同じコスト計算で移動距離を評価するようになります。

---

#### **2.4. `ConflictResolver` との統合計画**

`Tick` ベースの移動を廃止し、`ConflictResolverSubsystem` が `UUnitMovementComponent` と連携して、宣言的な意図（Intent）に基づいて競合を解決する仕組みを構築する。

*   **役割分担の定義**:
    *   **`ConflictResolverSubsystem` の役割**: ターン開始時に、プレイヤーと全てのAIから移動意図（`FEnemyIntent`）を収集します。移動が実行される**前**に、全ての意図を評価し、競合（同じセルへの移動、すれ違い）を検出・解決します。解決結果として、実際に移動を実行するアクターと待機するアクターを決定した `FResolvedAction` のリストを生成します。
    *   **`TurnCorePhaseManager` の役割**: `ConflictResolverSubsystem` から受け取った `FResolvedAction` リストに基づき、移動が許可されたユニットに対してのみ `AUnitBase::MoveUnit`（内部的に `UUnitMovementComponent` を呼び出す）をディスパッチします。

*   **プレイヤー移動キャンセル問題の解決ロジック**:
    *   最新ログで観測された「プレイヤーの移動がキャンセルされる問題」は、すれ違い（Swap）が単純な衝突として扱われ、プレイヤーの移動が一方的に破棄されることが原因です。
    *   **解決策**: `ConflictResolverSubsystem` に「すれ違い」を特別に扱うロジックを追加します。
        1.  アクターAがセルBへ、アクターBがセルAへ移動しようとしている競合を「スワップパターン」として検出します。
        2.  スワップパターンが検出された場合、両方のアクターの `FResolvedAction` を「待機（Wait）」ではなく「移動（Move）」としてマークします。
        3.  これにより、`TurnCorePhaseManager` は両者の移動を許可し、結果としてアクターがすれ違うアニメーションが実行されます。これにより、プレイヤーの移動が不必要にキャンセルされる問題が解決されます。