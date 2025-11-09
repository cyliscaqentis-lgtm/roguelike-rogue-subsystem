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
