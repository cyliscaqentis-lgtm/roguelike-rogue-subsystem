2の途中で躓き、現在はGeminiによって以下のように再定義されているが、どう思う？→# リファクタリング計画書: ターン処理システムの再設計

## 1. 背景

これまで、敵AIの移動に関するバグ（交通渋滞、不自然な移動、ユニットの消失、衝突、操作不能など）に対して、繰り返し修正を行ってきた。しかし、一つの問題を修正すると別の問題が露呈する状況が続いており、根本的な原因が個々のバグではなく、ターン処理全体のアーキテクチャの複雑さと責務分担の曖昧さにあることが明らかになった。

本計画書は、場当たり的な修正を中止し、ターン処理システムをより堅牢で予測可能なものに再設計することを目的とする。

---

## 2. 優先順位

本リファクタリングは、以下の優先順位で進める。

1.  **[Priority 1] 入力と WindowId の責務を確定し、入力受付ロジックを安定化する**
2.  **[Priority 2] 競合解決（ConflictResolver）の契約をコードで保証し、インテント消失を防ぐ**
3.  **[Priority 2.1] パッチ - 「敗者は留まる」原則による競合解決の完全化**
4.  **[Priority 3] ターンモード（Sequential / Simultaneous）決定と利用の責務を一元化する**
5.  **[Priority 4] EnemyThinker の純化（インテント表明フェーズの責務を「戦術判断」のみに限定する）**

---

## Priority 1: 入力と WindowId の責務を確定し、入力受付ロジックを安定化する

### 1. 目的とスコープ

#### 1-1. 目的

*   プレイヤー入力が「どのターン」「どの入力ウィンドウ」に属するかを **一意かつ正確に識別**できるようにする。
*   サーバ側で、「有効な入力」だけを受け付けるための基準を厳密にする。
*   `InputWindowId` の管理責務を **UTurnFlowCoordinator に一元化**し、
    `GameTurnManager` / `PlayerInputProcessor` / `PlayerController` からは「読み取りのみ」に制限する。

#### 1-2. スコープ

*   対象となるコンポーネント:
    *   `UTurnFlowCoordinator`（TFC）
    *   `AGameTurnManagerBase`（GTM）
    *   `UPlayerInputProcessor`（PIP）
    *   `APlayerControllerBase`（PC）
*   範囲:
    *   `TurnId` / `InputWindowId` / 入力ウィンドウ / コマンド検証 (`ValidateCommand`)
*   範囲外（この Priority では「前提」として扱う）:
    *   敵 AI のインテント生成
    *   ConflictResolver
    *   Attack / Move フェーズの詳細

### 2. 用語・データモデル

#### 2-1. 用語

*   **TurnId**
    *   現在のターンを識別する整数。
    *   サーバ権威。`UTurnFlowCoordinator` が管理する（推測）。
*   **InputWindowId**
    *   「プレイヤー入力を受け付けるウィンドウ」を識別する整数。
    *   1 回入力フェーズを開くごとにインクリメントされる単調増加値。
    *   サーバ権威。**唯一の書き込み元は `UTurnFlowCoordinator`**。
*   **Input Window**
    *   特定の `TurnId` / `InputWindowId` の組み合わせで表される、
        「この期間に送られてきたプレイヤーコマンドを受け付ける」論理的なウィンドウ。
    *   一度に開いている入力ウィンドウは 1 つだけとする。

#### 2-2. データ構造（推測を含む）

*   `FPlayerCommand`

    *   必須フィールド（仕様として要求）:
        *   `int32 TurnId;`
        *   `int32 WindowId;`
        *   その他、アクション種別・対象セルなどのゲーム固有情報。
*   `UTurnFlowCoordinator`

    *   フィールド（仕様として要求）:
        *   `int32 TurnId;`
        *   `int32 InputWindowId;`（Replicated）
*   `UPlayerInputProcessor`

    *   フィールド（仕様として要求）:
        *   `int32 CurrentAcceptedTurnId;`
        *   `int32 CurrentAcceptedWindowId;`
        *   `bool bInputWindowOpen;`

### 3. 不変条件（Invariants）

Priority 1 で **常に真であるべきルール** を定義します。

#### 3-1. SSOT（Single Source of Truth）

*   `InputWindowId` を書き換えてよいのは `UTurnFlowCoordinator` のみ。
*   `AGameTurnManagerBase` / `UPlayerInputProcessor` / `APlayerControllerBase` は **InputWindowId を変更してはならない**。
    *   読み取りは許可。

#### 3-2. Input Window の整合性

*   1 つのプレイヤーコマンド (`FPlayerCommand`) は、必ず

    *   `TurnId == TFC::TurnId`
    *   `WindowId == TFC::InputWindowId`
        を送信時点で満たしていること（クライアント側の責務）。

#### 3-3. サーバ側検証

*   サーバは、プレイヤーコマンドを受信するたびに **必ず** 次を検証する:

    *   `bInputWindowOpen == true`
    *   `Command.TurnId == CurrentAcceptedTurnId`
    *   `Command.WindowId == CurrentAcceptedWindowId`
*   この条件を満たさないコマンドは「無効な入力」として破棄する（ログは残してよい）。

#### 3-4. ウィンドウのライフサイクル

*   入力ウィンドウを「開く」のは必ずサーバ側で `UTurnFlowCoordinator::OpenNewInputWindow()` を呼んだ瞬間。
*   入力ウィンドウを「閉じる」のは、MovePrecheck 成功など **GameTurnManager / PlayerInputProcessor が明示的に決めたタイミングのみ**。
*   「入力ウィンドウが閉じたあとに届いたコマンド」は、すべて無効扱いとなる。

### 4. コンポーネント別 仕様

#### 4-1. `UTurnFlowCoordinator`（権威）

##### 4-1-1. 責務

*   `TurnId` / `InputWindowId` の **唯一の権威**。
*   サーバで値を更新し、その状態をクライアントへレプリケートする。

##### 4-1-2. フィールド（仕様）

*   `int32 TurnId;`
*   `int32 InputWindowId;`
    *   `UPROPERTY(ReplicatedUsing=OnRep_InputWindowId)` 等でレプリケート。

##### 4-1-3. 公開 API

```cpp
UCLASS()
class UTurnFlowCoordinator : public UObject
{
    GENERATED_BODY()

public:

    // サーバ権威のみが呼び出せる
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly)
    void OpenNewInputWindow()
    {
        check(HasAuthority());
        ++InputWindowId;

        UE_LOG(LogTurn, Log,
            TEXT("[TFC] OpenNewInputWindow: TurnId=%d, WindowId=%d"),
            TurnId, InputWindowId);
    }

    UFUNCTION(BlueprintCallable)
    int32 GetCurrentInputWindowId() const
    {
        return InputWindowId;
    }

    UFUNCTION(BlueprintCallable)
    int32 GetCurrentTurnId() const
    {
        return TurnId;
    }
};
```

*   重要なルール:

    *   `InputWindowId` に対する **唯一のインクリメント処理**は `OpenNewInputWindow()` のみ。
    *   他クラスは `GetCurrentInputWindowId()` / `GetCurrentTurnId()` の読み取りだけ行う。

#### 4-2. `AGameTurnManagerBase`（ターン制御）

##### 4-2-1. 責務

*   「いつ入力を受け付けるか」を決め、サーバ側で入力ウィンドウを開く。
*   `TurnFlowCoordinator` に対して入力ウィンドウ開放を依頼。
*   `PlayerInputProcessor` に対して「このターン、このウィンドウで受付開始」と伝える。

##### 4-2-2. フィールドの扱い

*   `CurrentInputWindowId` のようなメンバを **持たない**。
*   InputWindowId が必要な場合は、常に
    `TurnFlowCoordinator->GetCurrentInputWindowId()` を参照する。

##### 4-2-3. 入力ウィンドウ開放の仕様

```cpp
void AGameTurnManagerBase::OpenInputWindow()
{
    check(TurnFlowCoordinator);
    check(PlayerInputProcessor);

    // ① 新しい入力ウィンドウを開く（WindowId++）
    TurnFlowCoordinator->OpenNewInputWindow();

    const int32 CurrentTurnId   = TurnFlowCoordinator->GetCurrentTurnId();
    const int32 CurrentWindowId = TurnFlowCoordinator->GetCurrentInputWindowId();

    // ② PlayerInputProcessor に「このターン／ウィンドウで受付開始」と伝える
    PlayerInputProcessor->OpenInputWindow(CurrentTurnId, CurrentWindowId);

    // ③ 内部状態更新（例）
    bWaitingForPlayerInput = true;

    UE_LOG(LogTurn, Log,
        TEXT("[GTM] OpenInputWindow: TurnId=%d, WindowId=%d"),
        CurrentTurnId, CurrentWindowId);
}
```

##### 4-2-4. ウィンドウクローズの責務

*   ウィンドウを閉じるタイミングは、原則 `UPlayerInputProcessor` が MovePrecheck 成功時などで決める。
*   `GameTurnManager` は、必要に応じて

    *   `PlayerInputProcessor->CloseInputWindow();`
    *   `bWaitingForPlayerInput = false;`
        を呼び、ターン遷移処理に進む。

#### 4-3. `UPlayerInputProcessor`（サーバ側入力受付）

##### 4-3-1. 責務

*   サーバ側で受信したプレイヤーコマンドが「今受け付けるべきものか」を検証する。
*   有効なコマンドだけを内部キューへ追加し、MovePrecheck・ターン進行に渡す。

##### 4-3-2. フィールド（仕様）

```cpp
int32 CurrentAcceptedTurnId = -1;
int32 CurrentAcceptedWindowId = -1;
bool  bInputWindowOpen = false;
TArray<FPlayerCommand> PendingCommands; // 例
```

##### 4-3-3. API: 入力ウィンドウの制御

```cpp
void UPlayerInputProcessor::OpenInputWindow(int32 TurnId, int32 WindowId)
{
    CurrentAcceptedTurnId   = TurnId;
    CurrentAcceptedWindowId = WindowId;
    bInputWindowOpen        = true;

    PendingCommands.Empty();

    UE_LOG(LogTurn, Log,
        TEXT("[PIP] OpenInputWindow: TurnId=%d, WindowId=%d"),
        TurnId, WindowId);
}

void UPlayerInputProcessor::CloseInputWindow()
{
    bInputWindowOpen = false;

    UE_LOG(LogTurn, Log,
        TEXT("[PIP] CloseInputWindow: TurnId=%d, WindowId=%d"),
        CurrentAcceptedTurnId, CurrentAcceptedWindowId);
}
```

##### 4-3-4. API: コマンド検証

```cpp
bool UPlayerInputProcessor::ValidateCommand(const FPlayerCommand& Command) const
{
    if (!bInputWindowOpen)
    {
        UE_LOG(LogTurn, Verbose,
            TEXT("[PIP] RejectCommand: InputWindowClosed (Cmd Turn=%d, Window=%d)"),
            Command.TurnId, Command.WindowId);
        return false;
    }

    if (Command.TurnId != CurrentAcceptedTurnId)
    {
        UE_LOG(LogTurn, Warning,
            TEXT("[PIP] RejectCommand: TurnId mismatch (Expected=%d, Got=%d)"),
            CurrentAcceptedTurnId, Command.TurnId);
        return false;
    }

    if (Command.WindowId != CurrentAcceptedWindowId)
    {
        UE_LOG(LogTurn, Warning,
            TEXT("[PIP] RejectCommand: WindowId mismatch (Expected=%d, Got=%d)"),
            CurrentAcceptedWindowId, Command.WindowId);
        return false;
    }

    return true;
}
```

*   重要:

    *   `ValidateCommand` は `TurnId` / `WindowId` / `bInputWindowOpen` のチェックに **責務を集中**させる。
    *   WindowId を自分でインクリメントしたり、変更したりしてはならない。

##### 4-3-5. コマンド受付の例

```cpp
void UPlayerInputProcessor::ProcessPlayerCommand(const FPlayerCommand& Command)
{
    if (!ValidateCommand(Command))
    {
        return;
    }

    // ここで PendingCommands に積む / 直ちに MovePrecheck に回すなど
    PendingCommands.Add(Command);
}
```

#### 4-4. `APlayerControllerBase`（クライアント側）

##### 4-4-1. 責務

*   レプリケートされた `UTurnFlowCoordinator` から `TurnId` / `InputWindowId` を読み取り、
    クライアント側で生成する `FPlayerCommand` に正しい値を設定する。
*   WindowId をクライアント側で勝手に変更しない。

##### 4-4-2. コマンド生成の例（移動）

```cpp
void APlayerControllerBase::Input_Move_Triggered(/* params */)
{
    if (!TurnFlowCoordinator)
    {
        return;
    }

    FPlayerCommand Cmd;
    Cmd.TurnId   = TurnFlowCoordinator->GetCurrentTurnId();
    Cmd.WindowId = TurnFlowCoordinator->GetCurrentInputWindowId();

    // ここでアクション種別・座標などを詰める
    // Cmd.ActionType = EPlayerActionType::Move;
    // Cmd.TargetCell = ...;

    Server_SubmitCommand(Cmd);
}
```

##### 4-4-3. 注意点

*   既存の `CachedTurnManager->GetCurrentInputWindowId()` 参照がある場合、
    *   すべて `TurnFlowCoordinator->GetCurrentInputWindowId()` に置き換える。
*   コマンド生成時に、必ず `TurnId` / `WindowId` の両方をセットする。

### 5. 1ターン中のシーケンス（WindowId 観点）

1.  **ターン開始（サーバ）**

    *   `UTurnFlowCoordinator` が `TurnId` をインクリメント（タイミングは既存仕様に従う）。
    *   AI フェーズなどを処理。
2.  **プレイヤー入力フェーズ開始（サーバ）**

    *   `AGameTurnManagerBase::OpenInputWindow()` を呼び出す。
    *   内部で `TurnFlowCoordinator->OpenNewInputWindow()` が呼ばれ、`InputWindowId++`。
    *   `PlayerInputProcessor->OpenInputWindow(CurrentTurnId, CurrentWindowId)` 呼び出し。
3.  **クライアントでの同期**

    *   `TurnFlowCoordinator.InputWindowId` のレプリケーションが更新される。
    *   プレイヤー UI は「入力受付中」状態を表示してもよい。
4.  **プレイヤー入力（クライアント）**

    *   `APlayerControllerBase` が入力イベントを受け、
        *   `TurnId = TFC->GetCurrentTurnId()`
        *   `WindowId = TFC->GetCurrentInputWindowId()`
            を含む `FPlayerCommand` を構築し、サーバへ送信。
5.  **コマンド検証（サーバ）**

    *   `UPlayerInputProcessor::ProcessPlayerCommand` が `ValidateCommand` を通して検証。
    *   条件を満たすコマンドだけが Pending キューへ格納される。
6.  **MovePrecheck 成功（サーバ）**

    *   必要なプレイヤー入力がそろい、MovePrecheck が成功。
    *   `UPlayerInputProcessor::CloseInputWindow()` を呼び出し、以降のコマンドを拒否。
    *   `AGameTurnManagerBase` が次フェーズ（Simultaneous / Sequential 処理）へ遷移。
7.  **次ターンへ（サーバ）**

    *   `TurnId++` → 再び 1 に戻る。

### 6. 実装タスク・チェックリスト

#### 6-1. `UTurnFlowCoordinator`

*   [ ] `InputWindowId` フィールドを定義し、レプリケート設定を行う。
*   [ ] `OpenNewInputWindow()` を実装し、`InputWindowId++` をここに集約。
*   [ ] `GetCurrentInputWindowId()` / `GetCurrentTurnId()` を公開。
*   [ ] 他クラスが `InputWindowId` を直接書き換えていないことを確認。

#### 6-2. `AGameTurnManagerBase`

*   [ ] `CurrentInputWindowId` 等、WindowId を自前で保持するフィールドを削除。
*   [ ] `OpenInputWindow()` 内の `CurrentInputWindowId++` を削除。
*   [ ] `OpenInputWindow()` で `TurnFlowCoordinator->OpenNewInputWindow()` を呼ぶよう修正。
*   [ ] `PlayerInputProcessor->OpenInputWindow(CurrentTurnId, CurrentWindowId)` の呼び出しに統一。
*   [ ] ログを `TurnId` / `WindowId` を前提とした形に整理。

#### 6-3. `UPlayerInputProcessor`

*   [ ] `OpenInputWindow(int32 TurnId, int32 WindowId)` シグネチャに変更。
*   [ ] `CloseInputWindow()` を実装（既存があれば挙動を確認）。
*   [ ] `ValidateCommand()` を「TurnId / WindowId / bInputWindowOpen」の検証に集中させる。
*   [ ] PIP 内で WindowId を変更している箇所がないか確認し、あれば削除。
*   [ ] MovePrecheck 成功時に `CloseInputWindow()` を呼ぶフローを確認／整備。

#### 6-4. `APlayerControllerBase`

*   [ ] `FPlayerCommand` 生成時に `TurnId` / `WindowId` を `TurnFlowCoordinator` から取得する実装を確認。
*   [ ] `CachedTurnManager->GetCurrentInputWindowId()` など、GTM から WindowId を取っているコードがあれば削除 or 置き換え。
*   [ ] すべてのコマンド生成パス（移動、攻撃、キャンセルなど）で `TurnId` / `WindowId` が正しくセットされることを確認。

---

## Priority 2: 競合解決（ConflictResolver）の「契約」をコードで保証し、インテント消失と不定挙動を防ぐ

### 1. 目的とスコープ

### 1-1. 目的

*   **インテントがサイレントに消える問題を構造的に不可能にする。**
*   `UConflictResolverSubsystem` の振る舞いを「仕様」として明文化し、

    *   将来のリファクタリングや機能追加で壊れにくくする
    *   バグ発生時に「契約違反」として即座に検知できるようにする
*   Sequential / Simultaneous それぞれでの競合ルール（攻撃優先・セルロックなど）を、
    コードとドキュメントの両方で揃える。

### 1-2. スコープ

*   対象コンポーネント

    *   `UConflictResolverSubsystem`
    *   `FReservationEntry`（予約エントリ／インテントの ConflictResolver レベル表現）
    *   `FResolvedAction`（解決済みアクション）
    *   `UTurnCorePhaseManager::CoreResolvePhase`（呼び出し元）
*   対象外（前提として扱う）

    *   AI インテント生成（EnemyThinker 等）
    *   プレイヤー入力／WindowId（Priority 1）
    *   攻撃フェーズの実行 (`UAttackPhaseExecutorSubsystem` など)
        → ただし ConflictResolver から見る「ATTACK インテント／アクション」の扱いは仕様に含める。

---

### 2. 用語・データモデル

### 2-1. 用語

*   **Reservation (予約)**

    *   ある Actor が「このターンにこのセルへ MOVE/ATTACK/WAIT したい」という要求。
    *   `FReservationEntry` で表現される。
*   **ResolvedAction (解決済みアクション)**

    *   ConflictResolver が競合を解決した結果としての「最終アクション」。
    *   `FResolvedAction` で表現される。
*   **TurnMode**

    *   `Sequential` または `Simultaneous`。
    *   通常は `AGameTurnManagerBase` で決定され、`bSequentialAttackMode` として ConflictResolver に伝達される。

### 2-2. データモデル（仕様レベル）

※名前は一部推測ですが、「こういうフィールドがある」と想定して仕様化します。

#### FReservationEntry

*   最低限持つべきフィールド（仕様）:

    *   `AActor* Actor;`

        *   インテントの主体。
    *   `FIntPoint Cell;` or `FGridCell Cell;`

        *   この予約が関わるセル（通常は「目的地」）。
    *   `EIntentType IntentType;`

        *   `Move`, `Attack`, `Wait` など。
    *   `int32 BasePriority;`

        *   同じセルを争うときの優先度比較用。
    *   （任意）`int32 TurnId;`, `int32 WindowId;`

        *   呼び出し元で持つなら、ここに保持してもよい。

#### FResolvedAction

*   最低限持つべきフィールド（仕様）:

    *   `AActor* Actor;`
    *   `FIntPoint CurrentCell;`
    *   `FIntPoint NextCell;`
    *   `EResolvedActionType ActionType;`

        *   `Move`, `Attack`, `Wait`。
    *   `FString ResolutionReason;`

        *   なぜこの結果になったかの説明文字列。
    *   （任意）`int32 TurnId;`, `int32 WindowId;`

        *   元の Reservation と一致させる。

---

### 3. 不変条件（Invariants）

ConflictResolver が **常に満たすべきルール** をここで定義します。

#### 3-1. Invariant 1: インテント不消失

> **入力された Reservation 数と、出力される ResolvedAction 数は常に等しい。**

*   `NumReservations == NumResolvedActions`
*   どのような競合パターンでも、**インテントがサイレントに消えることはない**。
*   実装では、`ResolveAllConflicts` の末尾で `checkf` で保証する。

#### 3-2. Invariant 2: Actor につき 1 アクション（同ターン）

*   同一 `TurnId` / `WindowId` 内では、**1 Actor あたり最終的な `FResolvedAction` は 1 つだけ**とする。
*   基本前提：

    *   `UTurnCorePhaseManager` / 上位レイヤーは、1 Actor に対して 1 つの `FReservationEntry` だけを渡すようにする。
        （もし複数渡す運用にするなら、どれを採用してどれを WAIT にするかを ConflictResolver 仕様に追記する必要がある）

#### 3-3. Invariant 3: Turn 情報の保全

*   各 `FResolvedAction` は、対応する `FReservationEntry` と同じ `TurnId` / `WindowId` を持つ（もしくは、呼び出し元がそうみなせる）。
*   これにより、「異なるターンのアクションが混ざる」ことを防ぐ。

#### 3-4. Invariant 4: ResolutionReason の完全設定

*   すべての `FResolvedAction` は **非空の `ResolutionReason` を持つ**。
*   特に `WAIT` に変換されたアクションでは、「なぜ WAIT になったか」が必ず分かるコメント文字列を持つ。

#### 3-5. Invariant 5: Loser-Stays Principle (敗者は留まる原則)

*   CONFLICT で敗れたユニットは、そのターン中は `CurrentCell` に留まる。
*   そのセルは同ターン内に他ユニットの MOVE 先として使用できない。
*   ConflictResolver は、楽観的な勝者決定後に WAIT になった Stationary セルを再評価し、
*   Pass 3 の再検証フェーズで Stationary セルを狙う MOVE を WAIT に変換し、その理由を記録することでこの原則を保証する。

---

### 4. モード別の挙動仕様（Sequential / Simultaneous）

### 4-1. 共通ルール

*   任意のセルに対して、**同時に成立するアクションは 1 つだけ**。
*   同一セルを複数の Reservation が争う場合:

    *   `ActionTier`（ATTACK/MOVE/WAIT の優先度）
    *   `BasePriority`
    *   その他の規則（例: 元位置優先）
        に基づいて勝者を 1 つだけ選び、敗者は `WAIT` に変換する。

### 4-2. Sequential モード

#### 4-2-1. 定義

*   そのターンに少なくとも 1 つの ATTACK インテントが存在する場合、`Sequential` モードとする。
*   `AGameTurnManagerBase` が事前に判定し、`bSequentialAttackMode=true` として ConflictResolver に渡す。

#### 4-2-2. 攻撃者セルのロック

*   攻撃インテントを持つ Actor の **現在セル**（攻撃元のセル）は、
    MOVE インテントに対して「ロック」される。
*   仕様として：

    *   同一セルに対して ATTACK と MOVE が存在する場合、

        *   ATTACK が必ず勝ち、MOVE は `WAIT` に変換される。
    *   この規則は「Sequential モードでは攻撃者の現在マスを MOVE より優先する（事実上ロックする）」とコメントで明示する。

#### 4-2-3. ATTACK 同士の競合

*   同一セルに複数 ATTACK が飛び込むケース（例: 相互殴り合い）については、次のいずれかで仕様を定める：

    *   ケース A: **全て成立**

        *   同一セルで複数の攻撃が同時成立してよい世界観なら、そのまま並列に AttackPhaseExecutor に渡す。
    *   ケース B: **1 つだけ勝者を選ぶ**

        *   `BasePriority` または別の優先度規則により 1 つだけ Attack を成立させ、他は WAIT に変換。
*   現在のゲームデザインに合わせて、どちらを**仕様として採用するか決めてコメント化**する。

#### 4-2-4. 出力アクションの意味

*   `ActionType == Attack`:

    *   主に Occupancy や「ロック情報」として扱う。
    *   実際のダメージ適用／エフェクト再生は `UAttackPhaseExecutorSubsystem` など別フェーズが担当する。
*   `ActionType == Move`:

    *   Sequential モードでも、最終的には「攻撃フェーズ後の移動フェーズ」で実行される。
*   `ActionType == Wait`:

    *   MOVE/ATTACK インテントが競合などで不成立となった結果。
    *   `ResolutionReason` で理由を明示する。

### 4-3. Simultaneous モード

#### 4-3-1. 定義

*   そのターンに ATTACK インテントが 1 つも存在しない場合、`Simultaneous` モードとする。
*   `bSequentialAttackMode=false` が ConflictResolver に渡される。

#### 4-3-2. MOVE 競合ルール

*   すべての Reservation が MOVE または WAIT である前提。
*   同一セルを複数の MOVE が狙う場合:

    *   `BasePriority`（および必要に応じて他の規則）に基づき 1 つだけ MOVE を成立させる。
    *   その他の Reservation は、すべて `WAIT` に変換。
*   `ResolutionReason`：

    *   勝者: `"Success: Move"` など。
    *   敗者: `"LostConflict: Cell occupied by higher priority move"` など。

#### 4-3-3. 出力アクションの意味

*   Simultaneous モードでは、`ResolvedAction` は MOVE / WAIT のみになるのが理想。
*   Result:

    *   MOVE: 同時に一斉に実行する。
    *   WAIT: その場に留まり、何もしない。

---

### 5. `UConflictResolverSubsystem` API 仕様

### 5-1. シグネチャ（仕様）

```cpp
TArray<FResolvedAction> UConflictResolverSubsystem::ResolveAllConflicts(
    const TArray<FReservationEntry>& Reservations,
    bool bSequentialAttackMode
);
```

*   `Reservations`:

    *   当該ターンの全ユニット（敵／味方／プレイヤー）の予約一覧。
*   `bSequentialAttackMode`:

    *   `true` の場合、Sequential モードルール（攻撃者セルロック含む）を適用。
    *   `false` の場合、Simultaneous モードルール（MOVE 同士のみ）を適用。

### 5-2. 関数冒頭の処理

```cpp
const int32 NumReservations = Reservations.Num();

UE_LOG(LogTurnConflict, Verbose,
       TEXT("ResolveAllConflicts: NumReservations=%d, bSequentialAttackMode=%d"),
       NumReservations, bSequentialAttackMode);
```

*   ここで `NumReservations` をキャプチャし、最後の Invariant チェックに利用する。

### 5-3. 関数末尾の契約チェック

```cpp
const int32 NumResolved = ResolvedActions.Num();

checkf(NumReservations == NumResolved,
       TEXT("ConflictResolver contract violation: Input reservations (%d) != Output actions (%d)"),
       NumReservations, NumResolved);
```

*   開発ビルドでは `checkf`。
*   もしリリースビルドでクラッシュを避けたい場合は `ensureMsgf` としてログ＋安全なフォールバックを検討してもよい。

---

### 6. ResolutionReason 利用ルール

### 6-1. 目的

*   「なぜこのユニットが WAIT したのか」「なぜこの MOVE が通ったのか」を、ログから一目で分かるようにする。
*   デバッグ時に、ConflictResolver の内部状態を追いかけなくても原因を理解できるようにする。

### 6-2. 設定ポリシー

#### 成功パス

*   MOVE が成立した場合：

    ```cpp
    Action.ActionType       = EResolvedActionType::Move;
    Action.ResolutionReason = TEXT("Success: Move");
    ```

*   ATTACK が成立した場合：

    ```cpp
    Action.ActionType       = EResolvedActionType::Attack;
    Action.ResolutionReason = TEXT("Success: Attack");
    ```

#### 競合敗者 → WAIT

*   セル競合で負けた MOVE：

    ```cpp
    Action.ActionType       = EResolvedActionType::Wait;
    Action.ResolutionReason = TEXT("LostConflict: Cell occupied by higher tier");
    ```

*   Sequential モードで攻撃者セルに対する MOVE が弾かれた場合：

    ```cpp
    Action.ActionType       = EResolvedActionType::Wait;
    Action.ResolutionReason = TEXT("LostConflict: Cell locked by attacker (Sequential)");
    ```

#### その他の WAIT

*   目的地がそもそも無効（壁／マップ外など）で WAIT になった場合（もしフェーズ3で判定するなら）：

    ```cpp
    Action.ActionType       = EResolvedActionType::Wait;
    Action.ResolutionReason = TEXT("Invalid: Destination blocked or out of bounds");
    ```

---

### 7. 実装タスク・チェックリスト（Priority 2）

### 7-1. `UConflictResolverSubsystem::ResolveAllConflicts`

*   [ ] 関数冒頭で `NumReservations` を取得し、ログに出す。
*   [ ] 関数末尾で `NumReservations == ResolvedActions.Num()` を `checkf` で保証する。
*   [ ] Sequential / Simultaneous の分岐ロジックが仕様通りか確認し、

    *   攻撃者セルロック
    *   MOVE 同士の競合解決
        が期待通りに実装されているかを確認・補強。
*   [ ] すべてのコードパスで `ResolutionReason` が必ず設定されるようにする。

### 7-2. コメント・ドキュメント

*   [ ] `ResolveAllConflicts` のヘッダコメントに、次を明記：

    *   「入力予約数と出力アクション数が常に一致する」契約
    *   Sequential / Simultaneous での違い
    *   攻撃者セルロックの仕様
*   [ ] `bSequentialAttackMode` を利用している分岐の近くに、

    *   「Sequential モードでは攻撃者の現在セルを MOVE より優先し、事実上ロックする」
        といった説明コメントを追加。

### 7-3. 呼び出し元 (`UTurnCorePhaseManager::CoreResolvePhase`)

*   [ ] `Reservations` 引数に「そのターンの全インテント」が渡っていることを確認。
*   [ ] プレイヤーインテント統合後も、**1 Actor につき 1 Reservation** であることを前提とする場合、そのルールをコメントに明記。
*   [ ] 将来的に仕様変更（Actor が複数 Reservation を持つ）をするなら、その場合の扱いを別途仕様定義する。

---

### 8. テスト観点（Priority 2 完了条件）

実装が仕様どおり動いているかを確認するための代表シナリオです。

1.  **MOVE vs MOVE（同一セル）**

    *   2 体のユニットが同じセルへ MOVE インテントを出す。
    *   結果：

        *   どちらか片方だけが `ActionType=Move`・`ResolutionReason="Success: Move"`。
        *   もう一方は `ActionType=Wait`・`ResolutionReason` に `LostConflict` 系の理由が入る。
    *   `NumReservations == NumResolvedActions` が満たされている。

2.  **ATTACK vs MOVE（Sequential モード）**

    *   あるセルを、攻撃者 A が ATTACK、別ユニット B が MOVE で狙う。
    *   `bSequentialAttackMode=true` で解決。
    *   結果：

        *   A: `ActionType=Attack`・`ResolutionReason="Success: Attack"`.
        *   B: `ActionType=Wait`・`ResolutionReason` に `Cell locked by attacker` 系のメッセージ。
    *   MOVE 側が絶対にそのセルに入らないことを確認。

3.  **ATTACK が存在しないターン（Simultaneous モード）**

    *   すべての Reservation が MOVE のターン。
    *   `bSequentialAttackMode=false`。
    *   結果：

        *   いずれのセルでも、同一セルに対して成立する MOVE は高優先度ユニット 1 つのみ。
        *   それ以外は WAIT＋適切な `ResolutionReason`。

4.  **Reservations=0 / 1 / N**

    *   予約が 0 件、1 件、複数件のケースで、

        *   常に `NumReservations == NumResolvedActions` が満たされる。
        *   `ResolutionReason` が全アクションに設定されている。

---

## Priority 2.1: パッチ - 「敗者は留まる」原則による競合解決の完全化

### 根本原因
`ConflictResolver` は、「移動インテントを提出したが、他の場所の競合に負けて**動けなくなった**ユニット」がブロッカーになることを考慮していなかった。これにより、そのユニットが占有するセルへの移動が不正に許可され、`GridOccupancy` で移動が拒否されるエラーが発生していた。

### 新仕様
この問題を解決するため、`ConflictResolver` のロジックを「インテント」ではなく「競合解決の**結果**」に基づいて再検証する、**3パス（Three-Pass）方式**に修正する。

### 実装タスク

1.  **`ResolveAllConflicts` のロジックを以下のように変更します。**

    ---

    **Pass 1: 楽観的（Optimistic）競合解決**

    *   現在の `ResolveAllConflicts` のロジック（`StationaryBlockers` チェック**以外**）を実行します。
    *   `bSequentialAttackMode` に基づく攻撃優先処理や、`BasePriority` に基づく勝者選択を行います。
    *   **勝者（MOVE/ATTACK）**と**敗者（WAIT）**を含む `TArray<FResolvedAction> OptimisticActions` を生成します。
    *   （この時点では、`REJECT UPDATE` の原因となる不正な `MOVE` アクションがまだ含まれています。）

    ---

    **Pass 2: 静的ブロッカー（敗者）の特定**

    *   `OptimisticActions` をスキャンし、「最終的に動かないユニット」が占有するセルのセットを作成します。

    ```cpp
    TSet<FIntPoint> StationaryCells;
    for (const FResolvedAction& Action : OptimisticActions)
    {
        if (Action.bIsWait) // (WAITインテント、または競合敗者)
        {
            // このユニットは動かないため、その「現在地」を
            // 静的ブロッカーとして登録します。
            StationaryCells.Add(Action.CurrentCell);
        }
    }
    ```

    ---

    **Pass 3: 再検証（Pessimistic Re-validation）**

    *   `OptimisticActions` を**再度**イテレートし、最終的な `ResolvedActions` リストを構築します。

    ```cpp
    TArray<FResolvedAction> FinalActions;
    FinalActions.Reserve(OptimisticActions.Num());

    for (FResolvedAction Action : OptimisticActions)
    {
        // 既にWAIT（敗者）のアクションはそのまま追加
        if (Action.bIsWait)
        {
            FinalActions.Add(Action);
            continue;
        }

        // 勝者（MOVE/ATTACK）のアクションを検証
        FIntPoint TargetCell = Action.NextCell;

        // 攻撃アクションは移動しないため、常に有効
        if (Action.AbilityTag.MatchesTag(RogueGameplayTags::AI_Intent_Attack))
        {
            FinalActions.Add(Action);
            continue;
        }

        // MOVEアクションの検証:
        // 目的地が「動かないユニット」のセルと衝突していないか？
        if (StationaryCells.Contains(TargetCell))
        {
            // ★★★ 衝突！ ★★★
            // この「勝者」は、実際には「敗者」のセルに
            // 移動しようとしていたため、WAITに変換します。

            UE_LOG(LogConflictResolver, Warning, 
                TEXT("[ConflictResolver] Move REJECTED: %s's move to (%d,%d) blocked by stationary unit (Pass 3)"),
                *GetNameSafe(Action.Actor.Get()), TargetCell.X, TargetCell.Y);

            // WAITに変換
            Action.bIsWait = true;
            Action.NextCell = Action.CurrentCell;
            Action.FinalAbilityTag = RogueGameplayTags::AI_Intent_Wait;
            Action.ResolutionReason = FString::Printf(
                TEXT("LostConflict: Target cell (%d,%d) blocked by stationary unit (Pass 3)"),
                TargetCell.X, TargetCell.Y);
        }

        FinalActions.Add(Action);
    }

    // 契約チェック (NumReservations == FinalActions.Num()) を実行
    // ...

    return FinalActions;
    ```

---

## Priority 3: ターンモード（Sequential / Simultaneous）決定と利用の責務を一元化する

### 1. 目的とスコープ

### 1-1. 目的

*   ターンごとのモード（`Sequential` / `Simultaneous`）を **一箇所で決定**し、全サブシステムがその決定に従う構造にする。
*   「どのインテントが `Attack` と見なされるか」「どのようにモードが決まるか」を明確化し、

    *   予測不能なモード切り替え
    *   コンポーネント間のモード判定の重複・矛盾
        を防止する。
*   `Sequential` / `Simultaneous` の振る舞い（Attack フェーズ／Move フェーズ）が、コードとドキュメントの両方で揃った状態にする。

### 1-2. スコープ

*   対象コンポーネント

    *   `AGameTurnManagerBase`（GTM）
    *   `UTurnCorePhaseManager`（TCPM）
    *   `UAttackPhaseExecutorSubsystem`
    *   `UConflictResolverSubsystem`（モードフラグの利用側として）
    *   `UEnemyAISubsystem` / `UEnemyTurnDataSubsystem`（AI インテント供給側）
    *   `UPlayerInputProcessor` / `FPlayerCommand`（プレイヤーインテント供給側）
*   対象となる機能

    *   ターン開始〜インテント収集〜モード決定〜フェーズ実行の流れ
    *   モード決定の入力（どのインテントを見て判断するか）
    *   モード情報の保持・参照の責務分担

---

### 2. 用語・データモデル

### 2-1. 用語

*   **TurnMode**

    *   `Sequential` または `Simultaneous` の 2 値。
    *   1 ターンの間は不変。
*   **Intent（インテント）**

    *   ユニットが「このターンでやりたいこと」を表すデータ。
    *   敵 AI: `FEnemyIntent`
    *   プレイヤー: `FPlayerCommand` → `FEnemyIntent` 形式へ変換して統一する。
*   **Attack Intent**

    *   `IntentType == Attack` と判定されるインテント。
    *   敵 AI の攻撃行動、およびプレイヤーの攻撃行動が含まれる。

### 2-2. データモデル（仕様）

#### FEnemyIntent（Canonial Intent 型として扱う）

*   必須フィールド（仕様レベル）

    *   `AActor* Actor;`
    *   `FIntPoint CurrentCell;`
    *   `FIntPoint TargetCell;`
    *   `EAIIntentType IntentType;`

        *   `Move`, `Attack`, `Wait` 等
    *   `int32 BasePriority;`
*   敵 AI だけでなく、**プレイヤーや味方のインテントも最終的にはこの形式に変換して扱う**。

#### FPlayerCommand

*   Priority 1 仕様を前提として、以下を含む:

    *   `int32 TurnId;`
    *   `int32 WindowId;`
    *   `EPlayerActionType ActionType;`（移動 / 攻撃 / スキルなど）
    *   `FIntPoint TargetCell;` など
*   Priority 3 では、`GameTurnManager` がこれを `FEnemyIntent` に変換して利用する。

---

### 3. 不変条件（Invariants）

### 3-1. Invariant 1: TurnMode の SSOT（Single Source of Truth）

*   `TurnMode`（Sequential / Simultaneous）の唯一の権威は **`AGameTurnManagerBase`** である。
*   `UTurnCorePhaseManager` / `UConflictResolverSubsystem` / `UAttackPhaseExecutorSubsystem` は、

    *   `TurnMode` を自分で判定してはならない。
    *   `GameTurnManager` が決定した値のみを参照する。

### 3-2. Invariant 2: Turn 内不変

*   1 ターン中に `TurnMode` が途中で変わることはない。

    *   ターン開始時（正確には「全インテント収集後」）に一度だけ決定され、そのターンの終了まで固定。

### 3-3. Invariant 3: モード決定の入力

*   `TurnMode` は、そのターンに存在する **すべてのインテント（敵＋プレイヤー＋味方等）** に基づき決定される。
*   少なくとも 1 つの「攻撃インテント」が存在すれば `Sequential`、そうでなければ `Simultaneous`。

---

### 4. TurnMode 決定仕様

### 4-1. モード決定のタイミング

1.  敵 AI インテント生成（CoreThinkPhase）
2.  プレイヤーコマンドの受付完了（入力ウィンドウクローズ）
3.  `GameTurnManager` が「そのターンの全インテント」を確定
4.  その時点で `TurnMode` を決定
5.  決定された `TurnMode` に基づきフェーズ実行に進む

### 4-2. インテント収集と統合

`AGameTurnManagerBase` は次の 2 系統からインテントを収集する。

1.  **敵インテント**

    *   `UEnemyAISubsystem` / `UEnemyTurnDataSubsystem` から取得される `TArray<FEnemyIntent>`。
    *   ここにはすでに AI の MOVE/ATTACK インテントが入っている。

2.  **プレイヤーインテント**

    *   `UPlayerInputProcessor` が保持する `FPlayerCommand` 群。
    *   これを `GameTurnManager` 内で `ConvertPlayerCommandToIntent` のようなヘルパー関数で `FEnemyIntent` に変換する。

最終的に、`AGameTurnManagerBase` は 1 つの統合されたインテント配列を持つ：

```cpp
TArray<FEnemyIntent> AllIntents; // 敵 + プレイヤー (+ 味方など)
```

### 4-3. Attack Intent の判定

*   `AllIntents` 中の任意のインテントについて、

    *   `Intent.IntentType == EAIIntentType::Attack`
        と判定されるものが 1 つでも存在すれば、そのターンは `Sequential` モードとする。

判定関数の例：

```cpp
bool AGameTurnManagerBase::DoesAnyIntentHaveAttack(const TArray<FEnemyIntent>& Intents) const
{
    for (const FEnemyIntent& Intent : Intents)
    {
        if (Intent.IntentType == EAIIntentType::Attack)
        {
            return true;
        }
    }
    return false;
}
```

### 4-4. TurnMode の決定と保存

*   判定後、`AGameTurnManagerBase` は内部フィールドとして TurnMode を保持する：

```cpp
UENUM()
enum class ETurnMode : uint8
{
    Simultaneous,
    Sequential
};

ETurnMode CurrentTurnMode;
```

*   判定ロジック例：

```cpp
void AGameTurnManagerBase::DetermineTurnMode()
{
    // AllIntents は、この時点で「そのターンの全インテント」が入っている前提
    const bool bHasAttack = DoesAnyIntentHaveAttack(AllIntents);

    CurrentTurnMode = bHasAttack ? ETurnMode::Sequential
                                 : ETurnMode::Simultaneous;

    UE_LOG(LogTurn, Log,
        TEXT("[GTM] DetermineTurnMode: TurnId=%d, Mode=%s, NumIntents=%d"),
        TurnFlowCoordinator->GetCurrentTurnId(),
        CurrentTurnMode == ETurnMode::Sequential ? TEXT("Sequential") : TEXT("Simultaneous"),
        AllIntents.Num());
}
```

*   取得用 API:

```cpp
bool AGameTurnManagerBase::IsSequentialModeActive() const
{
    return CurrentTurnMode == ETurnMode::Sequential;
}
```

---

### 5. フェーズ実行仕様（Sequential / Simultaneous）

### 5-1. Simultaneous モードのフェーズ構造

**目的:** 攻撃が含まれないターンでは、すべての MOVE/WAIT を一斉に解決・実行する。

1.  `AllIntents`（敵＋プレイヤー）から、Move/Wait 系のインテントを Reservation に変換
2.  `UTurnCorePhaseManager::CoreResolvePhase` を呼び出し、`FResolvedAction` のリストを得る
3.  `UConflictResolverSubsystem` の結果に基づき、`DispatchResolvedMove` で全 MOVE/WAIT を実行

擬似コード:

```cpp
void AGameTurnManagerBase::ExecuteSimultaneousPhase()
{
    check(CurrentTurnMode == ETurnMode::Simultaneous);

    // 1. AllIntents を TCPM に渡して解決
    TArray<FResolvedAction> ResolvedActions =
        TurnCorePhaseManager->CoreResolvePhase(AllIntents, /*bSequentialAttackMode=*/false);

    // 2. 解決済みの MOVE/WAIT を全てディスパッチ
    for (const FResolvedAction& Action : ResolvedActions)
    {
        DispatchResolvedMove(Action); // WAIT の場合は何もしない、など
    }
}
```

### 5-2. Sequential モードのフェーズ構造

**目的:** 攻撃が含まれるターンでは、

*   まず ATTACK フェーズを専用のサブシステムで処理し、
*   その後に MOVE/WAIT を解決・実行する。

1.  `AllIntents` から ATTACK 系インテントを抽出 → AttackPhaseExecutor に渡す
2.  攻撃フェーズ完了（Barrier 等で同期）
3.  MOVE/WAIT 系インテントを Reservation に変換
4.  `CoreResolvePhase` → `ResolvedActions` を得る
5.  MOVE/WAIT のみ抽出して `DispatchResolvedMove` で実行

擬似コード例：

```cpp
void AGameTurnManagerBase::ExecuteSequentialPhase()
{
    check(CurrentTurnMode == ETurnMode::Sequential);

    // 1. Attack intents 抽出
    TArray<FEnemyIntent> AttackIntents;
    TArray<FEnemyIntent> MoveIntents;

    for (const FEnemyIntent& Intent : AllIntents)
    {
        if (Intent.IntentType == EAIIntentType::Attack)
        {
            AttackIntents.Add(Intent);
        }
        else
        {
            MoveIntents.Add(Intent);
        }
    }

    // 2. Attack フェーズ実行
    AttackPhaseExecutor->ExecuteAttacks(AttackIntents); // 既存の AttackPhaseExecutorSubsystem を利用
    // Barrier / Ability 完了待ち（既存実装どおり）

    // 3. Move/Wait のみを対象に ConflictResolver で解決
    TArray<FResolvedAction> ResolvedActions =
        TurnCorePhaseManager->CoreResolvePhase(MoveIntents, /*bSequentialAttackMode=*/true);

    // 4. MOVE/WAIT アクションをディスパッチ
    for (const FResolvedAction& Action : ResolvedActions)
    {
        DispatchResolvedMove(Action);
    }
}
```

※実装上は、現在の `ExecuteSequentialPhase()` の構成（AttackPhaseExecutor + CachedResolvedActions の利用）を「正」とし、その挙動を上記仕様としてドキュメント化する方針で問題ありません。

---

### 6. コンポーネント別の責務整理（TurnMode 観点）

### 6-1. AGameTurnManagerBase

*   責務:

    *   全インテント収集（敵＋プレイヤー）
    *   インテント統合 → `AllIntents`
    *   `TurnMode` の決定と保持（SSOT）
    *   フェーズの分岐呼び出し

        *   `ExecuteSimultaneousPhase()`
        *   `ExecuteSequentialPhase()`

*   禁止事項:

    *   部分的なインテントだけを見てモードを決めないこと。

        *   例: 敵インテントだけで判定、プレイヤー攻撃を無視、など。

### 6-2. UTurnCorePhaseManager

*   責務:

    *   与えられたインテントリスト（通常 `AllIntents` または MoveIntents）を Reservation に変換し、`UConflictResolverSubsystem` に渡す。
    *   `bSequentialAttackMode` フラグは `AGameTurnManagerBase::IsSequentialModeActive()` の結果を受け取るだけ。

*   禁止事項:

    *   自分で TurnMode を決定・推測しない。

### 6-3. UConflictResolverSubsystem

*   Priority 2 の仕様を前提としつつ、

    *   `bSequentialAttackMode`（= `TurnMode == Sequential`）を元に、ロジック分岐を行うのみ。
*   禁止事項:

    *   「攻撃インテントがあるかどうか」を再計算して TurnMode を推測しない。

### 6-4. UAttackPhaseExecutorSubsystem

*   責務:

    *   `GameTurnManager` から渡された ATTACK インテント群を処理する。
    *   `TurnMode` を変更しない。
*   禁止事項:

    *   自身で TurnMode を変更したり、Simultaneous モードに切り替えたりしない。

### 6-5. UPlayerInputProcessor / FPlayerCommand

*   責務:

    *   Priority 1 に基づき、正しい `TurnId` / `WindowId` のコマンドのみを確定させる。
    *   Attack / Move など、プレイヤーの「行動意思」を `FPlayerCommand` として保持する。

*   Priority 3 における役割:

    *   `AGameTurnManagerBase` による `FPlayerCommand -> FEnemyIntent` 変換の入力源。

---

### 7. 実装タスク・チェックリスト（Priority 3）

### 7-1. インテント統合

*   [ ] `AGameTurnManagerBase` に `TArray<FEnemyIntent> AllIntents` を導入する。
*   [ ] 敵インテント収集後、プレイヤーの `FPlayerCommand` を `FEnemyIntent` に変換し、`AllIntents` に追加。
*   [ ] 変換ヘルパー関数例:

    *   `FEnemyIntent ConvertPlayerCommandToIntent(const FPlayerCommand&)` を `GameTurnManager` に実装。

### 7-2. モード決定の一元化

*   [ ] `DetermineTurnMode()` 関数を `AGameTurnManagerBase` に追加し、**必ず `AllIntents` を入力として使用**。
*   [ ] `CurrentTurnMode` フィールド（`ETurnMode`）を追加し、そのターン中は不変とする。
*   [ ] 既存の `DoesAnyIntentHaveAttack()`（敵インテントのみを見ていた実装）があれば、`AllIntents` を見るように修正。
*   [ ] 他クラスで TurnMode を判定しているロジックがあれば削除し、`IsSequentialModeActive()` のみに依存するよう統一。

### 7-3. Sequential / Simultaneous フローの明文化

*   [ ] `ExecuteSimultaneousPhase()` が

    *   `AllIntents`（もしくは MoveOnlyIntents）を `CoreResolvePhase` に渡し、
    *   得られた `ResolvedActions` を一括で MOVE/WAIT 実行する形になっているか確認。
*   [ ] `ExecuteSequentialPhase()` が

    *   ATTACK と MOVE を分け、
    *   AttackPhaseExecutor → Barrier → CoreResolvePhase(MOVE) → DispatchMove
        という順序で動いているか確認。
*   [ ] 上記のフェーズ構造を関数コメントとして追記。

### 7-4. ログ・デバッグ

*   [ ] ターン開始時に、`TurnId / TurnMode / NumIntents / NumAttackIntents` を 1 行で出力するログを追加。
*   [ ] フェーズ開始時に、`[Phase] SimultaneousMove` / `[Phase] SequentialAttack` などのタグ付きログを追加。

---

## Priority 4: EnemyThinker の純化（インテント表明フェーズの責務を「戦術判断」のみに限定する）

### 1. 目的とスコープ

### 1-1. 目的

*   EnemyThinker（敵 AI）の責務を
    **「戦術レベルの意思決定（何をしたいか）」に限定**し、
    物理・経路・占有情報に関する検証ロジックを完全に排除する。
*   「移動できるか」「そこにユニットがいるか」といった検証は、
    **すべて `UTurnCorePhaseManager` と `UConflictResolverSubsystem` に委譲**する。
*   これにより：

    *   Phase1: 純粋なインテント表明
    *   Phase3: 競合解決＆物理解釈
        という 4 フェーズ設計の前提をコードレベルで保証する。

### 1-2. スコープ

*   対象コンポーネント

    *   `UEnemyThinkerBase` およびその派生クラス
    *   `UEnemyAISubsystem` / `UEnemyTurnDataSubsystem`（EnemyThinker の呼び出し元）
    *   `UTurnCorePhaseManager`（EnemyIntent を Reservation にマッピングする側）

*   対象となるフェーズ

    *   Phase1: Intent Declaration（インテント表明）
    *   Phase3: Conflict Resolution（競合解決）との責務境界

*   対象外（前提として扱う）

    *   Priority 1: WindowId / 入力ウィンドウ管理
    *   Priority 2: ConflictResolver の契約（インテント不消失 etc.）
    *   Priority 3: TurnMode 決定（Sequential / Simultaneous）

---

### 2. 用語と前提

### 2-1. 用語

*   **戦術状態 (Tactical State)**
    敵 AI が意思決定に利用してよい情報。

    *   自身の位置・HP・ステータス
    *   視界内または認識しているプレイヤー・味方・他の敵の位置／タグ
    *   事前計算済みの DistanceField / ThreatMap / InterestMap など
    *   自身の「役割」（近接アタッカー / 遠距離 / サポート etc.）

*   **物理状態 (Physical State)**
    「現在そのセルに誰がいるか」「通行可能か」「ターン内で誰がどこへ移動予約しているか」などの情報。

    *   Occupancy
    *   Collision / Blocker
    *   他ユニットの予約情報
    *   MovePrecheck のような物理的可否判定

*   **理想インテント (Ideal Intent)**
    敵 AI が「本来こう動きたい／攻撃したい」と考える行動意思。
    実行可能かは考慮しない。

### 2-2. 前提

*   4 フェーズ設計：

    1.  Intent Declaration（EnemyThinker）
    2.  Intent Collection & Mode Determination（GameTurnManager）
    3.  Conflict Resolution（TurnCorePhaseManager + ConflictResolver）
    4.  Resolved Action Execution（GameTurnManager）
*   Priority 1〜3 の仕様が満たされていること：

    *   WindowId / TurnId が安定している
    *   ConflictResolver がインテント不消失を保証している
    *   TurnMode は GameTurnManager が一元的に決定している

---

### 3. EnemyThinker の責務と非責務

### 3-1. 責務（やってよいこと）

EnemyThinker の責務は、**「理想インテントの決定」のみ**。

*   例：

    *   プレイヤーが攻撃射程内にいる → `IntentType = Attack`
    *   プレイヤーに近づきたい → DistanceField の勾配に沿って「行きたい方向」を決定し、`IntentType = Move`・`TargetCell` を設定
    *   この場から動きたくない / 待ち伏せしたい → `IntentType = Wait`
    *   特殊スキルを使いたい → `IntentType = UseAbility`（あれば）

※ここでの「近づきたい」「下がりたい」は純粋に戦術的な意思であり、
そのセルに本当に移動できるかは考慮しない。

### 3-2. 非責務（禁止事項）

EnemyThinker が **やってはいけないこと** を明確にします。

*   Occupancy / 物理状態の直接参照

    *   `IsCellOccupied`, `IsCellWalkable`, `CanMoveTo` 等を呼び出さない。
    *   Turn 内の予約状況（誰がどこへ動こうとしているか）を参照しない。

*   経路検証

    *   「そこへ行けなさそうだから WAIT にする」のような、
        物理的可否に基づく WAIT へのフォールバックを行わない。
    *   Pathfinding ライブラリ（旧 AGridPathfinding / 新 UGridPathfinding）を使って
        「実際に通れるルートだけ選ぶ」ことをしない。

*   ターン・Window の管理

    *   TurnId / WindowId を見て分岐・条件変更しない（これは GameTurnManager / TFC の責務）。

*   状態変更

    *   自身や他ユニットの位置・ステータスを直接変更しない。
    *   GameState に対する書き込みは、インテント生成以外では行わない。

---

### 4. データモデル仕様（FEnemyIntent）

### 4-1. FEnemyIntent の役割

*   EnemyThinker の出力（Phase1 の成果物）としての「正規フォーマット」。
*   敵・プレイヤー・味方問わず、最終的にはこの形に統一して扱う（Priority 3 で定義済み）。

### 4-2. 必須フィールド（仕様レベル）

（名前は実装に合わせて調整可）

```cpp
struct FEnemyIntent
{
    AActor* Actor;                // インテントの主体
    FIntPoint CurrentCell;        // Thinker が認識している自身位置（理想ベースでOK）
    FIntPoint TargetCell;         // 「行きたい／攻撃したい」理想セル
    EAIIntentType IntentType;     // Move / Attack / Wait / UseAbility ...
    int32 BasePriority;           // ConflictResolver に渡す優先度
    FGameplayTag AbilityTag;      // 使用したいアビリティ（Attack 種など）
    // 追加フィールド（任意）
    // float DesireScore;
    // FVector2D PreferredDirection;
};
```

### 4-3. 意図する使い方

*   `CurrentCell` / `TargetCell` は **「理想状態」の座標** でよい。

    *   Occupancy や物理制約によって後で修正される前提。
*   `BasePriority` は、ConflictResolver が MOVE vs MOVE / ATTACK vs ATTACK を比較する際に使用。

    *   例：ボス＞雑魚、プレイヤーに近いユニット＞遠いユニット、など。

---

### 5. EnemyThinker のインタフェース仕様

### 5-1. UEnemyThinkerBase

想定インタフェース例：

```cpp
UCLASS(Abstract)
class UEnemyThinkerBase : public UObject
{
    GENERATED_BODY()

public:
    // 1ターンの思考入り口
    virtual FEnemyIntent DecideIntent(const FEnemyTacticalContext& Context);

protected:
    // 実際の思考ロジックを各派生クラスが実装
    virtual FEnemyIntent ComputeIntent_Implementation(const FEnemyTacticalContext& Context);
};
```

### 5-2. 入力コンテキスト（戦術状態）

`FEnemyTacticalContext`（仮）の中に、**戦術判断に必要な情報のみ**を渡す。

*   自身の情報

    *   現在セル（グリッド座標）
    *   HP / ステータス
    *   行動可能かどうか（スタン中か等）

*   環境・敵味方

    *   プレイヤーの推定位置（または複数）
    *   距離場（DistanceField）：プレイヤーへの距離、目標地点への距離など
    *   行動方針（Aggressive / Defensive / Patrol）

*   禁止：Occupancy / Path 状態

    *   「そのセルが埋まっているか」「そこまでの経路があるか」は入れない。

---

### 6. 戦術ロジックの標準パターン

### 6-1. 近接アタッカーの例

1.  プレイヤーが攻撃射程内か？

    *   射程チェックは DistanceField やタイル距離で判定可（物理検証ではない）。
    *   射程内 → `IntentType = Attack`、`TargetCell = PlayerCell`。

2.  そうでなければ、プレイヤーに近づきたい：

    *   DistanceField（プレイヤーまでの距離）を参照し、

        *   自身の周囲 4 方向のうち、DistanceField をもっとも小さくする方向を選ぶ。
    *   その方向のセルを `TargetCell` として `IntentType = Move`。

3.  ターゲットがいない or 追跡不要なら：

    *   `IntentType = Wait`（純粋に「動かない」戦術判断として）。

ここでは、**「その方向が通れるかどうか」は考慮しない**。
通れなければ、Phase3 で WAIT 化される／別セルに修正される。

### 6-2. 射撃型の例

1.  プレイヤーが視界内かつ射程内 → `Attack`。
2.  視界外だが推定位置がある → 近寄る (`Move`).
3.  それもなければ → Patrol パターン（決められたルートに沿って Move）。

この時点でもやはり、「射線が遮られているか」「壁かどうか」は EnemyThinker では判断しない。
（視界計算自体を TacticalContext 側で済ませておくのは可）

---

### 7. 責務分割マトリクス（フェーズとの対応）

| フェーズ / コンポーネント   | EnemyThinker            | TurnCorePhaseManager        | ConflictResolver |
| :---------------- | :---------------------- | :-------------------------- | :--------------- |
| フェーズ1：インテント表明    | 戦術判断 → FEnemyIntent を生成 | （関与しない）                     | （関与しない）          |
| フェーズ2：収集 & モード決定 | （関与しない）                 | （関与しない）                     | （関与しない）          |
| フェーズ3：競合解決       | （関与しない）                 | Intent → Reservation 化、物理解釈 | 競合解決／WAIT 化      |
| フェーズ4：アクション実行    | （関与しない）                 | ResolvedAction → 実行の橋渡し     | （関与しない）          |

※ここでのポイントは：

*   「敵 AI が物理状態を見て行動を変える」のではなく、
*   「物理状態は Phase3 で一括処理され、その結果として WAIT になる／別セルに修正される」こと。

---

### 8. 実装タスク（チェックリスト）

### 8-1. コード監査

*   [ ] `UEnemyThinkerBase` および派生クラス内を全検索し、以下が無いか確認：

    *   `Occupancy` / `GridOccupancySubsystem` / `IsCellOccupied` 等
    *   Pathfinding ライブラリへの直接アクセス (`PathFinder`, `IsWalkable`, `FindPath` 等)
    *   `MovePrecheck` や「そのセルに実際に移動できるか」を確認する関数呼び出し

*   [ ] 上記が存在する場合、すべて削除 or `UTurnCorePhaseManager` 側へ移動。

### 8-2. WAIT の扱い整理

*   [ ] EnemyThinker が WAIT を返すのは「戦術的に動かない／動きたくない」と判断したケースに限定。

    *   例：

        *   見える敵がいないので待機
        *   防衛モードで持ち場を離れない
*   [ ] 「道が塞がれているから WAIT」「目標セルが無効だから WAIT」といったロジックは削除し、Phase3 に委譲。

### 8-3. TacticalContext の見直し

*   [ ] `FEnemyTacticalContext` から Occupancy / 物理状態に関する情報を排除。
*   [ ] 代わりに、DistanceField / ThreatMap / 視界情報など「戦術判断に必要な抽象化情報」を充実させる。
*   [ ] Context のコメントに「この構造体には物理状態は含めない」旨を明記。

### 8-4. テスト・ログ

*   [ ] ログに「EnemyThinkerIntent: Actor, IntentType, TargetCell, Reason(任意)」を出す機能を追加。
*   [ ] ConflictResolver のログ（Priority 2 で追加）と組み合わせて、

    *   「Thinker がどう考えたか」
    *   「最終的にどう解決されたか」
        を比較できるようにする。

---

### 9. テスト観点（Priority 4 完了条件）

1.  **塞がれた通路のテスト**

    *   通路が完全に塞がれた状態で、敵がプレイヤーへ「近づきたい」状況を作る。
    *   EnemyThinker は常に「目標方向へ Move 意図」を出し続ける（WAIT しない）。
    *   実際の動きは、ConflictResolver / CoreResolvePhase により WAIT 化される。
    *   インテント自体は毎ターン生成されていることをログで確認。

2.  **複数敵の交通渋滞**

    *   狭い通路を複数の敵が一列に並んでプレイヤーに向かってくる状況。
    *   EnemyThinker は全員「前進したい」Move インテントを出す。
    *   実際に動けるのは先頭のみで、後続は WAIT に解決される（Priority 2 のルール）。
    *   EnemyThinker 側では「後続だから WAIT する」といった分岐が無いことを確認。

3.  **戦術的 WAIT**

    *   防衛 AI など、「プレイヤーが一定距離以内でなければ動かない」敵を用意。
    *   プレイヤーが範囲外 → EnemyThinker は WAIT を返す。
    *   プレイヤーが範囲内 → Move or Attack を返す。
    *   この WAIT は戦術判断であり、物理的制約とは無関係であることを確認。

4.  **Determinism**

    *   同じタクティカルコンテキストで複数回ターンを回したとき、

        *   ランダム要素が無い限り、EnemyThinker のインテントが毎回同じであること。
    *   乱数を使う場合も、`FRandomStream` 等でターン単位にシードが管理されていること。
