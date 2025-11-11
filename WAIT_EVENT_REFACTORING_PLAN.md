# Wait Gameplay Event パターン - リファクタリング計画書

## 目的

プレイヤー移動の「承認フェーズ」と「実行フェーズ」を正しく分離し、以下の問題を解決する：

1. **同時移動の実現**: プレイヤーと敵が真に同時に移動する
2. **競合状態の防止**: プレイヤーの移動が敵のAI決定より先に完了しない
3. **拡張性の確保**: 複雑な行動解決（Conflict Resolution）の実装を可能にする

## 現状の問題

### 1. OnPlayerCommandAccepted の即時起動

**場所**: `Turn/GameTurnManagerBase.cpp:2613`

```cpp
// 【現状】承認フェーズ(OnPlayerCommandAccepted)でアビリティを即時起動している
const int32 TriggeredCount = ASC->HandleGameplayEvent(EventData.EventTag, &EventData);
```

**問題点**:
- 承認と実行が同時に行われる
- ExecutePlayerMove() が呼ばれないため、実行フェーズが機能していない
- プレイヤーの移動が敵より先に完了してしまう

### 2. ExecutePlayerMove の無効化

**場所**: `Turn/GameTurnManagerBase.cpp:2889`

```cpp
// ExecutePlayerMove();
// 【理由】二重実行を防ぐため無効化。OnPlayerCommandAccepted で即時起動している。
```

**問題点**:
- 本来の実行フェーズ関数が使われていない
- 同時移動を実現できない

## 実装計画

### Phase 1: ドキュメント化 ✅ 完了

- [x] OnPlayerCommandAccepted に現状の問題点を明記
- [x] ExecutePlayerMove に将来の実装方針を記載
- [x] ExecuteSimultaneousPhase の無効化理由を明確化

### Phase 2: GA_MoveBase に Wait Event 実装

#### 2.1 新しいイベントタグの定義 ✅ 完了

**ファイル**: `Utility/RogueGameplayTags.h`, `Utility/RogueGameplayTags.cpp`

```cpp
// Event.Turn.ExecuteMove - 移動実行を指示するイベント
LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Turn_ExecuteMove);
```

#### 2.2 GA_MoveBase のヘッダー変更 ✅ 完了

**ファイル**: `Abilities/GA_MoveBase.h`

```cpp
/** Wait Event Task - イベント待機用 */
UPROPERTY()
class UAbilityTask_WaitGameplayEvent* WaitEventTask;

/** イベント受信時のコールバック */
UFUNCTION()
void OnExecuteMoveEventReceived(FGameplayEventData Payload);

/** 移動実行を開始（イベント受信後に呼ばれる） */
void BeginMoveExecution();
```

#### 2.3 ActivateAbility のリファクタリング ⏳ 未実装

**ファイル**: `Abilities/GA_MoveBase.cpp`

**変更内容**:

1. **検証部分の分離**:
   - 現在の検証ロジック（セル計算、歩行可能性チェック等）はそのまま維持
   - 検証成功後、移動実行を開始せず Wait Event 状態に入る

2. **Wait Event Task の作成**:
   ```cpp
   // 検証完了後、実行は待機
   if (ShouldWaitForExecuteEvent())  // プレイヤーの場合のみ
   {
       WaitEventTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
           this,
           RogueGameplayTags::Event_Turn_ExecuteMove,
           nullptr,  // OptionalExternalTarget
           true,     // OnlyTriggerOnce
           true      // OnlyMatchExact
       );

       WaitEventTask->EventReceived.AddDynamic(this, &UGA_MoveBase::OnExecuteMoveEventReceived);
       WaitEventTask->ReadyForActivation();

       UE_LOG(LogTurnManager, Log, TEXT("[GA_MoveBase] Waiting for Event.Turn.ExecuteMove"));
       return;  // ここで待機状態に入る
   }

   // 敵の場合は即時実行（従来通り）
   BeginMoveExecution();
   ```

3. **BeginMoveExecution() の実装**:
   ```cpp
   void UGA_MoveBase::BeginMoveExecution()
   {
       // 現在の ActivateAbility の移動処理部分をここに移動
       // - NextTileStep の計算
       - Walkability check
       - MoveToLocation 呼び出し
       // ... 等
   }
   ```

4. **コールバックの実装**:
   ```cpp
   void UGA_MoveBase::OnExecuteMoveEventReceived(FGameplayEventData Payload)
   {
       UE_LOG(LogTurnManager, Log, TEXT("[GA_MoveBase] Event.Turn.ExecuteMove received, starting execution"));
       BeginMoveExecution();
   }
   ```

#### 2.4 プレイヤー/敵の判別ヘルパー

```cpp
bool UGA_MoveBase::ShouldWaitForExecuteEvent() const
{
    AActor* Avatar = GetAvatarActorFromActorInfo();
    if (!Avatar) return false;

    // プレイヤーの場合のみ Wait Event を使用
    APlayerController* PC = Cast<APlayerController>(Avatar->GetController());
    return PC != nullptr;
}
```

### Phase 3: OnPlayerCommandAccepted の変更

**ファイル**: `Turn/GameTurnManagerBase.cpp:2613`

**変更前**:
```cpp
const int32 TriggeredCount = ASC->HandleGameplayEvent(EventData.EventTag, &EventData);
```

**変更後**:
```cpp
// ★★★ Phase 3 (2025-11-11): 承認フェーズでは起動のみ、実行は待機 ★★★
// アビリティを起動し、Wait Event 状態に入れる
// 実際の移動は ExecutePlayerMove() でイベント送信により開始される
const int32 TriggeredCount = ASC->HandleGameplayEvent(EventData.EventTag, &EventData);

if (TriggeredCount > 0)
{
    UE_LOG(LogTurnManager, Log,
        TEXT("[GameTurnManager] Move ability activated and waiting for execute event"));
    bPlayerMoveInProgress = true;  // ★ この時点では「承認済み・待機中」

    // ACK送信等の処理は従来通り
    // ...
}
```

### Phase 4: ExecutePlayerMove の復活

**ファイル**: `Turn/GameTurnManagerBase.cpp:3309`

**変更内容**:

```cpp
void AGameTurnManagerBase::ExecutePlayerMove()
{
    if (!HasAuthority())
    {
        return;
    }

    APawn* PlayerPawn = GetPlayerPawn();
    if (!PlayerPawn)
    {
        UE_LOG(LogTurnManager, Error, TEXT("[ExecutePlayerMove] PlayerPawn not found"));
        return;
    }

    UAbilitySystemComponent* ASC = GetPlayerASC();
    if (!ASC)
    {
        UE_LOG(LogTurnManager, Error, TEXT("[ExecutePlayerMove] ASC not found"));
        return;
    }

    // ★★★ Phase 4: イベント送信で待機中のアビリティを再開 ★★★
    // HandleGameplayEvent() で起動するのではなく、イベントを送信する
    FGameplayEventData EventData;
    EventData.EventTag = RogueGameplayTags::Event_Turn_ExecuteMove;
    EventData.Instigator = PlayerPawn;
    EventData.Target = PlayerPawn;
    EventData.OptionalObject = this;

    // 方向情報は既に承認フェーズで渡されているため、ここでは不要
    // Wait Event 状態のアビリティにシグナルを送るだけ

    UE_LOG(LogTurnManager, Log,
        TEXT("[ExecutePlayerMove] Sending Event.Turn.ExecuteMove to waiting ability"));

    ASC->HandleGameplayEvent(EventData.EventTag, &EventData);
}
```

**ExecuteSimultaneousPhase での呼び出し復活**:

**ファイル**: `Turn/GameTurnManagerBase.cpp:2889`

```cpp
//==========================================================================
// ★★★ Phase 4: プレイヤーの移動実行 ★★★
//==========================================================================
ExecutePlayerMove();  // イベント送信で待機中のアビリティを再開

//==========================================================================
// (1) 敵の移動フェーズ。同時実行
//==========================================================================
ExecuteMovePhase();
```

## 実装の効果

### Before (現状)
```
1. OnPlayerCommandAccepted: アビリティ起動 → 即座に移動開始 → 移動完了
2. ExecuteSimultaneousPhase: 何もしない（コメントアウト済み）
3. ExecuteMovePhase: 敵の移動開始

結果: プレイヤーが先に移動完了 → 敵の移動開始（非同時）
```

### After (実装後)
```
1. OnPlayerCommandAccepted: アビリティ起動 → Wait Event 状態に入る（移動は開始しない）
2. ExecuteSimultaneousPhase:
   - ExecutePlayerMove: Event.Turn.ExecuteMove 送信 → プレイヤーの移動開始
   - ExecuteMovePhase: 敵の移動開始
3. 両者が同時に移動開始 → Barrier で同期 → 同時に完了

結果: プレイヤーと敵が真に同時に移動
```

## テスト計画

### 1. プレイヤー単独移動

**シナリオ**: 敵がいない状態でプレイヤーが移動

**期待動作**:
- OnPlayerCommandAccepted でアビリティ起動
- Wait Event 状態に入る
- ExecutePlayerMove でイベント送信
- 移動開始 → 完了

**確認ログ**:
```
[GA_MoveBase] Waiting for Event.Turn.ExecuteMove
[ExecutePlayerMove] Sending Event.Turn.ExecuteMove to waiting ability
[GA_MoveBase] Event.Turn.ExecuteMove received, starting execution
```

### 2. 同時移動

**シナリオ**: プレイヤーと敵が同時に移動

**期待動作**:
- プレイヤーと敵が同じタイミングで移動開始
- Barrier で両者の完了を待機
- 両者が完了後、次フェーズに進行

**確認ログ**:
```
[ExecutePlayerMove] Sending Event.Turn.ExecuteMove
[ExecuteMovePhase] Processing enemy intents
[Barrier] Waiting for 2 actions (Player + Enemy)
[Barrier] All actions completed
```

### 3. 競合解決

**シナリオ**: プレイヤーと敵が同じセルに移動しようとする

**期待動作**:
- 予約システムで競合検出
- 先に予約した方が移動成功
- 後から予約した方は待機または別の行動

## 注意事項

### 1. Barrier の完了待機

プレイヤーと敵が同時に移動を開始するため、Barrier システムが正しく両者を追跡する必要がある。

### 2. ネットワーク同期

クライアント/サーバーでの動作確認が重要。特に：
- アビリティの起動タイミング
- イベントの送受信
- 移動完了の同期

### 3. エラーハンドリング

Wait Event 状態でアビリティがキャンセルされた場合の処理：
```cpp
virtual void EndAbility(...)
{
    if (WaitEventTask)
    {
        WaitEventTask->EndTask();
        WaitEventTask = nullptr;
    }

    Super::EndAbility(...);
}
```

## 実装優先度

1. **High**: Phase 2.3（ActivateAbility のリファクタリング）
   - 最も複雑で影響範囲が大きい
   - 慎重なテストが必要

2. **Medium**: Phase 3（OnPlayerCommandAccepted の変更）
   - Phase 2 完了後に実施
   - 比較的小さな変更

3. **Medium**: Phase 4（ExecutePlayerMove の復活）
   - Phase 3 完了後に実施
   - 比較的小さな変更

4. **Low**: テストとデバッグ
   - 全Phase完了後に実施
   - 段階的にテストケースを追加

## 関連ファイル

- `Abilities/GA_MoveBase.h`
- `Abilities/GA_MoveBase.cpp`
- `Turn/GameTurnManagerBase.cpp`
- `Utility/RogueGameplayTags.h`
- `Utility/RogueGameplayTags.cpp`

## 参考資料

- UAbilityTask_WaitGameplayEvent の公式ドキュメント
- Lyra プロジェクトの Gameplay Ability 実装例
- UE5 GAS（Gameplay Ability System）ベストプラクティス

---

**作成日**: 2025-11-11
**作成者**: ClaudeCode
**ステータス**: Phase 1 完了、Phase 2-4 未実装
