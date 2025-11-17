# 修正計画書: INC-2025-1117-FINAL (攻撃タグの不整合問題)

## 1. 概要

- **インシデントID:** INC-2025-1117-FINAL
- **問題:** 逐次実行モードにおいて、敵の攻撃アビリティをトリガーするためのゲームプレイイベントタグが `None` になってしまい、攻撃が実行されない。
- **根本原因:** `AttackPhaseExecutorSubsystem` が、`FResolvedAction` 構造体からアビリティタグを読み出す際に、正しいフィールド (`FinalAbilityTag`) ではなく、空のフィールド (`AbilityTag`) を参照していた。

## 2. 原因仮説と根拠

- **仮説:** `ConflictResolverSubsystem` は解決済みアクションのタグを `FinalAbilityTag` に書き込んでいるが、`AttackPhaseExecutorSubsystem` は `AbilityTag` から読み込もうとしている。このフィールド名の不一致が原因で、タグ情報が失われている。
- **根拠:**
    - `ConflictResolverSubsystem.cpp` のコードレビューにより、`FResolvedAction` の `FinalAbilityTag` フィールドにタグを書き込んでいることを確認した。
    - `AttackPhaseExecutorSubsystem.cpp` のコードレビューにより、`FResolvedAction` の `AbilityTag` フィールドを読み込んでいることを確認した。
    - ログで `Sending GameplayEvent: Tag=None` と記録されている事実は、この不整合を裏付けている。

## 3. 修正方針案

`AttackPhaseExecutorSubsystem.cpp` の `DispatchNext` 関数内で、`FResolvedAction` から読み出すフィールドを `AbilityTag` から `FinalAbilityTag` に変更する。

- **修正対象ファイル:** `Source/LyraGame/Rogue/Turn/AttackPhaseExecutorSubsystem.cpp`
- **修正対象関数:** `UAttackPhaseExecutorSubsystem::DispatchNext`

- **修正前:**
  ```cpp
  Payload.EventTag = Action.AbilityTag;
  ```

- **修正後:**
  ```cpp
  Payload.EventTag = Action.FinalAbilityTag;
  ```
- **その他:** ログメッセージも `Action.FinalAbilityTag` を表示するように修正することが望ましい。
  ```cpp
  // 修正前
  // UE_LOG(..., *Action.AbilityTag.ToString(), ...);

  // 修正後
  UE_LOG(..., *Action.FinalAbilityTag.ToString(), ...);
  ```

## 4. 影響範囲

- **敵の攻撃実行:** この修正により、逐次実行モードで敵の攻撃が正しくトリガーされるようになる。
- **副作用のリスク:**
    - 極めて低い。これは、データの生成元と利用先での変数名の不一致を修正するだけの、非常に限定的で明確なバグ修正である。他のロジックへの影響はない。

## 5. テスト方針

- **手動テスト:**
    - 敵が攻撃を選択する状況（シーケンシャルモードが発動する状況）でターンを進行させる。
    - プレイヤーの移動後、敵の攻撃が正常に実行されることを確認する。
    - ログを確認し、`Sending GameplayEvent` のログで `Tag` が `GameplayEvent.Intent.Attack` のような正しい値になっていることを確認する。
