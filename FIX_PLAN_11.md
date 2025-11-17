# 修正計画書: INC-2025-1117-FINAL-FIX (攻撃アビリティのトリガータグ修正)

## 1. 概要

- **インシデントID:** INC-2025-1117-FINAL-FIX
- **問題:** 敵の攻撃アビリティ (`GA_MeleeAttack`) が、正しいトリガータグ (`AI.Intent.Attack`) を受信しているにもかかわらず起動しない。
- **根本原因:** `GA_AttackBase` (およびその派生クラス) が、起動トリガーとして `GameplayEvent.Intent.Attack` タグをリッスンするように設定されていた。一方で、システムは `AI.Intent.Attack` タグでイベントを送信しているため、イベントとトリガーが一致せず、アビリティが起動しなかった。

## 2. 修正方針案

`GA_AttackBase.cpp` 内で `AbilityTriggers` に設定しているゲームプレイタグを、`RogueGameplayTags::GameplayEvent_Intent_Attack` から `RogueGameplayTags::AI_Intent_Attack` に変更する。

- **修正対象ファイル:** `Source/LyraGame/Rogue/Abilities/GA_AttackBase.cpp`
- **修正箇所:**
    1.  `UGA_AttackBase::UGA_AttackBase` (コンストラクタ)
    2.  `UGA_AttackBase::PostInitProperties`

- **修正内容:**
  `Trigger.TriggerTag` に設定している値を、以下のように変更する。

  - **修正前:**
    ```cpp
    Trigger.TriggerTag = RogueGameplayTags::GameplayEvent_Intent_Attack;
    ```

  - **修正後:**
    ```cpp
    Trigger.TriggerTag = RogueGameplayTags::AI_Intent_Attack;
    ```
  (※ `RogueGameplayTags` に `AI_Intent_Attack` が定義されていることを前提とします。ログから存在することは確認済みです。)

## 3. 影響範囲

- **敵の攻撃実行:** この修正により、`AttackPhaseExecutorSubsystem` から送信される `AI.Intent.Attack` イベントに `GA_AttackBase` (およびその派生クラス) が正しく反応し、攻撃アビリティが起動するようになる。
- **副作用のリスク:**
    - 極めて低い。これは、イベントを送信する側と受信する側でタグを一致させるだけの、明確なバグ修正である。

## 4. テスト方針

- **手動テスト:**
    - ゲームを起動し、敵が攻撃を選択する状況（シーケンシャルモードが発動する状況）でターンを進行させる。
    - プレイヤーの移動後、敵の攻撃モーションが再生され、ダメージが適用されるなど、攻撃が正常に実行されることを確認する。
    - ログを確認し、`GA_MeleeAttack` の `ActivateAbility` が呼び出されていることを確認する。
