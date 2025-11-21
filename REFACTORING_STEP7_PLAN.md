# Step 7: OnTurnStartedHandler リファクタリング計画

## 目的
`AGameTurnManagerBase::OnTurnStartedHandler` (210行) を分解し、ターン初期化の責務を新しいサブシステムに移譲する。

## 現状分析

### `OnTurnStartedHandler`の現在の責務
1. **ターンID設定** (行1069-1079)
2. **残留タグのクリア** (行1090-1093)
3. **敵リストのリフレッシュ** (行1095-1103)
4. **GridOccupancy管理** (行1112-1145)
   - ターンID設定
   - 古い予約のパージ
   - ユニット位置の再構築
5. **DistanceField更新** (行1148-1230)
   - プレイヤー位置取得
   - 敵位置収集
   - マージン計算
   - 距離場更新
   - 到達可能性検証
6. **敵Intent事前生成** (行1232-1273)
   - 観測データ構築
   - Intent収集

### 問題点
- **単一責任原則違反**: 1つの関数が6つの異なる責務を持つ
- **テスト困難**: 各責務を個別にテストできない
- **再利用不可**: ロジックが`GameTurnManagerBase`に密結合
- **可読性低下**: 210行の長大な関数

## 新しいサブシステム設計

### `UTurnInitializationSubsystem`

#### 責務
ターン開始時の初期化処理を統括する。

#### API設計

```cpp
UCLASS()
class LYRAGAME_API UTurnInitializationSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    // サブシステムライフサイクル
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    /**
     * ターン開始時の初期化を実行
     * @param TurnId 開始するターンのID
     * @param PlayerPawn プレイヤーのPawn
     * @param Enemies 敵ユニットのリスト
     */
    void InitializeTurn(int32 TurnId, APawn* PlayerPawn, const TArray<AActor*>& Enemies);

    /**
     * DistanceFieldを更新
     * @param PlayerPawn プレイヤーのPawn
     * @param Enemies 敵ユニットのリスト
     * @return 更新が成功したか
     */
    bool UpdateDistanceField(APawn* PlayerPawn, const TArray<AActor*>& Enemies);

    /**
     * 敵の事前Intentを生成
     * @param PlayerPawn プレイヤーのPawn
     * @param Enemies 敵ユニットのリスト
     * @param OutIntents 生成されたIntentの出力先
     * @return 生成が成功したか
     */
    bool GeneratePreliminaryIntents(APawn* PlayerPawn, const TArray<AActor*>& Enemies, TArray<FEnemyIntent>& OutIntents);

    /**
     * GridOccupancyを初期化
     * @param TurnId ターンID
     * @param PlayerPawn プレイヤーのPawn
     * @param Enemies 敵ユニットのリスト
     */
    void InitializeGridOccupancy(int32 TurnId, APawn* PlayerPawn, const TArray<AActor*>& Enemies);

private:
    /**
     * DistanceField更新のマージンを計算
     * @param PlayerGrid プレイヤーのグリッド座標
     * @param EnemyPositions 敵の位置セット
     * @return 計算されたマージン
     */
    int32 CalculateDistanceFieldMargin(const FIntPoint& PlayerGrid, const TSet<FIntPoint>& EnemyPositions) const;

    /**
     * DistanceFieldの到達可能性を検証
     * @param TurnId ターンID
     * @param EnemyPositions 敵の位置セット
     * @param Margin 使用されたマージン
     * @return すべての敵が到達可能か
     */
    bool ValidateDistanceFieldReachability(int32 TurnId, const TSet<FIntPoint>& EnemyPositions, int32 Margin) const;
};
```

## 実装手順

### Phase 1: サブシステムの作成

#### 1.1 ファイル作成
- `Turn/TurnInitializationSubsystem.h`
- `Turn/TurnInitializationSubsystem.cpp`

#### 1.2 基本構造の実装
```cpp
// TurnInitializationSubsystem.h
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TurnInitializationSubsystem.generated.h"

class APawn;
class AActor;
struct FEnemyIntent;

UCLASS()
class LYRAGAME_API UTurnInitializationSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    void InitializeTurn(int32 TurnId, APawn* PlayerPawn, const TArray<AActor*>& Enemies);
    bool UpdateDistanceField(APawn* PlayerPawn, const TArray<AActor*>& Enemies);
    bool GeneratePreliminaryIntents(APawn* PlayerPawn, const TArray<AActor*>& Enemies, TArray<FEnemyIntent>& OutIntents);
    void InitializeGridOccupancy(int32 TurnId, APawn* PlayerPawn, const TArray<AActor*>& Enemies);

private:
    int32 CalculateDistanceFieldMargin(const FIntPoint& PlayerGrid, const TSet<FIntPoint>& EnemyPositions) const;
    bool ValidateDistanceFieldReachability(int32 TurnId, const TSet<FIntPoint>& EnemyPositions, int32 Margin) const;

    int32 CurrentTurnId = INDEX_NONE;
};
```

### Phase 2: ロジックの移行

#### 2.1 `UpdateDistanceField`の実装
**元のコード**: `OnTurnStartedHandler` 行1148-1230

```cpp
bool UTurnInitializationSubsystem::UpdateDistanceField(APawn* PlayerPawn, const TArray<AActor*>& Enemies)
{
    UWorld* World = GetWorld();
    if (!World || !PlayerPawn)
    {
        UE_LOG(LogTurnManager, Error, TEXT("[TurnInit] UpdateDistanceField: Invalid World or PlayerPawn"));
        return false;
    }

    UGridPathfindingSubsystem* PathFinder = World->GetSubsystem<UGridPathfindingSubsystem>();
    UDistanceFieldSubsystem* DistanceField = World->GetSubsystem<UDistanceFieldSubsystem>();
    UGridOccupancySubsystem* Occupancy = World->GetSubsystem<UGridOccupancySubsystem>();

    if (!PathFinder || !DistanceField || !Occupancy)
    {
        UE_LOG(LogTurnManager, Error, TEXT("[TurnInit] Required subsystems not available"));
        return false;
    }

    // プレイヤー位置取得
    const FIntPoint PlayerGrid = Occupancy->GetCellOfActor(PlayerPawn);

    // 敵位置収集
    TSet<FIntPoint> EnemyPositions;
    for (AActor* Enemy : Enemies)
    {
        if (IsValid(Enemy))
        {
            FIntPoint EnemyGrid = PathFinder->WorldToGrid(Enemy->GetActorLocation());
            EnemyPositions.Add(EnemyGrid);
        }
    }

    // マージン計算
    const int32 Margin = CalculateDistanceFieldMargin(PlayerGrid, EnemyPositions);

    UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] DF: Player=(%d,%d) Enemies=%d MaxDist=%d Margin=%d"),
        CurrentTurnId, PlayerGrid.X, PlayerGrid.Y, EnemyPositions.Num(), 
        /* MaxDistは内部計算 */, Margin);

    // 距離場更新
    DistanceField->UpdateDistanceFieldOptimized(PlayerGrid, EnemyPositions, Margin);

    // 到達可能性検証
    const bool bAllReachable = ValidateDistanceFieldReachability(CurrentTurnId, EnemyPositions, Margin);

    return bAllReachable;
}
```

#### 2.2 `GeneratePreliminaryIntents`の実装
**元のコード**: `OnTurnStartedHandler` 行1232-1273

```cpp
bool UTurnInitializationSubsystem::GeneratePreliminaryIntents(
    APawn* PlayerPawn, 
    const TArray<AActor*>& Enemies, 
    TArray<FEnemyIntent>& OutIntents)
{
    UWorld* World = GetWorld();
    if (!World || !PlayerPawn || Enemies.Num() == 0)
    {
        return false;
    }

    UEnemyAISubsystem* EnemyAISys = World->GetSubsystem<UEnemyAISubsystem>();
    UGridPathfindingSubsystem* PathFinder = World->GetSubsystem<UGridPathfindingSubsystem>();
    UGridOccupancySubsystem* Occupancy = World->GetSubsystem<UGridOccupancySubsystem>();

    if (!EnemyAISys || !PathFinder || !Occupancy)
    {
        UE_LOG(LogTurnManager, Error, TEXT("[TurnInit] Required subsystems not available for intent generation"));
        return false;
    }

    // プレイヤーグリッド座標取得
    const FIntPoint PlayerGrid = Occupancy->GetCellOfActor(PlayerPawn);

    // 観測データ構築
    TArray<FEnemyObservation> PreliminaryObs;
    EnemyAISys->BuildObservations(Enemies, PlayerGrid, PathFinder, PreliminaryObs);

    // Intent収集
    EnemyAISys->CollectIntents(PreliminaryObs, Enemies, OutIntents);

    UE_LOG(LogTurnManager, Warning,
        TEXT("[Turn %d] Preliminary intents generated: %d intents from %d enemies"),
        CurrentTurnId, OutIntents.Num(), Enemies.Num());

    return OutIntents.Num() > 0;
}
```

#### 2.3 `InitializeGridOccupancy`の実装
**元のコード**: `OnTurnStartedHandler` 行1112-1145

```cpp
void UTurnInitializationSubsystem::InitializeGridOccupancy(
    int32 TurnId, 
    APawn* PlayerPawn, 
    const TArray<AActor*>& Enemies)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    UGridOccupancySubsystem* GridOccupancy = World->GetSubsystem<UGridOccupancySubsystem>();
    if (!GridOccupancy)
    {
        return;
    }

    // ターンID設定
    GridOccupancy->SetCurrentTurnId(TurnId);

    // 古い予約のパージ
    GridOccupancy->PurgeOutdatedReservations(TurnId);
    UE_LOG(LogTurnManager, Log,
        TEXT("[Turn %d] PurgeOutdatedReservations called at turn start"),
        TurnId);

    // ユニット位置の検証
    TArray<AActor*> AllUnits;
    if (PlayerPawn)
    {
        AllUnits.Add(PlayerPawn);
    }
    AllUnits.Append(Enemies);

    if (AllUnits.Num() > 0)
    {
        // Note: RebuildFromWorldPositions は破壊的なので呼ばない
        // 代わりにEnforceUniqueOccupancyを使用
    }
    else
    {
        GridOccupancy->EnforceUniqueOccupancy();
        UE_LOG(LogTurnManager, Warning,
            TEXT("[Turn %d] EnforceUniqueOccupancy called (no units cached yet)"),
            TurnId);
    }
}
```

#### 2.4 `InitializeTurn`の実装（統合メソッド）

```cpp
void UTurnInitializationSubsystem::InitializeTurn(
    int32 TurnId, 
    APawn* PlayerPawn, 
    const TArray<AActor*>& Enemies)
{
    CurrentTurnId = TurnId;

    UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] ==== TurnInitialization START ===="), TurnId);

    // 1. GridOccupancy初期化
    InitializeGridOccupancy(TurnId, PlayerPawn, Enemies);

    // 2. DistanceField更新
    if (PlayerPawn && Enemies.Num() > 0)
    {
        UpdateDistanceField(PlayerPawn, Enemies);
    }

    // 3. 事前Intent生成
    TArray<FEnemyIntent> PreliminaryIntents;
    if (GeneratePreliminaryIntents(PlayerPawn, Enemies, PreliminaryIntents))
    {
        // IntentをEnemyTurnDataSubsystemに保存
        if (UEnemyTurnDataSubsystem* EnemyData = GetWorld()->GetSubsystem<UEnemyTurnDataSubsystem>())
        {
            EnemyData->Intents = PreliminaryIntents;
        }
    }

    UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] ==== TurnInitialization END ===="), TurnId);
}
```

### Phase 3: `GameTurnManagerBase`の簡素化

#### 3.1 `OnTurnStartedHandler`のリファクタリング

**Before** (210行):
```cpp
void AGameTurnManagerBase::OnTurnStartedHandler(int32 TurnId)
{
    // ... 210行の処理 ...
}
```

**After** (約30行):
```cpp
void AGameTurnManagerBase::OnTurnStartedHandler(int32 TurnId)
{
    if (!HasAuthority()) return;

    CurrentTurnId = TurnId;

    if (CachedCsvObserver.IsValid())
    {
        CachedCsvObserver->SetCurrentTurnForLogging(TurnId);
    }

    // プレイヤーPawn取得
    APawn* PlayerPawn = nullptr;
    if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
    {
        PlayerPawn = PC->GetPawn();
    }

    UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] ==== OnTurnStartedHandler START ===="), TurnId);

    // 残留タグクリア
    ClearResidualInProgressTags();

    // 敵リストリフレッシュ
    RefreshEnemyRoster(PlayerPawn, TurnId, TEXT("OnTurnStartedHandler"));
    TArray<AActor*> EnemyActors;
    CopyEnemyActors(EnemyActors);

    // ターン初期化サブシステムに委譲
    if (UTurnInitializationSubsystem* TurnInit = GetWorld()->GetSubsystem<UTurnInitializationSubsystem>())
    {
        TurnInit->InitializeTurn(TurnId, PlayerPawn, EnemyActors);
    }

    UE_LOG(LogTurnManager, Warning, TEXT("[Turn %d] ==== OnTurnStartedHandler END ===="), TurnId);
}
```

#### 3.2 ヘッダーファイルの更新

`GameTurnManagerBase.h`に以下を追加:
```cpp
#include "Turn/TurnInitializationSubsystem.h"
```

### Phase 4: テストとビルド

#### 4.1 コンパイル確認
```powershell
& "C:\Program Files\Epic Games\UE_5.6\Engine\Build\BatchFiles\Build.bat" LyraEditor Win64 Development "C:\UnrealProjects\RoguelikeDungeon\RoguelikeDungeon.uproject" -WaitMutex -NoTreatWarningsAsErrors
```

#### 4.2 動作確認項目
- [ ] ターン開始時にDistanceFieldが正しく更新される
- [ ] 敵Intentが事前生成される
- [ ] GridOccupancyが正しく初期化される
- [ ] ログ出力が適切に行われる

## 期待される成果

### コード削減
- **Before**: `OnTurnStartedHandler` 210行
- **After**: `OnTurnStartedHandler` 約30行
- **削減**: 約180行
- **新規追加**: `TurnInitializationSubsystem` 約200行（別ファイル）

### 品質向上
- ✅ **単一責任原則**: 各メソッドが明確な責務を持つ
- ✅ **テスト容易性**: サブシステムを個別にテスト可能
- ✅ **再利用性**: 他のシステムからも利用可能
- ✅ **可読性**: 各関数が短く理解しやすい

## 次のステップ

Step 7完了後:
- **Step 8**: `OnPlayerMoveCompleted`のリファクタリング
- **Step 9**: `InitializeTurnSystem`のリファクタリング
