# 修正計画書: INC-2025-1117D (v2 - 堅牢性の向上)

## 1. 概要

- **インシデントID:** INC-2025-1117D
- **問題:** 1タイルの移動で `PathNum=2` となる非効率なパスが生成されている。また、使用しているパス検索ロジックが、始点・終点が占有されているケースに弱く、将来的な不具合の原因となりうる。

## 2. 原因仮説と根拠

- **仮説1 (PathNum=2):** `UGridPathfindingSubsystem` のパス検索関数 (`FindPath` および `FindPathIgnoreEndpoints`) は、仕様として返すパス配列の先頭に「始点」を含んでいる。`GA_MoveBase` はこの始点を削除せずに `UnitMovementComponent` に渡している。
- **仮説2 (堅牢性):** 標準の `FindPath` 関数は、始点または終点のセルが「通行不可」とマークされている場合（例: 他のユニットに占有されている場合）、パス検索自体に失敗する。これにより、移動できるはずの状況で移動できなくなる可能性がある。
- **根拠:**
    - `GridPathfindingSubsystem.cpp` の実装を確認し、`FindPath` が始点を含むパスを返すこと、および始点/終点が通行不可の場合に即座に失敗することを突き止めた。
    - `FindPathIgnoreEndpoints` という、この問題を解決するために設計された関数が存在することを確認した。

## 3. 修正方針案

`GA_MoveBase` のパス生成ロジックを、堅牢かつ効率的になるように修正する。

1.  **`FindPathIgnoreEndpoints` の使用:**
    - `GA_MoveBase.cpp` 内で、パス検索を行う際に標準の `FindPath` ではなく、`FindPathIgnoreEndpoints` を使用する。これにより、始点や終点が占有されている状況でも安定してパスを検索できるようになる。

2.  **パス配列のトリミング:**
    - `FindPathIgnoreEndpoints` からパス配列 (`PathPoints`) を受け取った後、その要素数を確認する。
    - 要素数が `1` より大きい場合（始点＋1つ以上のウェイポイント）、`PathPoints.RemoveAt(0)` を呼び出して配列の先頭要素（始点）を削除する。

- **修正後 (イメージ):**
  ```cpp
  // GA_MoveBase.cpp -> StartMoveToCell()

  TArray<FVector> PathPoints;
  UGridPathfindingSubsystem* Pathfinding = GetGridPathfindingSubsystem();
  
  if (Pathfinding)
  {
      // 堅牢なFindPathIgnoreEndpointsを呼び出す
      Pathfinding->FindPathIgnoreEndpoints(StartPos, EndPos, PathPoints);
  }

  // パスに始点とそれ以外の点が含まれている場合、始点を削除する
  if (PathPoints.Num() > 1)
  {
      PathPoints.RemoveAt(0);
  }

  // FindPathが失敗した場合のフォールバック
  if (PathPoints.Num() == 0 && FVector::DistSquared(StartPos, EndPos) > 1.0f)
  {
      PathPoints.Add(EndPos);
  }

  Unit->MoveUnit(PathPoints);
  ```

## 4. 影響範囲

- **ユニット移動の堅牢性と効率化:**
    -   移動の成功率が向上し、他のユニットがいるセルへの移動（入れ替わりなど）や、自身のセルが何らかの理由で「通行不可」とマークされている状況でも、正しくパスを引けるようになる。
    -   全ての移動パスが最適化され、1タイル移動は `PathNum=1` で処理される。
- **副作用のリスク:**
    -   極めて低い。`FindPathIgnoreEndpoints` の使用と、その結果のトリミングは、どちらもシステムの設計思想に合致しており、より正しく、より安全な実装となる。

## 5. テスト方針

- **手動テスト:**
    -   プレイヤーを1タイル移動させ、`PathNum` が `1` になることをログで確認する。
    -   複数タイル移動させ、`PathNum` が移動タイル数と一致することを確認する。
    -   （可能であれば）敵ユニットがいるセルに移動を試み、パス検索が失敗せずに実行されることを確認する。
- **自動テスト:**
    -   既存のユニットテストで、パスのウェイポイント数が期待通りであることを検証するアサーションを更新・確認する。