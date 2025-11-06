// GA_MoveBase.h
#pragma once

#include "CoreMinimal.h"
#include "GA_TurnActionBase.h"
#include "GameplayTagContainer.h"
#include "GA_MoveBase.generated.h"

// 蜑肴婿螳｣險
class AGridPathfindingLibrary;
class AUnitBase;
class UTurnActionBarrierSubsystem;
class UGridOccupancySubsystem;

/**
 * UGA_MoveBase
 * 繝励Ξ繧､繝､繝ｼ繝ｦ繝九ャ繝医・遘ｻ蜍輔い繝薙Μ繝・ぅ
 *
 * 笘・・笘・Phase 3 蟇ｾ蠢懶ｼ・繧ｿ繧ｰ繧ｷ繧ｹ繝・Β + Barrier邨ｱ蜷茨ｼ・
 *
 * 讖溯・:
 * - EnhancedInput邨檎罰縺ｧ繧ｫ繝｡繝ｩ逶ｸ蟇ｾ譁ｹ蜷代ｒ蜿励￠蜿悶ｊ縲√げ繝ｪ繝・ラ遘ｻ蜍輔ｒ螳溯｡・
 * - State.Moving 繧ｿ繧ｰ縺ｫ繧医ｋ蜀崎ｵｷ蜍暮亟豁｢・・ctivationBlockedTags・・
 * - 笘・・笘・State.Action.InProgress 繧ｿ繧ｰ縺ｮ邂｡逅・ｼ・A縺瑚ｲｬ莉ｻ繧呈戟縺､・・
 * - 笘・・笘・Barrier::RegisterAction() / CompleteAction() 縺ｫ繧医ｋ騾ｲ陦悟宛蠕｡
 * - 遘ｻ蜍募ｮ御ｺ・ｾ後↓ Ability.Move.Completed 繧､繝吶Φ繝医ｒ騾∽ｿ｡・医ち繝ｼ繝ｳ蜷梧悄・・
 * - 3轤ｹ隧ｰ繧∝ｯｾ蠢懶ｼ壼ｺｧ讓吶せ繝翫ャ繝励〇霆ｸ陬懈ｭ｣縲∬ｨｺ譁ｭ繝ｭ繧ｰ
 * - TurnId繧ｭ繝｣繝励メ繝｣縺ｫ繧医ｋ荳堺ｸ閾ｴ蟇ｾ遲・ *
 * 縲占ｲｬ莉ｻ遽・峇縲・ * - State.Action.InProgress 繧ｿ繧ｰ縺ｮ莉倅ｸ・蜑･螂ｪ・・wnedTags・・ * - Barrier 縺ｸ縺ｮ RegisterAction / CompleteAction 騾夂衍
 * - TurnManager 縺ｯ InProgress 繧ｿ繧ｰ縺ｫ荳蛻・ｧｦ繧峨↑縺・ */
UCLASS(Abstract, Blueprintable)
class LYRAGAME_API UGA_MoveBase : public UGA_TurnActionBase
{
    GENERATED_BODY()

public:
    UGA_MoveBase(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

    //--------------------------------------------------------------------------
    // GameplayAbility 繧ｪ繝ｼ繝舌・繝ｩ繧､繝・    //--------------------------------------------------------------------------

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
    // 笘・・笘・Phase 6: 繧ｿ繧､繝繧｢繧ｦ繝亥ｯｾ蠢・    //==========================================================================

    /**
     * 繧｢繝薙Μ繝・ぅ髢句ｧ区凾蛻ｻ・医ち繧､繝繧｢繧ｦ繝郁ｨｺ譁ｭ逕ｨ・・     */
    double AbilityStartTime = 0.0;

    //--------------------------------------------------------------------------
    // 險ｭ螳壹ヱ繝ｩ繝｡繝ｼ繧ｿ
    //--------------------------------------------------------------------------

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Move")
    float Speed = 600.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Move")
    float SpeedBuff = 1.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Move")
    float SpeedDebuff = 1.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Move")
    float GridSize = 100.0f;

    /** 繧｢繝九Γ繝ｼ繧ｷ繝ｧ繝ｳ繧偵せ繧ｭ繝・・縺吶ｋ霍晞屬髢ｾ蛟､・・=蟶ｸ縺ｫ繧｢繝九Γ繝ｼ繧ｷ繝ｧ繝ｳ蜀咲函・・*/
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Move|Animation")
    float SkipAnimIfUnderDistance = 0.0f;  // 笘・・笘・繝・ヵ繧ｩ繝ｫ繝・縺ｫ螟画峩・壼ｸｸ縺ｫ繧｢繝九Γ繝ｼ繧ｷ繝ｧ繝ｳ蜀咲函 笘・・笘・
    UPROPERTY(EditDefaultsOnly, Category = "GAS|Tags")
    FGameplayTag StateMovingTag = FGameplayTag::RequestGameplayTag(TEXT("State.Moving"));

    //--------------------------------------------------------------------------
    // 蜀・Κ迥ｶ諷具ｼ・lueprintReadOnly - 繝・ヰ繝・げ逕ｨ・・    //--------------------------------------------------------------------------

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

    //--------------------------------------------------------------------------
    // C++蜀・Κ螳溯｣・    //--------------------------------------------------------------------------

    /** EventData縺九ｉDirection謚ｽ蜃ｺ */
    bool ExtractDirectionFromEventData(const FGameplayEventData* EventData, FVector& OutDirection);

    /** 8譁ｹ蜷鷹㍼蟄仙喧 */
    FVector2D QuantizeToGridDirection(const FVector& InDirection);

    /** 繧ｰ繝ｪ繝・ラ蠎ｧ讓呵ｨ育ｮ・*/
    FVector CalculateNextTilePosition(const FVector& CurrentPosition, const FVector2D& Dir);

    /**
     * 遘ｻ蜍募庄閭ｽ蛻､螳夲ｼ・轤ｹ隧ｰ繧∝ｯｾ蠢懃沿・・     */
    bool IsTileWalkable(const FVector& TilePosition, AUnitBase* Self = nullptr);

    /** 繧ｰ繝ｪ繝・ラ迥ｶ諷区峩譁ｰ */
    void UpdateGridState(const FVector& Position, int32 Value);

    /** 蜊譛峨・繧ｵ繝悶す繧ｹ繝・Β縺ｫ莉ｻ縺帙ｋ・域立UpdateGridState縺ｮ莉｣譖ｿ・・*/
    void UpdateOccupancy(AActor* Unit, const FIntPoint& NewCell);

    /** 豁ｩ陦悟庄蜷ｦ縺ｯOccupancy/DistanceField縺ｫ蟋碑ｭｲ */
    bool IsTileWalkable(const FIntPoint& Cell) const;

    /** 蝗櫁ｻ｢隗貞ｺｦ繧・5蠎ｦ蜊倅ｽ阪↓荳ｸ繧√ｋ */
    float RoundYawTo45Degrees(float Yaw);

    /** 繝・Μ繧ｲ繝ｼ繝医ヰ繧､繝ｳ繝・*/
    void BindMoveFinishedDelegate();

    /** 遘ｻ蜍募ｮ御ｺ・さ繝ｼ繝ｫ繝舌ャ繧ｯ */
    UFUNCTION()
    void OnMoveFinished(AUnitBase* Unit);

    /** 繧ｻ繝ｫ蠎ｧ讓呎欠螳壹〒縺ｮ遘ｻ蜍暮幕蟋具ｼ域雰繝ｦ繝九ャ繝育畑・・*/
    void StartMoveToCell(const FIntPoint& TargetCell);

    //--------------------------------------------------------------------------
    // 3轤ｹ隧ｰ繧・ｼ壼ｺｧ讓呎ｭ｣隕丞喧繝ｻ險ｺ譁ｭ繝ｦ繝ｼ繝・ぅ繝ｪ繝・ぅ
    //--------------------------------------------------------------------------

    FVector SnapToCellCenter(const FVector& WorldPos) const;
    FVector AlignZToGround(const FVector& WorldPos, float TraceUp = 200.0f, float TraceDown = 2000.0f) const;
    void DebugDumpAround(const FIntPoint& Center);

    //--------------------------------------------------------------------------
    // Blueprint諡｡蠑ｵ繝昴う繝ｳ繝茨ｼ医い繝九Γ繝ｼ繧ｷ繝ｧ繝ｳ蛻ｶ蠕｡・・    //--------------------------------------------------------------------------

    UFUNCTION(BlueprintImplementableEvent, Category = "Move|Animation")
    void ExecuteMoveAnimation(const TArray<FVector>& Path);

    UFUNCTION(BlueprintNativeEvent, Category = "Move|Animation")
    bool ShouldSkipAnimation();
    virtual bool ShouldSkipAnimation_Implementation();

private:
    //--------------------------------------------------------------------------
    // 笘・・笘・Phase 3: Barrier邨ｱ蜷医→InProgress繧ｿ繧ｰ邂｡逅・    //--------------------------------------------------------------------------

    /**
     * 繧｢繝薙Μ繝・ぅ髢句ｧ区凾縺ｮTurnId
     * Barrier::RegisterAction() 縺ｨ CompleteAction() 縺ｧ菴ｿ逕ｨ
     */
    int32 MoveTurnId = INDEX_NONE;

    /**
     * 笘・・笘・譁ｰ隕剰ｿｽ蜉: 繧｢繝薙Μ繝・ぅ縺ｮActionID
     * Barrier::RegisterAction() 縺ｧ蜿門ｾ・     * Barrier::CompleteAction() 縺ｧ菴ｿ逕ｨ
     */
    FGuid MoveActionId;

    //--------------------------------------------------------------------------
    // State.Moving 繧ｿ繧ｰ・亥・襍ｷ蜍暮亟豁｢・・    //--------------------------------------------------------------------------

    UPROPERTY()
    FGameplayTag TagStateMoving;

    //--------------------------------------------------------------------------
    // PathFinder繝ｻ繝・Μ繧ｲ繝ｼ繝・    //--------------------------------------------------------------------------

    UPROPERTY()
    mutable TWeakObjectPtr<class AGridPathfindingLibrary> CachedPathFinder;

    /**
     * PathFinder縺ｮ蜿門ｾ励ｒ荳邂・園縺ｫ髮・ｴ・     */
    const class AGridPathfindingLibrary* GetPathFinder() const;

    FDelegateHandle MoveFinishedHandle;

    //--------------------------------------------------------------------------
    // 遘ｻ蜍暮幕蟋倶ｽ咲ｽｮ・医Ρ繝ｼ繝ｫ繝牙ｺｧ讓呻ｼ会ｼ・ridOccupancy隗｣謾ｾ逕ｨ・・    //--------------------------------------------------------------------------

    FVector CachedStartLocWS = FVector::ZeroVector;

    //--------------------------------------------------------------------------
    // 繧ｭ繝｣繝・す繝･螟画焚・・nMoveFinished 縺ｧ菴ｿ逕ｨ・・    //--------------------------------------------------------------------------

    FGameplayAbilitySpecHandle CachedSpecHandle;
    FGameplayAbilityActorInfo CachedActorInfo;
    FGameplayAbilityActivationInfo CachedActivationInfo;
    FVector CachedFirstLoc = FVector::ZeroVector;

    bool RegisterBarrier(AActor* Avatar);
};
