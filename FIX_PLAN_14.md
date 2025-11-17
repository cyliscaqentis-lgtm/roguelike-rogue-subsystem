# 修正計画書: INC-2025-1117-FINAL-FIX-R4 (C++によるトリガー/アセットタグの強制上書き)

## 1. 概要

- **インシデントID:** INC-2025-1117-FINAL-FIX-R4
- **問題:** 敵の攻撃アビリティが起動しない。
- **根本原因 (確定):** `GA_MeleeAttack` のBlueprintアセットに設定されているトリガータグが、C++側が送信するイベントタグと不一致。C++のコンストラクタや `PostInitProperties` での修正が、ランタイムで有効になっていない。
- **解決策:** C++の `PostInitProperties` 内で、`AbilityTriggers` と `AssetTags` の両方に、正しいトリガータグ (`AI.Intent.Attack`) を強制的に設定し、Blueprintによる上書きを無効化する。

## 2. 修正方針案

`GA_AttackBase.cpp` の `PostInitProperties` 関数を拡張し、`AbilityTriggers` と `AssetTags` の両方が `AI.Intent.Attack` を含むことを保証する。

- **修正対象ファイル:** `Source/LyraGame/Rogue/Abilities/GA_AttackBase.cpp`
- **修正対象関数:** `UGA_AttackBase::PostInitProperties`

- **修正内容:**
  `PostInitProperties` の実装を、以下のロジックに置き換える。

  ```cpp
  void UGA_AttackBase::PostInitProperties()
  {
      Super::PostInitProperties();

      const FGameplayTag RequiredTriggerTag = RogueGameplayTags::AI_Intent_Attack;

      // 1. AbilityTriggers のチェックと修正
      bool bTriggerNeedsFix = true;
      for (const FAbilityTriggerData& Trigger : AbilityTriggers)
      {
          if (Trigger.TriggerTag == RequiredTriggerTag && Trigger.TriggerSource == EGameplayAbilityTriggerSource::GameplayEvent)
          {
              bTriggerNeedsFix = false;
              break;
          }
      }

      if (bTriggerNeedsFix)
      {
          // 古いトリガーを削除し、正しいトリガーを追加する
          AbilityTriggers.RemoveAll([&](const FAbilityTriggerData& Trigger)
          {
              return Trigger.TriggerSource == EGameplayAbilityTriggerSource::GameplayEvent;
          });

          FAbilityTriggerData NewTrigger;
          NewTrigger.TriggerTag = RequiredTriggerTag;
          NewTrigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
          AbilityTriggers.Add(NewTrigger);

          UE_LOG(LogTemp, Warning,
              TEXT("[GA_AttackBase::PostInitProperties] ★ FIXED: Re-registered trigger %s (was missing or incorrect)"),
              *RequiredTriggerTag.ToString());
      }

      // 2. AssetTags のチェックと修正
      if (!HasAssetTag(RequiredTriggerTag))
      {
          FGameplayTagContainer MutableTags = GetAssetTags();
          MutableTags.AddTag(RequiredTriggerTag);
          SetAssetTags(MutableTags);

          UE_LOG(LogTemp, Warning,
              TEXT("[GA_AttackBase::PostInitProperties] ★ FIXED: Added missing AssetTag %s"),
              *RequiredTriggerTag.ToString());
      }
  }
  ```
  **注:** この修正は、既存の `PostInitProperties` のロジックを、より堅牢なチェックと修正のロジックで完全に置き換えるものです。

## 3. 影響範囲

- **敵の攻撃実行:** この修正により、Blueprintアセットの設定に関わらず、C++がランタイムで正しいトリガーとアセットタグを強制するため、攻撃アビリティが正しく起動するようになる。
- **副作用のリスク:**
    - 極めて低い。意図した動作（`AI.Intent.Attack` での起動）を強制するものであり、他のロジックへの影響はない。

## 4. テスト方針

- **手動テスト:**
    - ゲームを起動し、敵が攻撃を選択する状況（シーケンシャルモードが発動する状況）でターンを進行させる。
    - プレイヤーの移動後、敵の攻撃モーションが再生され、ダメージが適用されるなど、攻撃が正常に実行されることを確認する。
    - ログを確認し、`GA_MeleeAttack` の `ActivateAbility` が呼び出されていることを確認する。
    - ログに `★ FIXED:` の警告が出力され、タグが修正されたことを確認する。
