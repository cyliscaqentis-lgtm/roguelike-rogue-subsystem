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

---

## Phase 2.4: キャンセルイベントサポート（2025-11-11 追加）

### 背景: 競合解決時のアビリティ終了問題

**発見された問題** (Turn 80 ログより):

```
LogTurnManager: Warning: [Turn 80] ★ Player movement CANCELLED by ConflictResolver
LogTurnManager: Warning: [CanAdvanceTurn] Barrier NOT quiescent: Turn=80 PendingActions=1
LogTurnManager: Error: [EndEnemyTurn] ABORT: Cannot end turn 80 (actions still in progress)
```

**原因**:
1. プレイヤーと敵が同じセルを予約（衝突）
2. ConflictResolverが両者の移動をキャンセル
3. **問題**: キャンセルされたGA_MoveBaseに終了通知が送られない
4. Barrierにアクションが残り続け、ターン終了がブロックされる

### 解決策: Event.Turn.ActionCancelled の導入

#### 2.4.1 新しいイベントタグ ✅ 完了

**ファイル**: `Utility/RogueGameplayTags.h`, `Utility/RogueGameplayTags.cpp`

```cpp
// Event.Turn.ActionCancelled - 競合解決によりアクションがキャンセルされた
LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Turn_ActionCancelled);
```

#### 2.4.2 GA_MoveBase のキャンセルイベント待機 ✅ 基盤完了

**ファイル**: `Abilities/GA_MoveBase.h`

```cpp
/** Wait Event Task - キャンセルイベント待機用 */
UPROPERTY()
TObjectPtr<class UAbilityTask_WaitGameplayEvent> WaitCancelEventTask;

/** キャンセルイベント受信時のコールバック */
UFUNCTION()
void OnActionCancelledEventReceived(FGameplayEventData Payload);
```

**実装** (Phase 2.3と並行):

```cpp
// ActivateAbility 内で2つのイベントを並列待機
void UGA_MoveBase::ActivateAbility(...)
{
    // ... 検証処理 ...

    if (ShouldWaitForExecuteEvent())  // プレイヤーの場合
    {
        // 実行イベントを待機
        WaitExecuteEventTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
            this,
            RogueGameplayTags::Event_Turn_ExecuteMove,
            nullptr, true, true
        );
        WaitExecuteEventTask->EventReceived.AddDynamic(
            this, &UGA_MoveBase::OnExecuteMoveEventReceived
        );
        WaitExecuteEventTask->ReadyForActivation();

        // キャンセルイベントを並列待機
        WaitCancelEventTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
            this,
            RogueGameplayTags::Event_Turn_ActionCancelled,
            nullptr, true, true
        );
        WaitCancelEventTask->EventReceived.AddDynamic(
            this, &UGA_MoveBase::OnActionCancelledEventReceived
        );
        WaitCancelEventTask->ReadyForActivation();

        UE_LOG(LogTurnManager, Log,
            TEXT("[GA_MoveBase] Waiting for Execute or Cancel event"));
        return;
    }

    // 敵は即時実行
    BeginMoveExecution();
}
```

**キャンセルコールバックの実装**:

```cpp
void UGA_MoveBase::OnActionCancelledEventReceived(FGameplayEventData Payload)
{
    AActor* Avatar = GetAvatarActorFromActorInfo();
    UE_LOG(LogTurnManager, Warning,
        TEXT("[GA_MoveBase] %s: Action CANCELLED by ConflictResolver"),
        *GetNameSafe(Avatar));

    // Wait Event タスクをクリーンアップ
    if (WaitExecuteEventTask)
    {
        WaitExecuteEventTask->EndTask();
        WaitExecuteEventTask = nullptr;
    }
    if (WaitCancelEventTask)
    {
        WaitCancelEventTask->EndTask();
        WaitCancelEventTask = nullptr;
    }

    // Barrierから登録を解除
    if (UTurnActionBarrierSubsystem* Barrier = GetBarrierSubsystem())
    {
        Barrier->CompleteAction(Avatar, MoveTurnId, MoveActionId);
        UE_LOG(LogTurnManager, Log,
            TEXT("[GA_MoveBase] Barrier action completed (cancelled): ActionId=%s"),
            *MoveActionId.ToString());
    }

    // アビリティをキャンセル終了
    CancelAbility(CachedSpecHandle, &CachedActorInfo, CachedActivationInfo, true);
}
```

#### 2.4.3 ConflictResolver の修正 ⏳ 未実装

**ファイル**: `Turn/GameTurnManagerBase.cpp` または `Turn/TurnCorePhaseManager.cpp`

**変更箇所**: ConflictResolverが移動をキャンセルした際の処理

**変更前**:
```cpp
// 競合検出 → アクターの移動をWAITに変更
LogTemp: Error: [TurnCore] ... MARKING AS WAIT (no movement)
```

**変更後**:
```cpp
// 競合検出 → アクターの移動をキャンセル + イベント送信
if (bConflictDetected)
{
    UE_LOG(LogTurnManager, Warning,
        TEXT("[ConflictResolver] Cancelling action for %s due to conflict"),
        *GetNameSafe(Actor));

    // アビリティにキャンセルを通知
    if (UAbilitySystemComponent* ASC = GetASCForActor(Actor))
    {
        FGameplayEventData EventData;
        EventData.EventTag = RogueGameplayTags::Event_Turn_ActionCancelled;
        EventData.Instigator = Actor;
        EventData.Target = Actor;
        EventData.OptionalObject = this;

        ASC->HandleGameplayEvent(EventData.EventTag, &EventData);
        UE_LOG(LogTurnManager, Log,
            TEXT("[ConflictResolver] Sent Event.Turn.ActionCancelled to %s"),
            *GetNameSafe(Actor));
    }
}
```

### テスト計画（Phase 2.4）

#### 1. 同一セル予約の競合

**シナリオ**: プレイヤーと敵が同じセルに移動しようとする

**期待動作**:
- ConflictResolverが衝突を検出
- 両者に`Event.Turn.ActionCancelled`を送信
- GA_MoveBaseが即座にキャンセル終了
- Barrierから登録が解除される
- ターンが正常に終了する

**確認ログ**:
```
[ConflictResolver] Cancelling action for BP_PlayerUnit_C_0 due to conflict
[ConflictResolver] Sent Event.Turn.ActionCancelled to BP_PlayerUnit_C_0
[GA_MoveBase] BP_PlayerUnit_C_0: Action CANCELLED by ConflictResolver
[GA_MoveBase] Barrier action completed (cancelled): ActionId=...
[CanAdvanceTurn] ✅OK: Turn=XX (Barrier=Quiet, InProgress=0)
```

#### 2. スワップ（入れ替わり）の検出

**シナリオ**: プレイヤーと敵が互いの位置に移動しようとする

**期待動作**:
- スワップ検出
- 両者の移動をキャンセル
- 両者がその場で待機
- ターンが正常に終了

#### 3. キャンセル後の再入力

**シナリオ**: Turn 80でキャンセル → Turn 81で別の方向に移動

**期待動作**:
- Turn 80: キャンセル完了、ターン終了
- Turn 81: 新しい入力窓が開く
- プレイヤーが正常に別方向に移動できる

### 実装の効果

#### Before (現状)
```
1. プレイヤー移動承認 → GA_MoveBase起動 → 即座に移動開始
2. ConflictResolver → 移動をキャンセル
3. 問題: GA_MoveBaseがBarrierに残り続ける
4. ターン終了がブロックされる
5. 最終的に移動完了後にターン進行（意図しない動作）
```

#### After (実装後)
```
1. プレイヤー移動承認 → GA_MoveBase起動 → Wait Event状態
2. ConflictResolver → Event.Turn.ActionCancelled送信
3. GA_MoveBase → キャンセルイベント受信 → 即座に終了
4. Barrierから登録解除
5. ターンが正常に終了
```

### 関連する問題

この修正により、以下の関連問題も解決されます：

1. **プレイヤー移動のキャンセル通知欠如**: 現在はキャンセルされてもクライアントに通知されない
2. **Barrier の不整合**: アクションがキャンセルされてもBarrierに残る
3. **ターン進行のブロック**: 未完了アクションがターン終了を妨げる

### 実装優先度

**Critical**: Phase 2.4はPhase 2.3と同時実装が推奨されます。

理由：
- Wait Event パターンを導入すると、キャンセル処理がないと重大な問題が発生
- ConflictResolverは既に実装されており、頻繁に発動する
- ターン進行がブロックされるとゲームが停止する

### 注意事項

1. **ネットワーク同期**: キャンセルイベントもサーバー/クライアント間で同期が必要
2. **アニメーションの停止**: 移動アニメーション開始後のキャンセルには追加処理が必要
3. **GridOccupancy の更新**: 予約のキャンセル時には占有情報も正しく更新する

---

**更新日**: 2025-11-11
**追加理由**: Turn 80 ログ解析により発見された競合解決時のアビリティ終了問題に対処
**ステータス**: Phase 2.4 基盤完了、実装は Phase 2.3 と並行実施予定
