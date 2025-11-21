# Step 8: OnPlayerMoveCompleted リファクタリング計画

## 目的
`AGameTurnManagerBase::OnPlayerMoveCompleted` (134行) を分解し、プレイヤー移動後の処理を新しいサブシステムに移譲する。

## 現状分析

### `OnPlayerMoveCompleted`の現在の責務 (行1350-1482)
1. **ターン検証** (行1352-1361)
   - 古いターン通知を無視
2. **プレイヤー移動の完了処理** (行1372-1373)
   - `FinalizePlayerMove()`呼び出し
3. **敵リストのリフレッシュ** (行1388-1397)
   - スポーン/死亡の処理
4. **AI知識の更新** (行1399-1429)
   - DistanceField更新
   - 観測データ再構築
   - Intent再生成
5. **フェーズ決定と実行** (行1431-1476)
   - 攻撃の有無を判定
   - Sequential/Simultaneousモードの選択
   - 対応するフェーズの実行

### 問題点
- **単一責任原則違反**: 1つの関数が5つの異なる責務を持つ
- **テスト困難**: 各責務を個別にテストできない
- **再利用不可**: ロジックが`GameTurnManagerBase`に密結合
- **可読性低下**: 134行の長大な関数

## 新しいサブシステム設計

### `UPlayerMoveHandlerSubsystem`

#### 責務
プレイヤー移動完了後の処理を統括する。

#### API設計

```cpp
UCLASS()
class LYRAGAME_API UPlayerMoveHandlerSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    // サブシステムライフサイクル
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    /**
     * プレイヤー移動完了時の処理を実行
     * @param Payload イベントデータ
     * @param CurrentTurnId 現在のターンID
     * @param OutEnemyActors 更新された敵リスト
     * @param OutFinalIntents 生成されたIntent
     * @return 処理が成功したか
     */
    bool HandlePlayerMoveCompletion(
        const FGameplayEventData* Payload,
        int32 CurrentTurnId,
        TArray<AActor*>& OutEnemyActors,
        TArray<FEnemyIntent>& OutFinalIntents);

    /**
     * プレイヤーの最終位置に基づいてAI知識を更新
     * @param PlayerPawn プレイヤーのPawn
     * @param Enemies 敵ユニットのリスト
     * @param OutIntents 生成されたIntentの出力先
     * @return 更新が成功したか
     */
    bool UpdateAIKnowledge(
        APawn* PlayerPawn,
        const TArray<AActor*>& Enemies,
        TArray<FEnemyIntent>& OutIntents);

    /**
     * プレイヤーの最終位置に基づいてDistanceFieldを更新
     * @param PlayerCell プレイヤーのグリッド座標
     * @return 更新が成功したか
     */
    bool UpdateDistanceFieldForFinalPosition(const FIntPoint& PlayerCell);

private:
    /**
     * ターン通知が有効かを検証
     * @param NotifiedTurn 通知されたターンID
     * @param CurrentTurn 現在のターンID
     * @return 有効な通知か
     */
    bool ValidateTurnNotification(int32 NotifiedTurn, int32 CurrentTurn) const;
};
```

## 実装手順

### Phase 1: サブシステムの作成

#### 1.1 ファイル作成
- `Turn/PlayerMoveHandlerSubsystem.h`
- `Turn/PlayerMoveHandlerSubsystem.cpp`

#### 1.2 基本構造の実装
```cpp
// PlayerMoveHandlerSubsystem.h
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PlayerMoveHandlerSubsystem.generated.h"

class APawn;
class AActor;
struct FEnemyIntent;
struct FGameplayEventData;

UCLASS()
class LYRAGAME_API UPlayerMoveHandlerSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    bool HandlePlayerMoveCompletion(
        const FGameplayEventData* Payload,
        int32 CurrentTurnId,
        TArray<AActor*>& OutEnemyActors,
        TArray<FEnemyIntent>& OutFinalIntents);

    bool UpdateAIKnowledge(
        APawn* PlayerPawn,
        const TArray<AActor*>& Enemies,
        TArray<FEnemyIntent>& OutIntents);

    bool UpdateDistanceFieldForFinalPosition(const FIntPoint& PlayerCell);

private:
    bool ValidateTurnNotification(int32 NotifiedTurn, int32 CurrentTurn) const;
};
```

### Phase 2: ロジックの移行

#### 2.1 `ValidateTurnNotification`の実装
**元のコード**: `OnPlayerMoveCompleted` 行1352-1361

```cpp
bool UPlayerMoveHandlerSubsystem::ValidateTurnNotification(int32 NotifiedTurn, int32 CurrentTurn) const
{
    if (NotifiedTurn != CurrentTurn)
    {
        UE_LOG(LogPlayerMove, Warning,
            TEXT("IGNORE stale move notification (notified=%d, current=%d)"),
            NotifiedTurn, CurrentTurn);
        return false;
    }
    return true;
}
```

#### 2.2 `UpdateDistanceFieldForFinalPosition`の実装
**元のコード**: `OnPlayerMoveCompleted` 行1408-1412

```cpp
bool UPlayerMoveHandlerSubsystem::UpdateDistanceFieldForFinalPosition(const FIntPoint& PlayerCell)
{
    UWorld* World = GetWorld();
    if (!World || PlayerCell == FIntPoint(-1, -1))
    {
        return false;
    }

    UDistanceFieldSubsystem* DF = World->GetSubsystem<UDistanceFieldSubsystem>();
    if (!DF)
    {
        UE_LOG(LogPlayerMove, Error, TEXT("DistanceFieldSubsystem not available"));
        return false;
    }

    DF->UpdateDistanceField(PlayerCell);
    UE_LOG(LogPlayerMove, Log, TEXT("DistanceField updated for player at (%d,%d)"), 
        PlayerCell.X, PlayerCell.Y);
    
    return true;
}
```

#### 2.3 `UpdateAIKnowledge`の実装
**元のコード**: `OnPlayerMoveCompleted` 行1414-1429

```cpp
bool UPlayerMoveHandlerSubsystem::UpdateAIKnowledge(
    APawn* PlayerPawn,
    const TArray<AActor*>& Enemies,
    TArray<FEnemyIntent>& OutIntents)
{
    UWorld* World = GetWorld();
    if (!World || !PlayerPawn || Enemies.Num() == 0)
    {
        UE_LOG(LogPlayerMove, Warning, TEXT("Cannot update AI knowledge: Invalid parameters"));
        return false;
    }

    UEnemyAISubsystem* EnemyAISys = World->GetSubsystem<UEnemyAISubsystem>();
    UEnemyTurnDataSubsystem* EnemyData = World->GetSubsystem<UEnemyTurnDataSubsystem>();
    UGridOccupancySubsystem* Occupancy = World->GetSubsystem<UGridOccupancySubsystem>();
    UGridPathfindingSubsystem* PathFinder = World->GetSubsystem<UGridPathfindingSubsystem>();

    if (!EnemyAISys || !EnemyData || !Occupancy || !PathFinder)
    {
        UE_LOG(LogPlayerMove, Error, TEXT("Required subsystems not available for AI knowledge update"));
        return false;
    }

    // Get final player position
    const FIntPoint FinalPlayerCell = Occupancy->GetCellOfActor(PlayerPawn);
    if (FinalPlayerCell == FIntPoint(-1, -1))
    {
        UE_LOG(LogPlayerMove, Error, TEXT("Cannot get player cell position"));
        return false;
    }

    // Update DistanceField
    UpdateDistanceFieldForFinalPosition(FinalPlayerCell);

    // Rebuild Observations
    TArray<FEnemyObservation> Observations;
    EnemyAISys->BuildObservations(Enemies, FinalPlayerCell, PathFinder, Observations);
    EnemyData->Observations = Observations;

    // Collect Intents
    EnemyAISys->CollectIntents(Observations, Enemies, OutIntents);

    // Commit to Data
    EnemyData->SaveIntents(OutIntents);

    UE_LOG(LogPlayerMove, Log, 
        TEXT("AI Knowledge Updated: Player at (%d,%d). %d Intents generated."),
        FinalPlayerCell.X, FinalPlayerCell.Y, OutIntents.Num());

    return true;
}
```

#### 2.4 `HandlePlayerMoveCompletion`の実装（統合メソッド）

```cpp
bool UPlayerMoveHandlerSubsystem::HandlePlayerMoveCompletion(
    const FGameplayEventData* Payload,
    int32 CurrentTurnId,
    TArray<AActor*>& OutEnemyActors,
    TArray<FEnemyIntent>& OutFinalIntents)
{
    // 1. Validate turn notification
    const int32 NotifiedTurn = Payload ? static_cast<int32>(Payload->EventMagnitude) : -1;
    if (!ValidateTurnNotification(NotifiedTurn, CurrentTurnId))
    {
        return false;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return false;
    }

    // 2. Get player pawn
    APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0);
    if (!PlayerPawn)
    {
        UE_LOG(LogPlayerMove, Error, TEXT("[Turn %d] PlayerPawn not found"), CurrentTurnId);
        return false;
    }

    // 3. Refresh enemy roster (handled by UnitTurnStateSubsystem)
    // OutEnemyActors should be populated by the caller

    // 4. Update AI knowledge
    if (!UpdateAIKnowledge(PlayerPawn, OutEnemyActors, OutFinalIntents))
    {
        UE_LOG(LogPlayerMove, Error, TEXT("[Turn %d] Failed to update AI knowledge"), CurrentTurnId);
        return false;
    }

    UE_LOG(LogPlayerMove, Log, 
        TEXT("[Turn %d] Player move completion handled: %d enemies, %d intents"),
        CurrentTurnId, OutEnemyActors.Num(), OutFinalIntents.Num());

    return true;
}
```

### Phase 3: `GameTurnManagerBase`の簡素化

#### 3.1 `OnPlayerMoveCompleted`のリファクタリング

**Before** (134行):
```cpp
void AGameTurnManagerBase::OnPlayerMoveCompleted(const FGameplayEventData* Payload)
{
    // ... 134行の処理 ...
}
```

**After** (約40行):
```cpp
void AGameTurnManagerBase::OnPlayerMoveCompleted(const FGameplayEventData* Payload)
{
    if (!HasAuthority())
    {
        return;
    }

    UE_LOG(LogTurnManager, Log,
        TEXT("Turn %d: OnPlayerMoveCompleted Tag=%s"),
        CurrentTurnId, Payload ? *Payload->EventTag.ToString() : TEXT("NULL"));

    // Finalize player move
    AActor* CompletedActor = Payload ? const_cast<AActor*>(Cast<AActor>(Payload->Instigator)) : nullptr;
    FinalizePlayerMove(CompletedActor);

    // Delegate to PlayerMoveHandlerSubsystem
    UPlayerMoveHandlerSubsystem* MoveHandler = GetWorld()->GetSubsystem<UPlayerMoveHandlerSubsystem>();
    if (!MoveHandler)
    {
        UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] PlayerMoveHandlerSubsystem not available!"), CurrentTurnId);
        EndEnemyTurn();
        return;
    }

    // Refresh enemy roster
    APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
    RefreshEnemyRoster(PlayerPawn, CurrentTurnId, TEXT("EnemyPhaseStart"));
    TArray<AActor*> EnemyActors;
    CopyEnemyActors(EnemyActors);

    // Handle move completion and update AI knowledge
    TArray<FEnemyIntent> FinalIntents;
    if (!MoveHandler->HandlePlayerMoveCompletion(Payload, CurrentTurnId, EnemyActors, FinalIntents))
    {
        UE_LOG(LogTurnManager, Error, TEXT("[Turn %d] Failed to handle player move completion"), CurrentTurnId);
        EndEnemyTurn();
        return;
    }

    // Cache intents
    CachedEnemyIntents = FinalIntents;

    // Determine and execute phase
    const bool bHasAttack = DoesAnyIntentHaveAttack();
    if (bHasAttack)
    {
        ExecuteSequentialPhase();
    }
    else
    {
        ExecuteSimultaneousPhase();
    }
}
```

**注意**: フェーズ決定ロジック（Sequential/Simultaneous）は`GameTurnManagerBase`に残す。
これは高レベルのフロー制御であり、サブシステムに移譲すべきではない。

### Phase 4: テストとビルド

#### 4.1 コンパイル確認
```powershell
& "C:\Program Files\Epic Games\UE_5.6\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe" LyraEditor Win64 Development -Project="C:\UnrealProjects\RoguelikeDungeon\RoguelikeDungeon.uproject" -WaitMutex -NoHotReload -Verbose -NoUBA -Log="C:\UnrealProjects\RoguelikeDungeon\UBT_noUBA.log"
```

#### 4.2 動作確認項目
- [ ] プレイヤー移動後に敵Intentが正しく再生成される
- [ ] DistanceFieldが更新される
- [ ] Sequential/Simultaneousモードが正しく選択される
- [ ] ログ出力が適切に行われる

## 期待される成果

### コード削減
- **Before**: `OnPlayerMoveCompleted` 134行
- **After**: `OnPlayerMoveCompleted` 約40行
- **削減**: 約94行
- **新規追加**: `PlayerMoveHandlerSubsystem` 約200行（別ファイル）

### 品質向上
- ✅ **単一責任原則**: 各メソッドが明確な責務を持つ
- ✅ **テスト容易性**: サブシステムを個別にテスト可能
- ✅ **再利用性**: 他のシステムからも利用可能
- ✅ **可読性**: 各関数が短く理解しやすい

## 次のステップ

Step 8完了後:
- **Step 9**: 最終クリーンアップと検証
- 残りの大きな関数のリファクタリング検討
