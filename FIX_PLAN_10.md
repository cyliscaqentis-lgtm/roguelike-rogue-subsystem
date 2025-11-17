# 修正計画書: INC-2025-1117-DIAG (アビリティブロックタグ診断ログ)

## 1. 概要

- **インシデントID:** INC-2025-1117-DIAG
- **問題:** 敵の攻撃アビリティ (`GA_MeleeAttack`) が、正しいトリガータグ (`AI.Intent.Attack`) を受信しているにもかかわらず起動しない。`GA_MeleeAttack` は `State.Ability.Executing` タグによって起動がブロックされる設定になっている。
- **目的:** `AttackPhaseExecutorSubsystem` が `HandleGameplayEvent` を呼び出す直前に、対象ユニットの `AbilitySystemComponent (ASC)` に `State.Ability.Executing` タグ、またはその他のブロックタグが付与されているかどうかを診断する。

## 2. 修正方針案

`AttackPhaseExecutorSubsystem.cpp` の `DispatchNext` 関数内に、診断用のログ出力を追加する。

- **修正対象ファイル:** `Source/LyraGame/Rogue/Turn/AttackPhaseExecutorSubsystem.cpp`
- **修正対象関数:** `UAttackPhaseExecutorSubsystem::DispatchNext`

- **追加するログ:**
  `ASC->HandleGameplayEvent` を呼び出す直前に、以下のログを追加する。

  ```cpp
  // UAttackPhaseExecutorSubsystem::DispatchNext 内

  // ... (既存のログ出力やPayloadの設定) ...

  // ★★★ DIAGNOSTIC LOG START ★★★
  FGameplayTagContainer CurrentASCTags;
  ASC->GetOwnedGameplayTags(CurrentASCTags);
  UE_LOG(LogAttackPhase, Warning,
      TEXT("[Turn %d] DIAG: Attacker %s ASC Tags: %s"),
      TurnId, *Attacker->GetName(), *CurrentASCTags.ToStringSimple());
  // ★★★ DIAGNOSTIC LOG END ★★★

  const int32 TriggeredCount = ASC->HandleGameplayEvent(Payload.EventTag, &Payload);
  // ...
  ```

## 3. 影響範囲

-   **コードの動作:** 変更なし。ログ出力のみ。
-   **ログ出力:** `AttackPhaseExecutorSubsystem` がアビリティをディスパッチする際に、対象ユニットの `ASC` が持つ全てのゲームプレイタグがログに出力されるようになる。これにより、`State.Ability.Executing` タグが存在するかどうかを明確に確認できる。

## 4. テスト方針

-   **手動テスト:**
    -   ゲームを起動し、敵が攻撃を選択する状況（シーケンシャルモードが発動する状況）でターンを進行させる。
    -   ログを確認し、`DIAG: Attacker %s ASC Tags:` で始まる行を探す。
    -   そのログに `State.Ability.Executing` タグが含まれているかどうかを確認する。
    -   もし含まれていれば、そのタグが `GA_MeleeAttack` の起動をブロックしている原因であると確定できる。
