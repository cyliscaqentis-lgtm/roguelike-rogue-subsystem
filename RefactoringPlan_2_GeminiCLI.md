# リファクタリング計画 Part 2

## 概要

`codebase_investigator`による分析の結果、新たなリファクタリング対象として「ステータス管理の欠陥」と「AUnitManagerの過剰な責務」が特定されました。この計画書では、これらの問題を解決するための具体的な手順を定義します。

---

## 1. ステータス管理のリファクタリング (データ重複の排除)

#### **目的**
`AUnitBase` 内に存在するステータスの三重管理（`AUnitManager`内の配列、`AUnitBase`内の`FUnitStatBlock`構造体、`AUnitBase`内の個別プロパティ）を解消します。`FUnitStatBlock` を唯一のデータソース（Single Source of Truth）とし、メンテナンス性とデータ整合性を向上させます。

#### **影響範囲**
- `Character/UnitBase.h`
- `Character/UnitBase.cpp`
- `Character/UnitManager.h`
- `Character/UnitManager.cpp`

#### **手順**

1.  **`AUnitBase` からの個別ステータスプロパティの削除:**
    *   `AUnitBase.h` を編集し、`StatBlock` 構造体の内容と重複している以下のメンバー変数をすべて削除します。
        *   `MaxHP`, `CurrentHP`, `Attack`, `Defense`, `Evasion`, `Accuracy`, etc...

2.  **`StatBlock` へのアクセサの統一:**
    *   `AUnitBase` の内外からステータスにアクセスする必要がある場合は、すべて `StatBlock` 構造体を介して行うようにコードを修正します。
    *   例: `Unit->CurrentHP` は `Unit->StatBlock.CurrentHP` に置き換えます。
    *   必要であれば、`AUnitBase` に `GetStatBlock()` や `GetMutableStatBlock()` といったインライン関数を用意し、アクセスを容易にします。

3.  **`AUnitManager` の責務変更:**
    *   `AUnitManager.cpp` にある `CalculateDerivedValues` のようなステータス計算ロジックを `AUnitManager` から削除します。
    *   このロジックは、`FUnitStatBlock` 構造体自身のメンバー関数（例: `FUnitStatBlock::RecalculateDerivedStats()`)、または `UGridUtils` のようなスタティックなユーティリティライブラリに移動します。
    *   `AUnitManager` はユニットの `StatBlock` を保持・管理する役割に徹し、計算ロジックは持たないようにします。

4.  **`SetStatVars` 関数の削除:**
    *   `AUnitBase.cpp` にある `SetStatVars` 関数を削除します。この関数はデータ重複の根源であり、不要になります。

---

## 2. `AUnitManager` の責務分離 (スポーン機能の抽出)

#### **目的**
`AUnitManager` が持つ複数の責務のうち、「ユニットの生成（スポーン）」に関するロジックを新しい専用のサブシステムに抽出し、`AUnitManager` の責務を単一化します。

#### **影響範囲**
- `Character/UnitManager.h`
- `Character/UnitManager.cpp`
- （新規作成）`Character/UnitSpawnerSubsystem.h`
- （新規作成）`Character/UnitSpawnerSubsystem.cpp`

#### **手順**

1.  **`UnitSpawnerSubsystem` の新規作成:**
    *   `UUnitSpawnerSubsystem` という名前で、新しい `UWorldSubsystem` を作成します。
    *   このサブシステムは、ユニットの生成と配置に特化した責務を持ちます。

2.  **スポーンロジックの移行:**
    *   `AUnitManager` から、ユニットを生成・配置しているロジック（例: `SpawnUnit`, `PlaceUnitOnGrid` など、関連する可能性のある関数）を特定し、`UUnitSpawnerSubsystem` に移動します。
    *   `DungeonConfig` や `AIProfileDataAsset` などのデータアセットへの依存も、新しいサブシステムが担当するようにします。

3.  **`AUnitManager` のスリム化:**
    *   移行したスポーン関連のコードを `AUnitManager` から削除します。
    *   `AUnitManager` は、生成済みの `AUnitBase` アクターを追跡・管理する責務に集中します。

4.  **呼び出し元の修正:**
    *   これまで `AUnitManager` のスポーン機能を呼び出していた箇所（例: `AGameTurnManagerBase` やダンジョン生成ロジック）を、`GetWorld()->GetSubsystem<UUnitSpawnerSubsystem>()` を介して新しいサブシステムを呼び出すように修正します。