# UE5 Rogueプロジェクト - リファクタリング設計書

作成日: 2025-11-09
対象: 巨大クラスの分割とComponent化

---

## 1. GameTurnManagerBase 分割設計（3,498行 → 5-6クラス）

### 現状の問題
- **3,498行**の巨大クラス
- **50個以上**のメンバ変数
- **8つの異なる責務**が混在

### 責任の分離

#### 1.1 UTurnCommandHandler (Subsystem)
**責務**: プレイヤーコマンド処理

```cpp
UCLASS()
class UTurnCommandHandler : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    // PlayerCommand処理
    bool ProcessPlayerCommand(const FPlayerCommand& Command);
    bool ValidateCommand(const FPlayerCommand& Command);
    void ApplyCommand(const FPlayerCommand& Command);

private:
    TMap<int32, FPlayerCommand> LastAcceptedCommands;
    int32 CurrentInputWindowId = 0;
};
```

**移行元**:
- `ProcessPlayerCommand()`
- `OnPlayerCommandAccepted()`
- `ServerSubmitCommand_Implementation()`

---

#### 1.2 UTurnEventDispatcher (Subsystem) [削除済み]
**削除日**: 2025-11-17
**理由**: 購読者が存在せず、イベントが配信されても実行されない未使用コードのため削除。

**削除前の責務**: ターンイベントの配信

**削除された機能**:
- `BroadcastTurnStarted()` - ターン開始イベント配信
- `BroadcastTurnEnded()` - ターン終了イベント配信
- `BroadcastPlayerInputReceived()` - プレイヤー入力受信イベント配信
- `BroadcastFloorReady()` - フロア準備完了イベント配信
- `BroadcastPhaseChanged()` - フェーズ変更イベント配信
- `BroadcastActionExecuted()` - アクション実行イベント配信

**代替手段**:
- `URogueDungeonSubsystem::OnGridReady` デリゲートは維持されており、フロア準備完了通知に使用可能
- その他のイベントは直接的な呼び出しで管理されている

---

#### 1.3 UTurnDebugSubsystem (Subsystem)
**責務**: デバッグ機能

```cpp
UCLASS()
class UTurnDebugSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    void DumpTurnState(int32 TurnId);
    void LogPhaseTransition(FGameplayTag OldPhase, FGameplayTag NewPhase);
    void DrawDebugInfo();

private:
    UPROPERTY()
    TObjectPtr<UDebugObserverCSV> CSVLogger;
};
```

**移行元**:
- `DebugObserverCSV`
- `DebugObservers`配列
- デバッグ関連のログ出力

---

#### 1.4 AGameTurnManagerCore (Actor - 軽量化版)
**責備**: ターンフロー制御のコア

```cpp
UCLASS()
class AGameTurnManagerCore : public AActor
{
    GENERATED_BODY()

public:
    // コアフロー
    void StartTurn();
    void AdvanceTurnAndRestart();
    void BeginPhase(FGameplayTag PhaseTag);
    void EndPhase(FGameplayTag PhaseTag);

    // Subsystem参照
    UPROPERTY()
    TObjectPtr<UTurnCommandHandler> CommandHandler;

    UPROPERTY()
    TObjectPtr<UTurnDebugSubsystem> DebugSubsystem;

    UPROPERTY()
    TObjectPtr<UTurnCorePhaseManager> PhaseManager;

private:
    // 最小限の状態
    int32 CurrentTurnId = 0;
    int32 CurrentTurnIndex = 0;
    FGameplayTag CurrentPhase;
};
```

**残す責務**:
- ターン進行のオーケストレーション
- フェーズ遷移の管理
- Subsystemの協調

---

### 移行手順

#### Phase 1: Subsystem作成
1. `UTurnCommandHandler`作成
2. ~~`UTurnEventDispatcher`作成~~ [削除済み 2025-11-17]
3. `UTurnDebugSubsystem`作成

#### Phase 2: メソッド移行
1. コマンド処理→`UTurnCommandHandler`
2. ~~イベント配信→`UTurnEventDispatcher`~~ [削除済み 2025-11-17]
3. デバッグ機能→`UTurnDebugSubsystem`

#### Phase 3: 依存関係整理
1. `AGameTurnManagerCore`に参照を追加
2. 既存の呼び出しをSubsystem経由に変更
3. プライベートメンバをSubsystemに移動

#### Phase 4: テスト
1. 既存機能が動作することを確認
2. Blueprint互換性を確認

---

## 2. UnitBase Component化設計（584行 → 3 Components）

### 現状の問題
- **584行**のクラス
- **40個以上**のメンバ変数
- **5つの異なる責務**が混在

### Component分割

#### 2.1 UUnitMovementComponent
**責務**: 移動処理

```cpp
UCLASS()
class UUnitMovementComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    // 移動制御
    void MoveUnit(const TArray<FVector>& Path);
    void OnMoveFinished();

    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
        FOnUnitMoveFinished, AUnitBase*, Unit);
    UPROPERTY(BlueprintAssignable)
    FOnUnitMoveFinished OnMoveFinished;

private:
    TArray<FVector> CurrentPath;
    int32 CurrentPathIndex = 0;
    float MoveSpeed = 300.0f;
};
```

**移行元**:
- `MoveUnit()`
- `OnMoveFinished`デリゲート
- 移動関連の状態変数

---

#### 2.2 UUnitUIComponent
**責備**: UI更新

```cpp
UCLASS()
class UUnitUIComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    void UpdateHealthBar(float CurrentHP, float MaxHP);
    void ShowDamageNumber(int32 Damage);
    void UpdateStatusIcons(const TArray<FGameplayTag>& StatusEffects);

private:
    UPROPERTY()
    TObjectPtr<UUserWidget> HealthBarWidget;
};
```

**移行元**:
- UI更新処理
- ウィジェット管理
- プレゼンテーション層のロジック

---

#### 2.3 AUnitBaseCore (Actor - 軽量化版)
**責務**: Actorコア機能のみ

```cpp
UCLASS()
class AUnitBaseCore : public APawn
{
    GENERATED_BODY()

public:
    // Component参照
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    TObjectPtr<UUnitMovementComponent> MovementComp;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    TObjectPtr<UUnitUIComponent> UIComp;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    TObjectPtr<UAbilitySystemComponent> AbilitySystemComp;

    // コア情報
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 Team = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 UnitID = -1;
};
```

---

### 移行手順

#### Phase 1: Component作成
1. `UUnitMovementComponent`作成
2. `UUnitUIComponent`作成

#### Phase 2: メソッド移行
1. 移動処理→`UUnitMovementComponent`
2. UI更新→`UUnitUIComponent`

#### Phase 3: 統合テスト
1. 既存のUnitが正常に動作することを確認
2. Blueprint互換性を確認

---

## 3. 期待される効果

### パフォーマンス
- ✅ Tickオーバーヘッド削減
- ✅ Component単位での最適化が可能

### 保守性
- ✅ 単一責任原則の遵守
- ✅ テストが容易
- ✅ 新機能追加が簡単

### 可読性
- ✅ 1クラス500行以下
- ✅ 明確な責任分離
- ✅ ドキュメント化が容易

---

## 4. リスク管理

### Blueprint互換性
- **リスク**: 既存Blueprintの破損
- **対策**: Blueprint Redirectorの生成

### ネットワーク
- **リスク**: レプリケーション動作の変化
- **対策**: 段階的な移行とテスト

### パフォーマンス
- **リスク**: Component追加によるオーバーヘッド
- **対策**: プロファイリングで検証

---

## 5. 実装優先度

1. 🔴 **最優先**: 共通ユーティリティ作成（完了）
2. 🟡 **高優先**: `UTurnCommandHandler`作成
3. 🟡 **高優先**: `UUnitMovementComponent`作成
4. 🟢 **中優先**: デバッグSubsystem作成
5. 🟢 **中優先**: イベントDispatcher作成

---

**このドキュメントに基づき、段階的にリファクタリングを実施します。**

---

## 6. GameTurnManagerBase ラッパー関数の削除

### 現状の問題
`GameTurnManagerBase` の責務分離は進んでいるが、依然として他のサブシステムへの処理を委譲（ラップ）するだけの関数が多数残存している。これらの関数は、`GameTurnManagerBase` が依然として多くの責務を持つ「God Object」であるかのような誤解を招き、コードの可読性と保守性を低下させている。

過去の修正（`INC-2025-00009-R1`）で類似のラッパー関数が削除された際の方針に基づき、これらの不要な関数を徹底的に削除する。

### 削除対象の関数リスト

#### 6.1 ダンジョン管理系のラッパー関数
**問題**: `URogueDungeonSubsystem` への単純な処理委譲。
**修正方針**: 呼び出し元は `GetWorld()->GetSubsystem<URogueDungeonSubsystem>()` を経由して、サブシステム本体の関数を直接呼び出すように修正する。

- `GetDungeonSystem()`
- `GetFloorGenerator()`
- `EnsureFloorGenerated()`
- `NextFloor()`
- `WarpPlayerToStairUp()`

#### 6.2 AI処理系のラッパー関数
**問題**: `EnemyAISubsystem` や `EnemyTurnDataSubsystem` への単純な処理委譲。
**修正方針**: AI関連の処理フローは、`TurnFlowCoordinator` やAI関連クラスが、各AIサブシステムを直接呼び出して制御するように修正する。

- `BuildAllObservations()`
- `CollectEnemies_Implementation()`
- `CollectIntents_Implementation()`
- `GetEnemyIntentsBP_Implementation()`
- `HasAnyAttackIntent()`

#### 6.3 汎用ユーティリティ系の関数
**問題**: `GameTurnManagerBase` の状態に依存しないヘルパー関数であり、クラスの責務を肥大化させている。
**修正方針**: `UGameplayStatics` の直接使用に置き換えるか、必要であれば専用の `UBlueprintFunctionLibrary` に移管する。

- `SendGameplayEventWithResult()`
- `SendGameplayEvent()`
- `GetPlayerController_TBS()`
- `GetPlayerPawnCachedOrFetch()`
- `GetPlayerPawn()`
- `GetPlayerActor()`

### 期待される効果
- `GameTurnManagerBase` の責務が「ターン進行の管理」にさらに限定され、クラスの見通しが良くなる。
- 各サブシステムが持つべき責務が明確になり、コードの呼び出し関係が正常化される。
- 不要な中間層がなくなることで、コードの可読性と保守性が向上する。

---

## 7. AGridPathfindingLibrary の責務分離

### 現状の問題
`AGridPathfindingLibrary` は、純粋なパス探索と地形コスト管理という責務を超えて、アクターの占有情報を問い合わせるロジックや、他のユーティリティクラス (`FGridUtils`) と重複する静的関数を抱えている。これにより、`UGridOccupancySubsystem` との責務の境界が曖昧になり、コードの重複を招いている。

### 7.1 アクターの占有情報を問い合わせる重複関数の削除
**問題**: `GetActorAtPosition` や `GetActorsAtGridPosition` は、`UGridOccupancySubsystem` に問い合わせた後、見つからない場合に自前で `TActorIterator` を用いてフォールバック検索を行っており、責務が重複している。アクターの占有に関する問い合わせは、`UGridOccupancySubsystem` が唯一の情報源であるべき。
**修正方針**: これらの関数を削除し、呼び出し元が `UGridOccupancySubsystem::GetActorAtCell` などを直接使用するように修正する。
**対象関数**:
- `GetActorAtPosition`
- `GetActorsAtGridPosition`

### 7.2 責務が曖昧な歩行可能性チェック関数の削除
**問題**: `IsCellWalkable` は地形コストとアクター占有の両方をチェックしており、責務が曖昧。`ReturnGridStatusIgnoringSelf` のような特殊ケース関数も、APIの責務が不明確であることを示している。
**修正方針**: これらの関数を削除し、呼び出し元は `IsCellWalkableIgnoringActor`（地形のみ）と `UGridOccupancySubsystem::IsCellOccupied` を個別に呼び出すか、`IsMoveValid` のような高レベルAPIを使用するように修正する。
**対象関数**:
- `IsCellWalkable`
- `IsCellWalkableAtWorldPosition`
- `ReturnGridStatusIgnoringSelf`

### 7.3 FGridUtils と重複する静的ユーティリティ関数の削除
**問題**: `AGridPathfindingLibrary` に実装されている静的な距離計算関数は、`FGridUtils` の同名関数を呼び出しているだけのラッパーであり、完全に重複している。
**修正方針**: これらの静的関数を `AGridPathfindingLibrary` から削除し、すべての呼び出し元が `FGridUtils` の静的関数を直接使用するように修正する。
**対象関数**:
- `GetChebyshevDistance`
- `GetManhattanDistanceGrid`
- `GetEuclideanDistanceGrid`

### 期待される効果
- `AGridPathfindingLibrary` の責務がパス探索と地形情報に特化し、`UGridOccupancySubsystem` との役割分担が明確になる。
- コードの重複が排除され、保守性が向上する。
- `FGridUtils` のようなユーティリティクラスの利用が促進され、一貫したコーディングスタイルが維持される。

---

## 8. Gameplay Ability の責務分離 (GA_MoveBase) [STATUS: 完了]

### 現状の問題分析
`GA_MoveBase` をはじめとするGameplay Abilityクラスが、`UTurnActionBarrierSubsystem` や `UGridOccupancySubsystem` などのグローバルなサブシステムを直接参照・呼び出している。これにより、以下の問題が発生していた。
- **密結合**: アビリティが特定のターン管理システムやグリッドシステムに強く依存しており、再利用性や独立性が損なわれていた。
- **責務の不整合**: 移動完了後のグリッド更新処理や、アクション完了の通知といった、本来はアビリティの責務外であるべき処理をアビリティ自身が実行していた。

**実装前の状況**:
このリファクタリング実施前、`GA_MoveBase.h` および `GA_MoveBase.cpp` には以下のメンバー変数と関数が残存していた。

*   **残存していたメンバー変数:**
    *   `int32 MoveTurnId;`
    *   `FGuid MoveActionId;`
    *   `bool bBarrierRegistered;`
    *   `bool bBarrierActionCompleted;`
    *   `FGuid BarrierToken;`
    *   `mutable TWeakObjectPtr<UTurnActionBarrierSubsystem> CachedBarrier;`
    *   `mutable TWeakObjectPtr<AActor> CachedBarrierAvatar;`

*   **残存していた関数宣言:**
    *   `UTurnActionBarrierSubsystem* GetBarrierSubsystem() const;`
    *   `void CompleteBarrierAction(...);`
    *   `bool RegisterBarrier(...);`
    *   `void UpdateOccupancy(...);`

### リファクタリング方針
1.  **依存関係の逆転**: アビリティがサブシステムを直接呼び出すのではなく、アビリティは自身の状態をGameplay Tagで表現するに留める。サブシステム側がそのタグを監視し、必要な処理を行うように設計を変更する。
2.  **責務の移譲**: 物理的な移動に責任を持つ `UUnitMovementComponent` が、移動完了後のグリッド占有情報更新の責務も担うようにする。

---

### 実装完了内容

#### Phase 1: `UUnitMovementComponent` の責務追加 [完了]
**実装日**: 2025-11-17

**実装内容**:
- `Character/UnitMovementComponent.h`に`FTimerHandle GridUpdateRetryHandle`を追加
- `Character/UnitMovementComponent.cpp`の`FinishMovement()`関数を修正
- `OnMoveFinished.Broadcast()`を呼び出す**前**に、グリッド更新ロジックを実装
- `FPathFinderUtils::GetCachedPathFinder()`を使用してPathFinderを取得
- `UGridOccupancySubsystem::UpdateActorCell()`を呼び出してグリッド更新
- 更新失敗時（競合など）は0.1秒後に再試行するタイマーを設定
- 必要なインクルード（`GridOccupancySubsystem.h`, `PathFinderUtils.h`, `TimerManager.h`）を追加

#### Phase 2: `GA_MoveBase` の責務削減 [完了]
**実装日**: 2025-11-17

**実装内容**:
- `GA_MoveBase::OnMoveFinished()`からグリッド更新処理（行887-923）を削除
- `UpdateOccupancy()`関数の実装（行719-739）を削除
- `UpdateOccupancy()`関数の宣言を`GA_MoveBase.h`から削除
- `UpdateGridState()`から`UpdateOccupancy()`呼び出しを削除し、コメントで説明を追加

#### Phase 3: `GA_MoveBase` からのバリア管理削除 [完了]
**実装日**: 2025-11-17

**実装内容**:
- `GA_MoveBase.h`から以下のメンバー変数を削除:
  - `int32 MoveTurnId;`
  - `FGuid MoveActionId;`
  - `bool bBarrierRegistered;`
  - `bool bBarrierActionCompleted;`
  - `FGuid BarrierToken;`
  - `mutable TWeakObjectPtr<UTurnActionBarrierSubsystem> CachedBarrier;`
  - `mutable TWeakObjectPtr<AActor> CachedBarrierAvatar;`
- `GA_MoveBase.h`から以下の関数宣言を削除:
  - `UTurnActionBarrierSubsystem* GetBarrierSubsystem() const;`
  - `void CompleteBarrierAction(...);`
  - `bool RegisterBarrier(...);`
- `GA_MoveBase.cpp`から以下の実装を削除:
  - `GetBarrierSubsystem()`の実装
  - `CompleteBarrierAction()`の実装
  - `RegisterBarrier()`の実装
- `ActivateAbility()`内のバリア関連呼び出しを削除:
  - `RegisterBarrier()`呼び出しを削除
  - `Barrier->RegisterActionOnce()`呼び出しを削除
  - `MoveTurnId`の取得処理を`CompletedTurnIdForEvent`用に簡略化
- `EndAbility()`内のバリア関連呼び出しを削除:
  - `CompleteBarrierAction()`呼び出しを削除
  - `Barrier->CompleteActionToken()`呼び出しを削除
  - バリア関連のメンバー変数のリセット処理を簡略化
- `OnMoveFinished()`関数を簡略化:
  - グリッド更新処理とバリア処理を削除
  - 位置のスナップ処理のみ残し、`EndAbility()`を呼び出すだけのシンプルな実装に変更
- `SendCompletionEvent()`を修正:
  - `MoveTurnId`への参照を削除し、`CompletedTurnIdForEvent`のみを使用

#### Phase 4: 依存関係の整理 [完了]
**実装日**: 2025-11-17

**実装内容**:
- `GA_MoveBase.cpp`から`TurnActionBarrierSubsystem.h`のインクルードを削除
- `GA_MoveBase.h`から`UTurnActionBarrierSubsystem`の前方宣言を削除
- `GridOccupancySubsystem`は`GetReservedCellForActor()`で使用されているため残存

---

### 実装結果

**削除されたコード**:
- バリア管理関連のメンバー変数: 7個
- バリア管理関連の関数: 3個
- グリッド更新処理: `OnMoveFinished()`内の約40行
- 不要なインクルード: 1個
- 不要な前方宣言: 1個

**追加されたコード**:
- `UnitMovementComponent`へのグリッド更新処理: 約30行
- タイマーハンドルメンバー変数: 1個

**期待される効果**:
- `GA_MoveBase`の責務が「移動アビリティの実行」に限定され、コードが簡潔になった
- `UnitMovementComponent`が移動に関連するすべての処理（物理移動＋グリッド更新）を担当し、責務が明確になった
- バリア管理が`GA_MoveBase`から分離され、再利用性が向上した

---

### 過去の修正指示（参考）

#### Phase 1: `UUnitMovementComponent` の責務追加
**目的**: 移動を完了したコンポーネント自身が、グリッド占有情報を更新する責務を持つようにする。

1.  **対象ファイル**: `Character/UnitMovementComponent.cpp`
2.  **修正箇所**: `FinishMovement()` 関数
3.  **修正内容**: `OnMoveFinished.Broadcast(OwnerUnit);` を呼び出す**前**に、グリッド更新ロジックを実装する。これには、競合時の再試行ロジックも含まれる。

    ```cpp
    // UUnitMovementComponent::FinishMovement() の実装を修正

    void UUnitMovementComponent::FinishMovement()
    {
        bIsMoving = false;

        AUnitBase* OwnerUnit = GetOwnerUnit();
        if (OwnerUnit)
        {
            if (UWorld* World = GetWorld())
            {
                if (UGridOccupancySubsystem* Occupancy = World->GetSubsystem<UGridOccupancySubsystem>())
                {
                    // FPathFinderUtils.h のインクルードが必要な場合がある
                    const AGridPathfindingLibrary* PathFinder = FPathFinderUtils::GetCachedPathFinder(World);
                    if (PathFinder)
                    {
                        const FIntPoint FinalCell = PathFinder->WorldToGrid(OwnerUnit->GetActorLocation());
                        if (!Occupancy->UpdateActorCell(OwnerUnit, FinalCell))
                        {
                            // 更新が失敗した場合（競合など）、短時間後に再試行する
                            FTimerHandle RetryHandle;
                            World->GetTimerManager().SetTimer(RetryHandle, this, &UUnitMovementComponent::FinishMovement, 0.1f, false);
                            return; // ここで処理を中断し、再試行に任せる
                        }
                    }
                }
            }
        }

        CurrentPath.Empty();
        CurrentPathIndex = 0;
        SetComponentTickEnabled(false);
        OnMoveFinished.Broadcast(OwnerUnit);
        UE_LOG(LogTemp, Log, TEXT("[UnitMovementComponent] Movement finished"));
    }
    ```

---

#### Phase 2: `GA_MoveBase` の責務削減
**目的**: `GA_MoveBase` からサブシステムへの直接参照をすべて削除し、アビリティを自己完結させる。

1.  **対象ファイル**: `Abilities/GA_MoveBase.h`
2.  **修正内容**: 
    -   以下のバリア管理用のメンバー変数を**削除**する。
        ```cpp
        int32 MoveTurnId;
        FGuid MoveActionId;
        bool bBarrierRegistered;
        bool bBarrierActionCompleted;
        FGuid BarrierToken;
        mutable TWeakObjectPtr<UTurnActionBarrierSubsystem> CachedBarrier;
        ```
    -   以下の関数宣言を**削除**する。
        ```cpp
        UTurnActionBarrierSubsystem* GetBarrierSubsystem() const;
        void CompleteBarrierAction(...);
        bool RegisterBarrier(...);
        void UpdateOccupancy(...);
        ```

3.  **対象ファイル**: `Abilities/GA_MoveBase.cpp`
4.  **修正内容**: 
    -   上記で削除した関数の実装をすべて**削除**する。
    -   `ActivateAbility` 内のバリア関連の呼び出し (`RegisterBarrier`, `Barrier->RegisterActionOnce` など) をすべて**削除**する。
    -   `EndAbility` 内のバリア関連の呼び出し (`CompleteBarrierAction`, `Barrier->CompleteActionToken` など) をすべて**削除**する。
    -   `OnMoveFinished` 関数内の処理を、`EndAbility` を呼び出すだけのシンプルなものに**書き換える**。グリッド更新ロジックはすべて**削除**する。
        ```cpp
        void UGA_MoveBase::OnMoveFinished(AUnitBase* Unit)
        {
            UE_LOG(LogTurnManager, Log,
                TEXT("[MoveComplete] Unit %s reached destination, GA_MoveBase ending."),
                *GetNameSafe(Unit));

            EndAbility(CachedSpecHandle, &CachedActorInfo, CachedActivationInfo, true, false);
        }
        ```
