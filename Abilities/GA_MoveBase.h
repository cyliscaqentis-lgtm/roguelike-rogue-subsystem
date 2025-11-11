// GA_MoveBase.h
#pragma once

#include "CoreMinimal.h"
#include "GA_TurnActionBase.h"
#include "GameplayTagContainer.h"
#include "GA_MoveBase.generated.h"

// 蜑肴婿螳�E�險
class AGridPathfindingLibrary;
class AUnitBase;
class UTurnActionBarrierSubsystem;
class UGridOccupancySubsystem;
class AGameTurnManagerBase;

/**
 * UGA_MoveBase
 * 繝励Ξ繧�E�繝､繝ｼ繝ｦ繝九ャ繝医・遘ｻ蜍輔い繝薙Μ繝�EぁE
 *
 * 笘�E・笘�EPhase 3 蟁E��蠢懶�E�・繧�E�繧�E�繧�E�繧�E�繝�E΁E+ Barrier邨�E�蜷茨�E�・
 *
 * 讖溯・:
 * - EnhancedInput邨檎罰縺�E�繧�E�繝｡繝ｩ逶�E�蟁E��譁E��蜷代�E�蜿励�E�蜿悶�E�縲√げ繝ｪ繝�Eラ遘ｻ蜍輔ｒ螳溯�E�・
 * - State.Moving 繧�E�繧�E�縺�E�繧医�E�蜀崎ｵ�E�蜍暮亟豁E��・・ctivationBlockedTags・・
 * - 笘�E・笘�EState.Action.InProgress 繧�E�繧�E�縺�E�邂｡送E�E�E�・A縺瑚ｲ�E�莉ｻ繧呈戟縺�E�・・
 * - 笘�E・笘�EBarrier::RegisterAction() / CompleteAction() 縺�E�繧医�E�騾�E�陦悟宛蠕｡
 * - 遘ｻ蜍募�E�御�E�・�E�後�E Ability.Move.Completed 繧�E�繝吶Φ繝医�E�騾∽�E��E�・医ち繝ｼ繝ｳ蜷梧悁E�E・
 * - 3轤�E�隧�E�繧∝ｯ�E�蠢懶�E�壼�E��E�讓吶せ繝翫ャ繝励、E���E�陬懈ｭ�E�縲∬�E��E�譁E��繝ｭ繧�E�
 * - TurnId繧�E�繝｣繝励メ繝｣縺�E�繧医�E�荳堺�E�閾�E�蟁E��遲・ *
 * 縲占�E��E�莉ｻ遽・峁E��・ * - State.Action.InProgress 繧�E�繧�E�縺�E�莉倁E��・蜑･螂ｪ・・wnedTags・・ * - Barrier 縺�E�縺�E� RegisterAction / CompleteAction 騾夂衁E
 * - TurnManager 縺�E� InProgress 繧�E�繧�E�縺�E�荳蛻・�E��E�繧峨↑縺・ */
UCLASS(Abstract, Blueprintable)
class LYRAGAME_API UGA_MoveBase : public UGA_TurnActionBase
{
    GENERATED_BODY()

public:
    UGA_MoveBase(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

    //--------------------------------------------------------------------------
    // GameplayAbility 繧�E�繝ｼ繝�E・繝ｩ繧�E�繝�E    //--------------------------------------------------------------------------

    virtual void ActivateAbility(
        const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,
        const FGameplayEventData* TriggerEventData
    ) override;

    virtual bool CanActivateAbility(
        const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayTagContainer* SourceTags = nullptr,
        const FGameplayTagContainer* TargetTags = nullptr,
        FGameplayTagContainer* OptionalRelevantTags = nullptr
    ) const override;

    virtual void ActivateAbilityFromEvent(
        const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,
        const FGameplayEventData* EventData
    );

    virtual void EndAbility(
        const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,
        bool bReplicateEndAbility,
        bool bWasCancelled
    ) override;

    virtual void CancelAbility(
        const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,
        bool bReplicateCancelAbility
    ) override;

protected:
    virtual void SendCompletionEvent(bool bTimedOut = false) override;

    //==========================================================================
    // 笘�E・笘�EPhase 6: 繧�E�繧�E�繝繧�E�繧�E�繝亥�E��E�蠢・    //==========================================================================

    /**
     * 繧�E�繝薙Μ繝�EぁE��句�E�区凾蛻�E�・医ち繧�E�繝繧�E�繧�E�繝郁�E��E�譁E��逕ｨ・・     */
    double AbilityStartTime = 0.0;

    //--------------------------------------------------------------------------
    // 險�E�螳壹ヱ繝ｩ繝｡繝ｼ繧�E�
    //--------------------------------------------------------------------------

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Move")
    float Speed = 600.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Move")
    float SpeedBuff = 1.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Move")
    float SpeedDebuff = 1.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Move")
    float GridSize = 100.0f;

    /** 繧�E�繝九Γ繝ｼ繧�E�繝ｧ繝ｳ繧偵せ繧�E�繝�E・縺吶�E�霍晞屬髢�E�蛟､・・=蟶�E�縺�E�繧�E�繝九Γ繝ｼ繧�E�繝ｧ繝ｳ蜀咲函・・*/
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Move|Animation")
    float SkipAnimIfUnderDistance = 0.0f;  // 笘�E・笘�E繝�Eヵ繧�E�繝ｫ繝�E縺�E�螟画峩・壼�E��E�縺�E�繧�E�繝九Γ繝ｼ繧�E�繝ｧ繝ｳ蜀咲函 笘�E・笘�E
    UPROPERTY(EditDefaultsOnly, Category = "GAS|Tags")
    FGameplayTag StateMovingTag = FGameplayTag::RequestGameplayTag(TEXT("State.Moving"));

    //--------------------------------------------------------------------------
    // 蜀・Κ迥�E�諷具�E�・lueprintReadOnly - 繝�Eヰ繝�Eげ逕ｨ・・    //--------------------------------------------------------------------------

    UPROPERTY(BlueprintReadOnly, Category = "Move|State")
    FVector Direction = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "Move|State")
    FVector2D MoveDir2D = FVector2D::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "Move|State")
    FVector FirstLoc = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "Move|State")
    FVector NextTileStep = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "Move|State")
    float DesiredYaw = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Move|State")
    float CurrentSpeed = 0.0f;

    int32 CompletedTurnIdForEvent = INDEX_NONE;

    //--------------------------------------------------------------------------
    // C++蜀・Κ螳溯�E�・    //--------------------------------------------------------------------------

    /** EventData縺九ｉDirection謚ｽ蜁E�� */
    bool ExtractDirectionFromEventData(const FGameplayEventData* EventData, FVector& OutDirection);

    /** 8譁E��蜷鷹㍼蟁E��喧 */
    FVector2D QuantizeToGridDirection(const FVector& InDirection);

    /** 繧�E�繝ｪ繝�Eラ蠎ｧ讓呵�E�育�E�・*/
    FVector CalculateNextTilePosition(const FVector& CurrentPosition, const FVector2D& Dir);

    /**
     * 遘ｻ蜍募庁E���E�蛻�E�螳夲�E�・轤�E�隧�E�繧∝ｯ�E�蠢懁E��・・     */
    bool IsTileWalkable(const FVector& TilePosition, AUnitBase* Self = nullptr);

    /** 繧�E�繝ｪ繝�Eラ迥�E�諷区峩譁E�� */
    void UpdateGridState(const FVector& Position, int32 Value);

    /** 蜊譛峨・繧�E�繝悶す繧�E�繝�EΒ縺�E�莉ｻ縺帙ｋ�E域立UpdateGridState縺�E�莉｣譖ｿ・・*/
    void UpdateOccupancy(AActor* Unit, const FIntPoint& NewCell);

    /** 豁E��陦悟庁E���E�縺�E�Occupancy/DistanceField縺�E�蟋碑ｭ�E� */
    bool IsTileWalkable(const FIntPoint& Cell) const;

    /** 蝗櫁E���E�隗貞ｺ�E�繧・5蠎ｦ蜊倁E��阪↓荳�E�繧√ａE*/
    float RoundYawTo45Degrees(float Yaw);

    /** 繝�EΜ繧�E�繝ｼ繝医ヰ繧�E�繝ｳ繝�E*/
    void BindMoveFinishedDelegate();

    /** 遘ｻ蜍募�E�御�E�・さ繝ｼ繝ｫ繝�Eャ繧�E� */
    UFUNCTION()
    void OnMoveFinished(AUnitBase* Unit);

    /** 繧�E�繝ｫ蠎ｧ讓呎欠螳壹〒縺�E�遘ｻ蜍暮幕蟋具�E�域雰繝ｦ繝九ャ繝育畑�E・*/
    void StartMoveToCell(const FIntPoint& TargetCell);

    //--------------------------------------------------------------------------
    // 3轤�E�隧�E�繧・�E�壼�E��E�讓呎�E��E�隕丞喧繝ｻ險�E�譁E��繝ｦ繝ｼ繝�EぁE��ｪ繝�EぁE
    //--------------------------------------------------------------------------

    FVector SnapToCellCenter(const FVector& WorldPos) const;
    FVector SnapToCellCenterFixedZ(const FVector& WorldPos, float FixedZ) const;
    float ComputeFixedZ(const AUnitBase* Unit, const AGridPathfindingLibrary* PathFinder) const;
    FVector AlignZToGround(const FVector& WorldPos, float TraceUp = 200.0f, float TraceDown = 2000.0f) const;
    void DebugDumpAround(const FIntPoint& Center);

    //--------------------------------------------------------------------------
    // Blueprint諡�E�蠑ｵ繝昴ぁE��ｳ繝茨�E�医ぁE��九Γ繝ｼ繧�E�繝ｧ繝ｳ蛻�E�蠕｡・・    //--------------------------------------------------------------------------

    UFUNCTION(BlueprintImplementableEvent, Category = "Move|Animation")
    void ExecuteMoveAnimation(const TArray<FVector>& Path);

    UFUNCTION(BlueprintNativeEvent, Category = "Move|Animation")
    bool ShouldSkipAnimation();
    virtual bool ShouldSkipAnimation_Implementation();

private:
    //--------------------------------------------------------------------------
    // 笘�E・笘�EPhase 3: Barrier邨�E�蜷医→InProgress繧�E�繧�E�邂｡送E�E    //--------------------------------------------------------------------------

    /**
     * 繧�E�繝薙Μ繝�EぁE��句�E�区凾縺�E�TurnId
     * Barrier::RegisterAction() 縺�E� CompleteAction() 縺�E�菴�E�逕ｨ
     */
    int32 MoveTurnId = INDEX_NONE;

    /**
     * 笘�E・笘�E譁E��隕剰�E��E�蜉: 繧�E�繝薙Μ繝�EぁE���E�ActionID
     * Barrier::RegisterAction() 縺�E�蜿門�E�・     * Barrier::CompleteAction() 縺�E�菴�E�逕ｨ
     */
    FGuid MoveActionId;

    /**
     * ☁E�E☁EHotfix: Barrier二重登録防止フラグ
     * RegisterBarrier()が同一インスタンスで褁E��回呼ばれても登録は1回�Eみ
     */
    bool bBarrierRegistered = false;

    /**
     * ★★★ Token方式: Barrier完了トークン（冪等Complete用）
     */
    UPROPERTY(Transient)
    FGuid BarrierToken;

    /**
     * ★★★ Token方式: Barrierサブシステムのキャッシュ
     */
    mutable TWeakObjectPtr<UTurnActionBarrierSubsystem> CachedBarrier;

    /**
     * ★★★ Token方式: ワールドからBarrier取得（キャッシュ付き）
     */
    UTurnActionBarrierSubsystem* GetBarrierSubsystem() const;

    //--------------------------------------------------------------------------
    // State.Moving 繧�E�繧�E�・亥・襍ｷ蜍暮亟豁E��・・    //--------------------------------------------------------------------------

    UPROPERTY()
    FGameplayTag TagStateMoving;

    //--------------------------------------------------------------------------
    // PathFinder繝ｻ繝�EΜ繧�E�繝ｼ繝�E    //--------------------------------------------------------------------------

    UPROPERTY()
    mutable TWeakObjectPtr<class AGridPathfindingLibrary> CachedPathFinder;
    mutable TWeakObjectPtr<AGameTurnManagerBase> CachedTurnManager;

    /**
     * PathFinder縺�E�蜿門�E�励�E�荳邂�E園縺�E�髮・�E�・     */
    const class AGridPathfindingLibrary* GetPathFinder() const;
    AGameTurnManagerBase* GetTurnManager() const;

    FDelegateHandle MoveFinishedHandle;

    //--------------------------------------------------------------------------
    // 遘ｻ蜍暮幕蟋倶�E�咲�E��E�・医Ρ繝ｼ繝ｫ繝牙�E��E�讓呻�E�会ｼ・ridOccupancy隗｣謾�E�逕ｨ・・    //--------------------------------------------------------------------------

    FVector CachedStartLocWS = FVector::ZeroVector;

    /** 移動先セル（OnMoveFinishedで正しい位置設定に使用） */
    FIntPoint CachedNextCell = FIntPoint(-1, -1);

    //--------------------------------------------------------------------------
    // 繧�E�繝｣繝�Eす繝･螟画焚�E・nMoveFinished 縺�E�菴�E�逕ｨ・・    //--------------------------------------------------------------------------

    FGameplayAbilitySpecHandle CachedSpecHandle;
    FGameplayAbilityActorInfo CachedActorInfo;
    FGameplayAbilityActivationInfo CachedActivationInfo;
    FVector CachedFirstLoc = FVector::ZeroVector;

    // ☁E�E☁ESparky修正: 再�E防止フラグ ☁E�E☁E
    bool bIsEnding = false;

    bool RegisterBarrier(AActor* Avatar);

    // ★★★ REMOVED: InProgressStack (2025-11-11) ★★★
    // ActivationOwnedTags を使用するため、手動カウントは不要
};

