# 修正計画書: INC-2025-1117-ROLLBACK (C++タグ設定のロールバック)

## 1. 概要

- **インシデントID:** INC-2025-1117-ROLLBACK
- **問題:** 敵の攻撃アビリティが起動しない。
- **根本原因 (確定):** `GA_MeleeAttack` アセットは変更されておらず、元々 `GameplayEvent.Intent.Attack` をリッスンしている。しかし、これまでのC++コードの修正によって、`AttackPhaseExecutor` や `GA_AttackBase` が `AI.Intent.Attack` を送信/登録するように変更されてしまっていたため、タグの不一致が発生していた。
- **解決策:** C++コード側を、アセットが期待する `GameplayEvent.Intent.Attack` に戻す。また、`state.turn.active` タグの管理も元々必要なかったはずなので削除する。

## 2. 修正方針案

これまでの `AI.Intent.Attack` への変更をすべて `GameplayEvent.Intent.Attack` に戻し、`state.turn.active` タグの管理ロジックを削除する。

- **修正対象ファイル:**
    -   `Source/LyraGame/Rogue/Utility/RogueGameplayTags.h`
    -   `Source/LyraGame/Rogue/Utility/RogueGameplayTags.cpp`
    -   `Source/LyraGame/Rogue/Turn/AttackPhaseExecutorSubsystem.cpp`
    -   `Source/LyraGame/Rogue/Abilities/GA_AttackBase.cpp`

- **修正内容:**

  **1. `Utility/RogueGameplayTags.h` の修正:**
  -   `LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Turn_Active);` の行を**削除**する。

  **2. `Utility/RogueGameplayTags.cpp` の修正:**
  -   `UE_DEFINE_GAMEPLAY_TAG(RogueGameplayTags::State_Turn_Active, "State.Turn.Active");` の行を**削除**する。

  **3. `Turn/AttackPhaseExecutorSubsystem.cpp` の修正:**

  **`UAttackPhaseExecutorSubsystem::DispatchNext` 内:**
  -   `Payload.EventTag = Action.FinalAbilityTag;` の行を、`Payload.EventTag = RogueGameplayTags::GameplayEvent_Intent_Attack;` に変更する。
  -   `State_Turn_Active` タグの追加 (`ASC->AddLooseGameplayTag(...)`) および関連する `UE_LOG` の行を**削除**する。
  -   `UE_LOG` の出力で `Action.FinalAbilityTag` を参照している箇所を `RogueGameplayTags::GameplayEvent_Intent_Attack` に変更する。

  **`UAttackPhaseExecutorSubsystem::OnAbilityCompleted` 内:**
  -   `State_Turn_Active` タグの削除 (`WaitingASC->RemoveLooseGameplayTag(...)`) および関連する `UE_LOG` の行を**削除**する。

  **`UAttackPhaseExecutorSubsystem::DispatchNext` 内の失敗パス:**
  -   `State_Turn_Active` タグの削除 (`ASC->RemoveLooseGameplayTag(...)`) および関連する `UE_LOG` の行を**削除**する。

  **4. `Abilities/GA_AttackBase.cpp` の修正:**

  **`UGA_AttackBase::UGA_AttackBase` (コンストラクタ) 内:**
  -   `AbilityTriggers` に `RogueGameplayTags::AI_Intent_Attack` を設定している箇所を `RogueGameplayTags::GameplayEvent_Intent_Attack` に変更する。
  -   `AssetTags` に `RogueGameplayTags::AI_Intent_Attack` を追加している行を**削除**する。

  **`UGA_AttackBase::PostInitProperties` 内:**
  -   `AbilityTriggers` を `RogueGameplayTags::AI_Intent_Attack` に強制登録しているロジックを、`RogueGameplayTags::GameplayEvent_Intent_Attack` に変更する。
  -   `AssetTags` に `RogueGameplayTags::AI_Intent_Attack` を強制追加しているロジックを**削除**する。

## 3. 影響範囲

-   **敵の攻撃実行:** C++コードがアセットが期待するタグを送信/登録するようになるため、攻撃が正常に実行されるはず。
-   **副作用のリスク:**
    -   これまでの修正をロールバックするため、以前のバグが再発しないか慎重なテストが必要。

## 4. テスト方針

-   **手動テスト:**
    -   ゲームを起動し、敵が攻撃を選択する状況（シーケンシャルモードが発動する状況）でターンを進行させる。
    -   プレイヤーの移動後、敵の攻撃モーションが再生され、ダメージが適用されるなど、攻撃が正常に実行されることを確認する。
    -   ログを確認し、`GA_MeleeAttack` の `ActivateAbility` が呼び出されていることを確認する。
    -   `Sending GameplayEvent` のログで `Tag` が `GameplayEvent.Intent.Attack` になっていることを確認する。
