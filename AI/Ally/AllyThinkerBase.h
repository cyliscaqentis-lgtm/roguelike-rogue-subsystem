// =============================================================================
// AllyThinkerBase.h
// 味方AIの意思決定（実行層）
// 
// 【責任】
// - 部品API提供（GetPlayerActor等）
// - フックの宣言
// 
// 【責任外】
// - 具体的な行動ロジック → Blueprint実装
// - 数値調整 → Blueprint変数
// =============================================================================

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "AllyTurnDataSubsystem.h"
#include "AllyThinkerBase.generated.h"

class AActor;
struct FTurnContext;
struct FAllyIntent;
class UAbilitySystemComponent;

// ログカテゴリ
DECLARE_LOG_CATEGORY_EXTERN(LogAllyThinker, Log, All);

/**
 * 味方AI思考コンポーネント
 *
 * 【設計原則】
 * - C++: 部品API（GetPlayerActor等）、パラメータ定義
 * - Blueprint: 具体的な行動判断（ComputeXXXIntent）
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class LYRAGAME_API UAllyThinkerBase : public UActorComponent
{
    GENERATED_BODY()

public:
    UAllyThinkerBase();

    //--------------------------------------------------------------------------
    // エントリーポイント（Blueprint実装）
    //--------------------------------------------------------------------------

    /** 味方の意図を計算 */
    UFUNCTION(BlueprintNativeEvent, Category = "Ally|AI")
    void ComputeAllyIntent(const FTurnContext& Context, EAllyCommand Command, UPARAM(ref) FAllyIntent& OutIntent);
    virtual void ComputeAllyIntent_Implementation(const FTurnContext& Context, EAllyCommand Command, FAllyIntent& OutIntent);

    //--------------------------------------------------------------------------
    // 個別フック（Blueprint実装）
    //--------------------------------------------------------------------------

    /** フック: 追従行動 */
    UFUNCTION(BlueprintNativeEvent, Category = "Ally|AI|Hooks")
    void ComputeFollowIntent(const FTurnContext& Context, UPARAM(ref) FAllyIntent& OutIntent);
    virtual void ComputeFollowIntent_Implementation(const FTurnContext& Context, FAllyIntent& OutIntent);

    /** フック: 待機行動 */
    UFUNCTION(BlueprintNativeEvent, Category = "Ally|AI|Hooks")
    void ComputeStayIntent(const FTurnContext& Context, UPARAM(ref) FAllyIntent& OutIntent);
    virtual void ComputeStayIntent_Implementation(const FTurnContext& Context, FAllyIntent& OutIntent);

    /** フック: 自由巡回行動 */
    UFUNCTION(BlueprintNativeEvent, Category = "Ally|AI|Hooks")
    void ComputeFreeRoamIntent(const FTurnContext& Context, UPARAM(ref) FAllyIntent& OutIntent);
    virtual void ComputeFreeRoamIntent_Implementation(const FTurnContext& Context, FAllyIntent& OutIntent);

    /** フック: 通常攻撃のみ */
    UFUNCTION(BlueprintNativeEvent, Category = "Ally|AI|Hooks")
    void ComputeNoAbilityIntent(const FTurnContext& Context, UPARAM(ref) FAllyIntent& OutIntent);
    virtual void ComputeNoAbilityIntent_Implementation(const FTurnContext& Context, FAllyIntent& OutIntent);

    /** フック: 完全自動 */
    UFUNCTION(BlueprintNativeEvent, Category = "Ally|AI|Hooks")
    void ComputeAutoIntent(const FTurnContext& Context, UPARAM(ref) FAllyIntent& OutIntent);
    virtual void ComputeAutoIntent_Implementation(const FTurnContext& Context, FAllyIntent& OutIntent);

    //--------------------------------------------------------------------------
    // 部品API（C++実装 - 10年変わらない）
    //--------------------------------------------------------------------------

    /** プレイヤーActorを取得 */
    UFUNCTION(BlueprintPure, Category = "Ally|AI|Parts")
    AActor* GetPlayerActor() const;

    /** プレイヤーとの距離をタイル単位で取得 */
    UFUNCTION(BlueprintPure, Category = "Ally|AI|Parts")
    int32 GetDistanceToPlayerInTiles() const;

    /** 視界内の敵を取得 */
    UFUNCTION(BlueprintPure, Category = "Ally|AI|Parts")
    TArray<AActor*> GetVisibleEnemies() const;

    /** 攻撃範囲を取得 */
    UFUNCTION(BlueprintPure, Category = "Ally|AI|Ability")
    int32 GetMaxAttackRangeInTiles() const { return AttackRangeInTiles; }

    /** 距離に応じた攻撃Abilityを取得 */
    UFUNCTION(BlueprintCallable, Category = "Ally|AI|Ability")
    FGameplayTag GetAttackAbilityForRange(int32 DistanceInTiles) const;

    /** AbilitySystemComponentを取得 */
    UFUNCTION(BlueprintPure, Category = "Ally|AI|Ability")
    UAbilitySystemComponent* GetOwnerAbilitySystemComponent() const;

protected:
    //--------------------------------------------------------------------------
    // 行動パラメータ（Blueprint調整可能）
    //--------------------------------------------------------------------------

    /** 追従距離の最大値（タイル単位） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ally|AI|Behavior", meta = (ClampMin = "1", ClampMax = "20"))
    int32 MaxFollowDistanceInTiles = 5;

    /** 自由巡回の半径（タイル単位） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ally|AI|Behavior", meta = (ClampMin = "1", ClampMax = "50"))
    int32 FreeRoamRadiusInTiles = 10;

    /** 攻撃優先度の閾値 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ally|AI|Behavior", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float AttackPriorityThreshold = 0.6f;

    /** 攻撃範囲（タイル単位） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ally|AI|Behavior", meta = (ClampMin = "1", ClampMax = "10"))
    int32 AttackRangeInTiles = 1;

    /** デフォルトの速度優先度 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ally|AI|Behavior", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float DefaultSpeedPriority = 0.5f;

    /** タイルサイズ（Unreal Units） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ally|AI|Grid", meta = (ClampMin = "1.0", ClampMax = "1000.0"))
    float TileSizeInUnits = 100.0f;
};
