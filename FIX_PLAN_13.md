# 修正計画書: INC-2025-1117-FINAL-FIX-R3 (State.Turn.Activeタグの定義と管理)

## 1. 概要

- **インシデントID:** INC-2025-1117-FINAL-FIX-R3
- **問題:** 敵の攻撃アビリティが起動しない。`ActivationRequiredTags` に `state.turn.active` が含まれているが、このタグがユニットの `ASC` に付与されておらず、アビリティの起動条件を満たせない。
- **根本原因:** `state.turn.active` タグが `RogueGameplayTags.h` に定義されておらず、「Invalid GameplayTag」警告が出ていた。このタグがシステムに存在しないため、アビリティの `ActivationRequiredTags` チェックが常に失敗していた。

## 2. 修正方針案

1.  **`State.Turn.Active` タグの定義:**
    -   `Utility/RogueGameplayTags.h` に `State_Turn_Active` タグを宣言する。
    -   `Utility/RogueGameplayTags.cpp` に `State_Turn_Active` タグを定義する。
2.  **`AttackPhaseExecutorSubsystem` でのタグ管理:**
    -   `AttackPhaseExecutorSubsystem.cpp` の `DispatchNext` 関数内で、`HandleGameplayEvent` を呼び出す前に、攻撃ユニットの `ASC` に `RogueGameplayTags::State_Turn_Active` を追加する。
    -   `AttackPhaseExecutorSubsystem.cpp` の `OnAbilityCompleted` 関数内（および `DispatchNext` 内の失敗パス）で、アビリティが完了したユニットの `ASC` から `RogueGameplayTags::State_Turn_Active` を削除する。

- **修正対象ファイル:**
    -   `Source/LyraGame/Rogue/Utility/RogueGameplayTags.h`
    -   `Source/LyraGame/Rogue/Utility/RogueGameplayTags.cpp`
    -   `Source/LyraGame/Rogue/Turn/AttackPhaseExecutorSubsystem.cpp`

- **修正内容:**

  **1. `Utility/RogueGameplayTags.h` の修正:**
  ```cpp
  // State tags -------------------------------------------------------------
  LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Ability_Executing);
  LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Presentation_Dash);
  LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Moving);
  LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Turn_Active); // ★★★ この行を追加 ★★★
  ```

  **2. `Utility/RogueGameplayTags.cpp` の修正:**
  ```cpp
  // State tags -------------------------------------------------------------
  UE_DEFINE_GAMEPLAY_TAG(RogueGameplayTags::State_Ability_Executing, "State.Ability.Executing");
  UE_DEFINE_GAMEPLAY_TAG(RogueGameplayTags::State_Presentation_Dash, "State.Presentation.Dash");
  UE_DEFINE_GAMEPLAY_TAG(RogueGameplayTags::State_Moving, "State.Moving");
  UE_DEFINE_GAMEPLAY_TAG(RogueGameplayTags::State_Turn_Active, "State.Turn.Active"); // ★★★ この行を追加 ★★★
  ```

  **3. `Turn/AttackPhaseExecutorSubsystem.cpp` の修正:**

  **`UAttackPhaseExecutorSubsystem::DispatchNext` 内:**
  ```cpp
  // ... (既存のASC取得ロジックの後) ...

  if (!ASC)
  {
      // ... (既存のログとスキップロジック) ...
  }

  // ★★★ ADD: State.Turn.Active タグを付与 ★★★
  ASC->AddLooseGameplayTag(RogueGameplayTags::State_Turn_Active);
  UE_LOG(LogAttackPhase, Verbose,
      TEXT("[Turn %d] Added State.Turn.Active to %s"),
      TurnId, *Attacker->GetName());

  BindASC(ASC);

  FGameplayEventData Payload;
  Payload.EventTag = Action.FinalAbilityTag;
  // ... (既存のPayload設定) ...

  const int32 TriggeredCount = ASC->HandleGameplayEvent(Payload.EventTag, &Payload);

  if (TriggeredCount > 0)
  {
      UE_LOG(LogAttackPhase, Log,
          TEXT("[Turn %d] Dispatched attack %d/%d: %s (Tag=%s)"),
          TurnId, CurrentIndex + 1, Queue.Num(),
          *Attacker->GetName(), *Action.FinalAbilityTag.ToString());
  }
  else
  {
      UE_LOG(LogAttackPhase, Warning,
          TEXT("[Turn %d] %s: %s failed to trigger any abilities"),
          TurnId, *Attacker->GetName(), *Action.FinalAbilityTag.ToString());

      // ★★★ ADD: 失敗時もタグを削除 ★★★
      if (ASC)
      {
          ASC->RemoveLooseGameplayTag(RogueGameplayTags::State_Turn_Active);
          UE_LOG(LogAttackPhase, Verbose,
              TEXT("[Turn %d] Removed State.Turn.Active from %s (failure path)"),
              TurnId, *Attacker->GetName());
      }

      UnbindCurrentASC();
      CurrentIndex++;
      DispatchNext();
  }
  ```

  **`UAttackPhaseExecutorSubsystem::OnAbilityCompleted` 内:**
  ```cpp
  void UAttackPhaseExecutorSubsystem::OnAbilityCompleted(const FGameplayEventData* Payload)
  {
      UE_LOG(LogAttackPhase, Log,
          TEXT("[Turn %d] Ability completed at index %d"),
          TurnId, CurrentIndex);

      // ★★★ ADD: 成功時もタグを削除 ★★★
      if (WaitingASC.IsValid())
      {
          if (UAbilitySystemComponent* ASC = WaitingASC.Get())
          {
              ASC->RemoveLooseGameplayTag(RogueGameplayTags::State_Turn_Active);
              UE_LOG(LogAttackPhase, Verbose,
                  TEXT("[Turn %d] Removed State.Turn.Active from %s (success path)"),
                  TurnId, *ASC->GetOwner()->GetName());
          }
      }

      UnbindCurrentASC();
      CurrentIndex++;
      DispatchNext();
  }
  ```

## 3. 影響範囲

-   **敵の攻撃実行:** この修正により、`state.turn.active` タグが正しく定義され、攻撃アビリティの起動条件が満たされるため、攻撃が正常に実行されるようになる。
-   **副作用のリスク:**
    -   `state.turn.active` タグが他の場所で不適切に利用されている場合、影響が出る可能性がある。しかし、このタグは `ActivationRequiredTags` でのみ参照されているため、そのリスクは低い。

## 4. テスト方針

-   **手動テスト:**
    -   ゲームを起動し、敵が攻撃を選択する状況（シーケンシャルモードが発動する状況）でターンを進行させる。
    -   プレイヤーの移動後、敵の攻撃モーションが再生され、ダメージが適用されるなど、攻撃が正常に実行されることを確認する。
    -   ログを確認し、`GA_MeleeAttack` の `ActivateAbility` が呼び出されていることを確認する。
    -   `DIAG` ログで `State.Turn.Active` が付与・削除されていることを確認する。
