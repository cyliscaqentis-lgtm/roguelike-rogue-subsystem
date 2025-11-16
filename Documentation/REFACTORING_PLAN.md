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

#### 1.2 UTurnEventDispatcher (Subsystem)
**責務**: ターンイベントの配信

```cpp
UCLASS()
class UTurnEventDispatcher : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintAssignable)
    FOnTurnStarted OnTurnStarted;

    UPROPERTY(BlueprintAssignable)
    FOnPlayerInputReceived OnPlayerInputReceived;

    UPROPERTY(BlueprintAssignable)
    FOnFloorReady OnFloorReady;

    void BroadcastTurnStarted(int32 TurnIndex);
    void BroadcastPhaseChanged(FGameplayTag NewPhase);
};
```

**移行元**:
- `OnTurnStarted`デリゲート
- `OnPlayerInputReceived`デリゲート
- `OnFloorReady`デリゲート

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
    TObjectPtr<UTurnEventDispatcher> EventDispatcher;

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
2. `UTurnEventDispatcher`作成
3. `UTurnDebugSubsystem`作成

#### Phase 2: メソッド移行
1. コマンド処理→`UTurnCommandHandler`
2. イベント配信→`UTurnEventDispatcher`
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
