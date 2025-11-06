// =============================================================================
// AllyTurnDataSubsystem.h
// 味方ターン用データ管理（機能層）
// 
// 【責任】
// - 味方の登録・解除管理
// - コマンド管理
// - 意図生成のループ処理
// 
// 【責任外】
// - 具体的な行動決定 → AllyThinkerBase（Blueprint実装）
// - 数値調整 → Blueprint変数
// =============================================================================

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "AllyTurnDataSubsystem.generated.h"

class AActor;
struct FTurnContext;
class UAllyThinkerBase;

// ログカテゴリ
DECLARE_LOG_CATEGORY_EXTERN(LogAllyTurnData, Log, All);

/** 味方への高位コマンド */
UENUM(BlueprintType)
enum class EAllyCommand : uint8
{
    Follow      UMETA(DisplayName = "Follow"),
    StayHere    UMETA(DisplayName = "StayHere"),
    FreeRoam    UMETA(DisplayName = "FreeRoam"),
    NoAbility   UMETA(DisplayName = "NoAbility"),
    Auto        UMETA(DisplayName = "Auto"),
};

/** 味方の意図データ */
USTRUCT(BlueprintType)
struct FAllyIntent
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent")
    FGameplayTag ActionType;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent")
    FVector TargetLocation = FVector::ZeroVector;

    // 構造体では弱参照を使用（コピー安全性）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent")
    TWeakObjectPtr<AActor> TargetActor = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent")
    bool bIsPlayerDirected = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent")
    float Score = 0.0f;
};

/**
 * 味方ターンデータ管理サブシステム
 *
 * 【設計原則】
 * - C++: ループ処理、データ管理、nullチェック
 * - Blueprint: 個別の行動判断（AllyThinkerBase経由）
 */
UCLASS()
class LYRAGAME_API UAllyTurnDataSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    //--------------------------------------------------------------------------
    // 登録管理（C++実装）
    //--------------------------------------------------------------------------

    /** 味方を登録 */
    UFUNCTION(BlueprintCallable, Category = "Ally")
    void RegisterAlly(AActor* AllyActor, EAllyCommand InitialCommand = EAllyCommand::Follow);

    /** 味方を解除 */
    UFUNCTION(BlueprintCallable, Category = "Ally")
    void UnregisterAlly(AActor* AllyActor);

    /** 味方の数を取得 */
    UFUNCTION(BlueprintPure, Category = "Ally")
    int32 GetAllyCount() const { return Allies.Num(); }

    /** 味方がいるか確認 */
    UFUNCTION(BlueprintPure, Category = "Ally")
    bool HasAllies() const { return Allies.Num() > 0; }

    //--------------------------------------------------------------------------
    // コマンド操作（C++実装）
    //--------------------------------------------------------------------------

    /** 味方のコマンドを設定 */
    UFUNCTION(BlueprintCallable, Category = "Ally|Command")
    void SetAllyCommand(AActor* AllyActor, EAllyCommand Command);

    /** 味方のコマンドを取得 */
    UFUNCTION(BlueprintPure, Category = "Ally|Command")
    EAllyCommand GetAllyCommand(AActor* AllyActor) const;

    /** 全味方のコマンドを一括設定 */
    UFUNCTION(BlueprintCallable, Category = "Ally|Command")
    void SetAllAllyCommands(EAllyCommand Command);

    //--------------------------------------------------------------------------
    // 意図構築（C++でループ、Blueprint実装で中身）
    //--------------------------------------------------------------------------

    /** 全味方の意図を構築 */
    UFUNCTION(BlueprintNativeEvent, Category = "Ally")
    void BuildAllAllyIntents(const FTurnContext& Context);
    virtual void BuildAllAllyIntents_Implementation(const FTurnContext& Context);

    /** プレイヤー指示の意図を設定 */
    UFUNCTION(BlueprintCallable, Category = "Ally")
    void SetPlayerDirectedIntent(AActor* AllyActor, const FAllyIntent& Intent);

    //--------------------------------------------------------------------------
    // 拡張用フック（Blueprint実装）
    //--------------------------------------------------------------------------

    /** フック: 味方登録時 */
    UFUNCTION(BlueprintNativeEvent, Category = "Ally|Hooks")
    void OnAllyRegistered(AActor* AllyActor);
    virtual void OnAllyRegistered_Implementation(AActor* AllyActor) {}

    /** フック: 味方解除時 */
    UFUNCTION(BlueprintNativeEvent, Category = "Ally|Hooks")
    void OnAllyUnregistered(AActor* AllyActor);
    virtual void OnAllyUnregistered_Implementation(AActor* AllyActor) {}

    /** フック: コマンド変更時 */
    UFUNCTION(BlueprintNativeEvent, Category = "Ally|Hooks")
    void OnAllyCommandChanged(AActor* AllyActor, EAllyCommand OldCommand, EAllyCommand NewCommand);
    virtual void OnAllyCommandChanged_Implementation(AActor* AllyActor, EAllyCommand OldCommand, EAllyCommand NewCommand) {}

    /** フック: 意図生成前 */
    UFUNCTION(BlueprintNativeEvent, Category = "Ally|Hooks")
    void PreBuildIntents(const FTurnContext& Context);
    virtual void PreBuildIntents_Implementation(const FTurnContext& Context) {}

    /** フック: 意図生成後 */
    UFUNCTION(BlueprintNativeEvent, Category = "Ally|Hooks")
    void PostBuildIntents(const FTurnContext& Context);
    virtual void PostBuildIntents_Implementation(const FTurnContext& Context) {}

    /** フック: 味方状態更新 */
    UFUNCTION(BlueprintNativeEvent, Category = "Ally|Hooks")
    void UpdateAllyStates(const FTurnContext& Context);
    virtual void UpdateAllyStates_Implementation(const FTurnContext& Context) {}

    //--------------------------------------------------------------------------
    // 読み取り用状態（Blueprint参照可）
    //--------------------------------------------------------------------------

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ally")
    TArray<TObjectPtr<AActor>> Allies;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ally")
    TArray<FAllyIntent> Intents;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ally")
    TMap<TObjectPtr<AActor>, EAllyCommand> AllyCommands;

private:
    /** 味方のインデックスを検索 */
    int32 FindAllyIndex(AActor* AllyActor) const;
};
