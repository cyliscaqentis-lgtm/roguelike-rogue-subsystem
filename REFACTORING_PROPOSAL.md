# コード重複除去・リファクタリング提案書

## 📋 エグゼクティブサマリー

本プロジェクトの分析により、**重複コード**と**不要コード**の両面で改善機会が特定されました。

### 主要な発見事項
- **コード総行数**: 21,251行（C++）
- **重複パターン**: 12カテゴリー、200+箇所
- **不要コード**: 200-300行
- **空実装ファイル**: 4ファイル
- **TODO/未実装**: 11箇所

### 推奨アクション
1. **高優先度**: Actorゲッター、座標ユーティリティの共通化（リスク: 低、価値: 高）
2. **中優先度**: 不要コード・コメントアウトコードの削除
3. **低優先度**: ロギングマクロ、タグユーティリティの整備

---

## 🎯 Phase 1: 高優先度リファクタリング（低リスク・高価値）

### 1.1 Actor/Subsystemゲッターの共通化

#### 問題
`GetPathFinder()`, `GetTurnManager()`などのゲッター関数が複数ファイルで重複実装されています。

**重複箇所:**
- `Abilities/GA_MoveBase.cpp:686-704` - GetPathFinder()
- `Turn/GameTurnManagerBase.cpp:560-594` - GetPathFinder()
- `Turn/DistanceFieldSubsystem.cpp:407-420` - GetPathFinder()
- `Abilities/GA_MoveBase.cpp:728-745` - GetTurnManager()
- `Abilities/GA_MeleeAttack.cpp:212-228` - GetTurnManager()

#### 解決策
テンプレート化されたユーティリティ関数を作成します。

**新規ファイル: `Utility/ActorFinderUtils.h`**
```cpp
#pragma once
#include "CoreMinimal.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"

namespace RogueUtils
{
    /**
     * ワールドから指定クラスのActorを取得（キャッシュ付き）
     * GameplayStatics::GetActorOfClassを使用
     */
    template<typename TActorClass>
    static TActorClass* GetCachedActor(const UWorld* World, TWeakObjectPtr<TActorClass>& CachedPtr)
    {
        if (CachedPtr.IsValid())
        {
            return CachedPtr.Get();
        }

        if (!World)
        {
            return nullptr;
        }

        if (TActorClass* Found = Cast<TActorClass>(
            UGameplayStatics::GetActorOfClass(World, TActorClass::StaticClass())))
        {
            CachedPtr = Found;
            return Found;
        }

        return nullptr;
    }

    /**
     * ワールドから指定クラスのActorを取得（イテレーター版、キャッシュ付き）
     */
    template<typename TActorClass>
    static TActorClass* GetCachedActorByIterator(const UWorld* World, TWeakObjectPtr<TActorClass>& CachedPtr)
    {
        if (CachedPtr.IsValid())
        {
            return CachedPtr.Get();
        }

        if (!World)
        {
            return nullptr;
        }

        for (TActorIterator<TActorClass> It(World); It; ++It)
        {
            CachedPtr = *It;
            return *It;
        }

        return nullptr;
    }
}
```

**使用例:**
```cpp
// Before
const AGridPathfindingLibrary* UGA_MoveBase::GetPathFinder() const
{
    if (CachedPathFinder.IsValid())
    {
        return CachedPathFinder.Get();
    }
    if (const UWorld* World = GetWorld())
    {
        if (AGridPathfindingLibrary* Found = Cast<AGridPathfindingLibrary>(
            UGameplayStatics::GetActorOfClass(World, AGridPathfindingLibrary::StaticClass())))
        {
            CachedPathFinder = Found;
            return CachedPathFinder.Get();
        }
    }
    return nullptr;
}

// After
const AGridPathfindingLibrary* UGA_MoveBase::GetPathFinder() const
{
    return RogueUtils::GetCachedActor(GetWorld(), CachedPathFinder);
}
```

**影響範囲:**
- 3ファイル削減: 各18-35行 → 1行
- コード削減: 約80行

**リスク:** 低
**価値:** 高

---

### 1.2 グリッド座標ユーティリティの共通化

#### 問題
基本方向（上下左右）のオフセット計算が複数箇所で重複しています。

**重複箇所:**
- `Turn/DistanceFieldSubsystem.cpp:93-94,108-109,322-323,368-369`
- `AI/Enemy/EnemyThinkerBase.cpp:71-74`
- `AI/Enemy/EnemyAISubsystem.cpp:105`

#### 解決策
グリッド座標ユーティリティクラスを作成します。

**新規ファイル: `Utility/GridCoordinateUtils.h`**
```cpp
#pragma once
#include "CoreMinimal.h"

namespace RogueGrid
{
    /** 基本4方向（東西南北） */
    static const TArray<FIntPoint> CardinalDirections = {
        FIntPoint(1, 0),   // 右（東）
        FIntPoint(-1, 0),  // 左（西）
        FIntPoint(0, 1),   // 上（北）
        FIntPoint(0, -1)   // 下（南）
    };

    /** 対角線を含む8方向 */
    static const TArray<FIntPoint> AllDirections = {
        FIntPoint(1, 0),   // 右
        FIntPoint(-1, 0),  // 左
        FIntPoint(0, 1),   // 上
        FIntPoint(0, -1),  // 下
        FIntPoint(1, 1),   // 右上
        FIntPoint(1, -1),  // 右下
        FIntPoint(-1, 1),  // 左上
        FIntPoint(-1, -1)  // 左下
    };

    /**
     * グリッド方向ベクトルを-1~1の範囲にクランプ
     */
    static FIntPoint ClampGridDirection(const FIntPoint& Direction)
    {
        return FIntPoint(
            FMath::Clamp(Direction.X, -1, 1),
            FMath::Clamp(Direction.Y, -1, 1)
        );
    }

    /**
     * 指定セルの隣接セル（4方向）を取得
     */
    static TArray<FIntPoint> GetAdjacentCells(const FIntPoint& Cell)
    {
        TArray<FIntPoint> Result;
        Result.Reserve(4);
        for (const FIntPoint& Dir : CardinalDirections)
        {
            Result.Add(Cell + Dir);
        }
        return Result;
    }

    /**
     * 指定セルの隣接セル（8方向）を取得
     */
    static TArray<FIntPoint> GetAllNeighborCells(const FIntPoint& Cell)
    {
        TArray<FIntPoint> Result;
        Result.Reserve(8);
        for (const FIntPoint& Dir : AllDirections)
        {
            Result.Add(Cell + Dir);
        }
        return Result;
    }
}
```

**使用例:**
```cpp
// Before
TArray<FIntPoint> Neighbors = {
    Intent.CurrentCell + FIntPoint(1, 0),   // Right
    Intent.CurrentCell + FIntPoint(-1, 0),  // Left
    Intent.CurrentCell + FIntPoint(0, 1),   // Up
    Intent.CurrentCell + FIntPoint(0, -1)   // Down
};

// After
TArray<FIntPoint> Neighbors = RogueGrid::GetAdjacentCells(Intent.CurrentCell);
```

**影響範囲:**
- 6箇所のコード簡略化
- コード削減: 約30行

**リスク:** 低
**価値:** 中

---

### 1.3 GameplayTag動的生成の静的化

#### 問題
`FGameplayTag::RequestGameplayTag(TEXT("..."))`が実行時に複数箇所で呼び出されています。これはパフォーマンスの問題です。

**重複箇所:**
- `Player/PlayerControllerBase.cpp:29-30`
- `AI/Enemy/EnemyAISubsystem.cpp:174,207,226`
- `AI/Enemy/EnemyThinkerBase.cpp:201,209,214,243`
- `AI/Enemy/EnemyTurnDataSubsystem.cpp:233`

#### 解決策
既存の`RogueGameplayTags`に不足しているタグを追加し、静的タグを使用するよう修正します。

**修正対象ファイル: `Utility/RogueGameplayTags.h`**
```cpp
// 追加すべきタグ（現在動的に取得されているもの）
namespace RogueGameplayTags
{
    // 既存のタグに追加
    ROGUELIKEDUNGEON_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_TurnAction_Move);
    ROGUELIKEDUNGEON_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_TurnAction_Attack);
    ROGUELIKEDUNGEON_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_TurnAction_Wait);
}
```

**使用例:**
```cpp
// Before
FGameplayTag MoveTag = FGameplayTag::RequestGameplayTag(TEXT("Ability.TurnAction.Move"));

// After
FGameplayTag MoveTag = RogueGameplayTags::Ability_TurnAction_Move;
```

**影響範囲:**
- 12箇所の動的タグ取得を静的化
- パフォーマンス向上

**リスク:** 低
**価値:** 中（パフォーマンス＋可読性）

---

## 🧹 Phase 2: 不要コード削除（中優先度）

### 2.1 空実装ファイルの削除

以下のファイルは空またはコメントのみのため削除可能です：

| ファイル | 状態 | 削除可否 |
|---------|------|---------|
| `Debug/TurnSystemInterfaces.cpp` | "空実装（インターフェースのみのため）"のみ | ✅ 削除可 |
| `Data/AIProfileDataAsset.cpp` | includeのみ | ✅ 削除可 |
| `Data/MoveInputPayloadBase.cpp` | "実装なし（シンプルなデータクラス）"のみ | ✅ 削除可 |
| `Turn/TurnSystemTypes.cpp` | コメントのみ | ✅ 削除可 |

**削除方法:**
```bash
git rm Debug/TurnSystemInterfaces.cpp
git rm Data/AIProfileDataAsset.cpp
git rm Data/MoveInputPayloadBase.cpp
git rm Turn/TurnSystemTypes.cpp
```

**リスク:** 低（.uproject/.Build.csで参照されていないことを確認済み）
**価値:** 中（コードベースの整理）

---

### 2.2 スタブ関数の処理

#### 2.2.1 削除推奨（常にfalseを返すだけの関数）

**`Turn/DashStopConditions.cpp:61-95`**
```cpp
bool UDashStopEvaluator::HasAdjacentEnemy(const FIntPoint& Cell, UWorld* World)
{
    // TODO: Phase 3後半で実装
    return false;
}

bool UDashStopEvaluator::IsDangerTile(const FIntPoint& Cell, UWorld* World)
{
    // TODO: Phase 3後半で実装
    return false;
}

bool UDashStopEvaluator::IsObstacle(const FIntPoint& Cell, UWorld* World)
{
    // TODO: Phase 3後半で実装
    return false;
}
```

**オプション1: 削除**
```cpp
// 関数を削除し、呼び出し側も修正
```

**オプション2: BlueprintNativeEventに変更**
```cpp
// .hファイル
UFUNCTION(BlueprintNativeEvent, Category = "Dash")
bool HasAdjacentEnemy(const FIntPoint& Cell, UWorld* World);

// .cppファイル
bool UDashStopEvaluator::HasAdjacentEnemy_Implementation(const FIntPoint& Cell, UWorld* World)
{
    return false; // デフォルト実装、Blueprintでオーバーライド可
}
```

**推奨:** オプション2（将来的な拡張性を保持）

#### 2.2.2 削除推奨（nullptrを返すだけの関数）

**`Turn/TBSLyraGameMode.h:50-51`**
```cpp
FORCEINLINE AGridPathfindingLibrary* GetPathFinder() const { return nullptr; }
FORCEINLINE AUnitManager* GetUnitManager() const { return nullptr; }
```

**削除理由:** コメントに"GameTurnManagerが所有"とあり、使用されていない

**リスク:** 低（参照箇所なし）
**価値:** 中

---

### 2.3 重複includeの削除

**`Turn/TurnCorePhaseManager.cpp`**
```cpp
// Line 18
#include "AbilitySystemGlobals.h"

// Line 583（削除対象）
#include "AbilitySystemGlobals.h"
```

**リスク:** 低
**価値:** 低（整理）

---

### 2.4 コメントアウトコードの削除

大量のコメントアウトコードが存在します。Gitでバージョン管理しているため、これらは削除可能です。

#### 主要な削除対象

**`Turn/GameTurnManagerBase.cpp:2434-2494` (60行)**
```cpp
// #if 0
// /*
//  * ─────────────────────────────────────────
//  * 旧AP(Action Point)システム実装（Phase 2で撤廃）
//  * ─────────────────────────────────────────
//  */
// [60行以上のコメントアウトコード]
// #endif
```

**`Player/PlayerControllerBase.cpp`**
- Lines 99-102: InputWindow検出
- Lines 245-249: InputGuard設定
- Lines 461-467: InputGuardリリース

**削除コマンド例:**
各ファイルでコメントアウトブロックを削除

**削減行数:** 約150-200行
**リスク:** 低（Gitに履歴が残る）
**価値:** 高（可読性向上）

---

### 2.5 過剰な空行の削除

**`Data/DungeonPresetTemplates.cpp`**
```cpp
// Before
void UDungeonTemplate_NormalBSP::Generate_Implementation(ADungeonFloorGenerator* Generator, const FDungeonResolvedParams& Params, FRandomStream& Rng)

{

    if (Generator)

    {

        Generator->Make_NormalBSP(Rng, Params);

    }

}

// After
void UDungeonTemplate_NormalBSP::Generate_Implementation(ADungeonFloorGenerator* Generator, const FDungeonResolvedParams& Params, FRandomStream& Rng)
{
    if (Generator)
    {
        Generator->Make_NormalBSP(Rng, Params);
    }
}
```

**リスク:** 低
**価値:** 中（可読性）

---

### 2.6 重複コンストラクタの統合

**`Abilities/GA_TurnActionBase.cpp:7-20`**
```cpp
// 削除対象
UGA_TurnActionBase::UGA_TurnActionBase()
{
    TimeoutTag = RogueGameplayTags::Effect_Turn_AbilityTimeout;
    StartEventTag = RogueGameplayTags::Gameplay_Event_Turn_Ability_Started;
    CompletionEventTag = RogueGameplayTags::Gameplay_Event_Turn_Ability_Completed;
}

// 保持
UGA_TurnActionBase::UGA_TurnActionBase(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    TimeoutTag = RogueGameplayTags::Effect_Turn_AbilityTimeout;
    StartEventTag = RogueGameplayTags::Gameplay_Event_Turn_Ability_Started;
    CompletionEventTag = RogueGameplayTags::Gameplay_Event_Turn_Ability_Completed;
}
```

**リスク:** 低
**価値:** 低

---

## 📊 Phase 3: 低優先度改善（任意）

### 3.1 ロギングマクロの作成

**現状:** 200+箇所で以下のパターンが繰り返されています
```cpp
UE_LOG(LogTurnManager, Error, TEXT("[GA_MoveBase] %s: Message"), *GetNameSafe(Actor));
```

**改善案:**
```cpp
// Utility/LoggingMacros.h
#define ROGUE_LOG(Category, Verbosity, Actor, Format, ...) \
    UE_LOG(Category, Verbosity, TEXT("[%s] %s: " Format), \
           *FString(__FUNCTION__), *GetNameSafe(Actor), ##__VA_ARGS__)

// 使用例
ROGUE_LOG(LogTurnManager, Error, Actor, "Failed to execute move");
```

**リスク:** 中（マクロ導入のため）
**価値:** 中（一貫性）

---

### 3.2 ABilitySystemInterfaceヘルパー

**現状:** Cast<IAbilitySystemInterface>が15+箇所で繰り返されています

**改善案:**
```cpp
// Utility/AbilitySystemUtils.h
namespace RogueAbility
{
    static UAbilitySystemComponent* GetASCFromActor(AActor* Actor)
    {
        if (!Actor) return nullptr;

        if (IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Actor))
        {
            return ASI->GetAbilitySystemComponent();
        }
        return nullptr;
    }
}
```

**リスク:** 低
**価値:** 中

---

## 📋 実装計画

### フェーズ1: 準備（0.5日）
1. ✅ 重複・不要コード分析完了
2. ⬜ 新規ユーティリティファイル作成
   - `Utility/ActorFinderUtils.h`
   - `Utility/GridCoordinateUtils.h`
3. ⬜ 既存`RogueGameplayTags.h`へのタグ追加

### フェーズ2: 高優先度リファクタリング（1-2日）
1. ⬜ Actorゲッター共通化（5ファイル修正）
2. ⬜ グリッド座標ユーティリティ導入（6ファイル修正）
3. ⬜ GameplayTag静的化（5ファイル修正）
4. ⬜ ビルド＆テスト

### フェーズ3: 不要コード削除（0.5-1日）
1. ⬜ 空実装ファイル削除（4ファイル）
2. ⬜ コメントアウトコード削除（3ファイル、150行削減）
3. ⬜ 重複include削除（1ファイル）
4. ⬜ スタブ関数処理（2ファイル）
5. ⬜ ビルド＆テスト

### フェーズ4: 低優先度改善（任意、1日）
1. ⬜ ロギングマクロ導入
2. ⬜ ASCヘルパー導入
3. ⬜ ビルド＆テスト

### フェーズ5: 仕上げ（0.5日）
1. ⬜ コミット＆プッシュ
2. ⬜ ドキュメント更新
3. ⬜ レビュー

**合計所要時間:** 2.5-5日（低優先度含む）

---

## ⚠️ リスク評価

### 低リスク項目
- ✅ Actorゲッター共通化
- ✅ グリッド座標ユーティリティ
- ✅ GameplayTag静的化
- ✅ 空ファイル削除
- ✅ コメントアウトコード削除

### 中リスク項目
- ⚠️ スタブ関数の処理（使用箇所確認必要）
- ⚠️ ロギングマクロ導入（全箇所テスト必要）

### 高リスク項目
- 🚨 なし

---

## 📈 期待される効果

### コード品質
- **削減行数:** 300-400行（重複80行 + 不要200行 + その他）
- **ファイル削減:** 4ファイル
- **可読性:** 向上（コメントアウト削除、整理）
- **保守性:** 向上（共通化により変更箇所減少）

### パフォーマンス
- **GameplayTag取得:** 約12箇所で実行時検索→静的参照に改善
- **Actor検索:** キャッシュ実装の標準化

### 開発効率
- **新機能開発:** ユーティリティ関数により実装速度向上
- **バグ修正:** 重複コード削減により修正漏れ防止
- **オンボーディング:** クリーンなコードベースにより学習コスト削減

---

## 📝 TODO一覧

### 実装TODO（Phase 1-3）
- [ ] `Utility/ActorFinderUtils.h` 作成
- [ ] `Utility/GridCoordinateUtils.h` 作成
- [ ] `RogueGameplayTags.h/.cpp` にタグ追加
- [ ] GA_MoveBase.cpp - GetPathFinder()修正
- [ ] GameTurnManagerBase.cpp - GetPathFinder()修正
- [ ] DistanceFieldSubsystem.cpp - GetPathFinder()修正
- [ ] GA_MoveBase.cpp - GetTurnManager()修正
- [ ] GA_MeleeAttack.cpp - GetTurnManager()修正
- [ ] DistanceFieldSubsystem.cpp - CardinalDirections使用
- [ ] EnemyThinkerBase.cpp - GetAdjacentCells()使用
- [ ] EnemyAISubsystem.cpp - GetAdjacentCells()使用
- [ ] PlayerControllerBase.cpp - 静的タグ使用
- [ ] EnemyAISubsystem.cpp - 静的タグ使用
- [ ] EnemyThinkerBase.cpp - 静的タグ使用
- [ ] EnemyTurnDataSubsystem.cpp - 静的タグ使用
- [ ] 空ファイル4つ削除
- [ ] コメントアウトコード削除（GameTurnManagerBase.cpp）
- [ ] コメントアウトコード削除（PlayerControllerBase.cpp）
- [ ] コメントアウトコード削除（その他）
- [ ] TurnCorePhaseManager.cpp - 重複include削除
- [ ] DashStopConditions.cpp - スタブ関数処理
- [ ] TBSLyraGameMode.h - nullptrゲッター削除
- [ ] DungeonPresetTemplates.cpp - 空行削除
- [ ] GA_TurnActionBase.cpp - 重複コンストラクタ削除

### テストTODO
- [ ] ビルド確認（Phase 1完了後）
- [ ] ビルド確認（Phase 2完了後）
- [ ] ビルド確認（Phase 3完了後）
- [ ] 動作確認（移動システム）
- [ ] 動作確認（戦闘システム）
- [ ] 動作確認（AIシステム）
- [ ] 動作確認（ダンジョン生成）

---

## 🎯 まとめ

本提案書は、**安全かつ段階的なリファクタリング**を実現するための詳細な計画です。

### 推奨アプローチ
1. **Phase 1（高優先度）から開始** - 低リスク・高価値
2. **各フェーズ後に必ずビルド＆テスト** - 問題の早期発見
3. **Phase 3（不要コード削除）は随時実施可** - 独立した変更
4. **Phase 4（低優先度）は任意** - プロジェクト状況次第

### 次のステップ
本提案をレビューいただき、承認後にPhase 1から実装を開始します。

---

**作成日:** 2025-11-09
**分析対象:** roguelike-rogue-subsystem
**総コード行数:** 21,251行
