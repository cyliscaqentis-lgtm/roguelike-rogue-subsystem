# 修正計画書: INC-2025-1117-FINAL-FIX-R2 (攻撃アビリティのAssetTags修正)

## 1. 概要

- **インシデントID:** INC-2025-1117-FINAL-FIX-R2
- **問題:** 敵の攻撃アビリティ (`GA_MeleeAttack`) が、正しいトリガータグ (`AI.Intent.Attack`) を受信しているにもかかわらず起動しない。`AbilityTriggers` の設定は正しいが、依然として `HandleGameplayEvent` が `0` を返している。
- **根本原因:** `UAbilitySystemComponent::HandleGameplayEvent` の内部実装、またはプロジェクト固有のカスタムロジックが、アビリティの `AssetTags` も参照している可能性がある。現在、`GA_AttackBase` の `AssetTags` には `AI.Intent.Attack` が含まれていないため、これが起動失敗の最後の原因である可能性が高い。

## 2. 修正方針案

`GA_AttackBase.cpp` のコンストラクタ内で、アビリティの `AssetTags` に `AI.Intent.Attack` を追加する。これにより、アビリティの起動トリガー (`AbilityTriggers`) と、アビリティ自体の識別タグ (`AssetTags`) の両方が、システムが送信するイベントと一致するようになる。

- **修正対象ファイル:** `Source/LyraGame/Rogue/Abilities/GA_AttackBase.cpp`
- **修正対象関数:** `UGA_AttackBase::UGA_AttackBase` (コンストラクタ)

- **修正内容:**
  `SetAssetTags(Tags);` を呼び出す前に、`Tags` コンテナに `AI.Intent.Attack` を追加する。

  - **修正前:**
    ```cpp
    FGameplayTagContainer Tags;
    Tags.AddTag(RogueGameplayTags::Ability_Category_Attack);  // ネイティブタグを使用
    SetAssetTags(Tags);
    ```

  - **修正後:**
    ```cpp
    FGameplayTagContainer Tags;
    Tags.AddTag(RogueGameplayTags::Ability_Category_Attack);  // ネイティブタグを使用
    Tags.AddTag(RogueGameplayTags::AI_Intent_Attack); // ★★★ トリガーイベントと一致させるために追加 ★★★
    SetAssetTags(Tags);
    ```

## 3. 影響範囲

- **敵の攻撃実行:** この修正により、`GA_AttackBase` (およびその派生クラス) が `AI.Intent.Attack` イベントに完全に紐づけられ、攻撃アビリティが正しく起動するようになる。
- **副作用のリスク:**
    - 極めて低い。`AssetTags` はアビリティを識別・分類するためのものであり、ここに対応するイベントタグを追加することは、意図を明確にする上でむしろ望ましい。

## 4. テスト方針

- **手動テスト:**
    - ゲームを起動し、敵が攻撃を選択する状況（シーケンシャルモードが発動する状況）でターンを進行させる。
    - プレイヤーの移動後、敵の攻撃モーションが再生され、ダメージが適用されるなど、攻撃が正常に実行されることを確認する。
    - ログを確認し、`GA_MeleeAttack` の `ActivateAbility` が呼び出されていることを確認する。
