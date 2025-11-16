// GA_MoveBase.h
// CodeRevision: INC-2025-00026-R1 (Fix garbled Japanese comments - Phase 1) (2025-11-16 00:00)
#pragma once

#include "CoreMinimal.h"
#include "GA_TurnActionBase.h"
#include "GameplayTagContainer.h"
#include "GA_MoveBase.generated.h"

// CodeRevision: INC-2025-00030-R1 (Migrate to UGridPathfindingSubsystem) (2025-11-16 23:55)
// Forward declarations
class UGridPathfindingSubsystem;
class AUnitBase;
class UGridOccupancySubsystem;
class AGameTurnManagerBase;

/**
 * UGA_MoveBase
 * 遘ｻ蜍輔い繝薙Μ繝・ぅ縺ｮ蝓ｺ蠎輔け繝ｩ繧ｹ
 *
 * Phase 3 繝ｪ繝輔ぃ繧ｯ繧ｿ繝ｪ繝ｳ繧ｰ: State.Action.InProgress + Barrier 蟇ｾ蠢・
 *
 * 讖溯・:
 * - EnhancedInput縺九ｉ蜿励￠蜿悶▲縺滓婿蜷代・繧ｯ繝医Ν繧偵げ繝ｪ繝・ラ遘ｻ蜍輔↓螟画鋤
 * - State.Moving 繧ｿ繧ｰ繧堤ｮ｡逅・＠縲、ctivationBlockedTags 縺ｧ驥崎､・ｮ溯｡後ｒ髦ｲ豁｢
 * - State.Action.InProgress 繧ｿ繧ｰ繧堤ｮ｡逅・＠縲∽ｻ悶・繧｢繧ｯ繧ｷ繝ｧ繝ｳ縺ｨ遶ｶ蜷医＠縺ｪ縺・ｈ縺・↓縺吶ｋ
 * - Barrier::RegisterAction() / CompleteAction() 縺ｧ繧｢繧ｯ繧ｷ繝ｧ繝ｳ縺ｮ螳御ｺ・ｒ邂｡逅・
 * - 遘ｻ蜍募ｮ御ｺ・凾縺ｫ Ability.Move.Completed 繧､繝吶Φ繝医ｒ逋ｺ陦後＠縲ゝurnManager 縺ｫ騾夂衍
 * - 3霆ｸ遘ｻ蜍包ｼ・/Y/Z・峨ｒ繧ｵ繝昴・繝医＠縲√げ繝ｪ繝・ラ荳ｭ蠢・∈縺ｮ繧ｹ繝翫ャ繝玲ｩ溯・繧呈署萓・
 * - TurnId縺ｨWindowId繧呈､懆ｨｼ縺励∝商縺・ち繝ｼ繝ｳ縺ｮ繧ｳ繝槭Φ繝峨ｒ辟｡隕・
 *
 * 螳溯｣・ｩｳ邏ｰ:
 * - State.Action.InProgress 繧ｿ繧ｰ繧・ActivationOwnedTags 縺ｧ邂｡逅・
 * - Barrier 繧ｷ繧ｹ繝・Β縺ｨ騾｣謳ｺ縺励※ RegisterAction / CompleteAction 繧貞他縺ｳ蜃ｺ縺・
 * - TurnManager 縺ｮ InProgress 繧ｿ繧ｰ邂｡逅・→蜷梧悄
 */
UCLASS(Abstract, Blueprintable)
class LYRAGAME_API UGA_MoveBase : public UGA_TurnActionBase
{
    GENERATED_BODY()

public:
    UGA_MoveBase(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

    //--------------------------------------------------------------------------
    // GameplayAbility 繧ｪ繝ｼ繝舌・繝ｩ繧､繝・
    //--------------------------------------------------------------------------

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

    /**
     * 笘・・笘・ChatGPT謠先｡・ 繧､繝吶Φ繝亥ｿ懃ｭ斐・霆ｽ驥上メ繧ｧ繝・け (2025-11-11) 笘・・笘・
     * HandleGameplayEvent returned 0 蝠城｡後ｒ隗｣豎ｺ縺吶ｋ縺溘ａ縲・
     * 繧､繝吶Φ繝医↓蠢懃ｭ斐☆繧句燕縺ｫ霆ｽ驥上↑讀懆ｨｼ繧定｡後≧縲・
     * 驥阪＞讀懆ｨｼ縺ｯActivateAbility縺ｧ螳溯｡後＆繧後ｋ縲・
     */
    virtual bool ShouldRespondToEvent(
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayEventData* Payload
    ) const;

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
    // Phase 6: 繝・ヰ繝・げ繝ｻ險域ｸｬ
    //==========================================================================

    /**
     * 繧｢繝薙Μ繝・ぅ髢句ｧ区凾蛻ｻ・医ョ繝舌ャ繧ｰ逕ｨ・・
     */
    double AbilityStartTime = 0.0;

    //--------------------------------------------------------------------------
    // 遘ｻ蜍輔ヱ繝ｩ繝｡繝ｼ繧ｿ
    //--------------------------------------------------------------------------

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Move")
    float Speed = 600.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Move")
    float SpeedBuff = 1.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Move")
    float SpeedDebuff = 1.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Move")
    float GridSize = 100.0f;

    /** 縺薙・霍晞屬莉･荳九・蝣ｴ蜷医・繧｢繝九Γ繝ｼ繧ｷ繝ｧ繝ｳ繧偵せ繧ｭ繝・・・・0縺ｧ辟｡蜉ｹ蛹厄ｼ・*/
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Move|Animation")
    float SkipAnimIfUnderDistance = 0.0f;  // 霍晞屬縺檎洒縺吶℃繧句ｴ蜷医・繧｢繝九Γ繝ｼ繧ｷ繝ｧ繝ｳ繧偵せ繧ｭ繝・・
    UPROPERTY(EditDefaultsOnly, Category = "GAS|Tags")
    FGameplayTag StateMovingTag = FGameplayTag::RequestGameplayTag(TEXT("State.Moving"));

    //--------------------------------------------------------------------------
    // 迥ｶ諷句､画焚・・lueprintReadOnly - 隱ｭ縺ｿ蜿悶ｊ蟆ら畑・・
    //--------------------------------------------------------------------------

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
    // C++蜀・Κ髢｢謨ｰ
    //--------------------------------------------------------------------------

    /** EventData縺九ｉDirection繧呈歓蜃ｺ */

    /** 8譁ｹ蜷代げ繝ｪ繝・ラ縺ｫ驥丞ｭ仙喧 */
    FVector2D QuantizeToGridDirection(const FVector& InDirection);

    /** 谺｡縺ｮ繧ｿ繧､繝ｫ菴咲ｽｮ繧定ｨ育ｮ・*/
    FVector CalculateNextTilePosition(const FVector& CurrentPosition, const FVector2D& Dir);

    /**
     * 遘ｻ蜍募・繧ｿ繧､繝ｫ縺碁夊｡悟庄閭ｽ縺九メ繧ｧ繝・け・・霆ｸ遘ｻ蜍募ｯｾ蠢懶ｼ・
     */
    bool IsTileWalkable(const FVector& TilePosition, AUnitBase* Self = nullptr);

    /** 繧ｿ繧､繝ｫ迥ｶ諷九ｒ譖ｴ譁ｰ */
    void UpdateGridState(const FVector& Position, int32 Value);

    /** 繧ｻ繝ｫ蜊倅ｽ阪〒騾夊｡悟庄閭ｽ縺九メ繧ｧ繝・け・・ccupancy/DistanceField繧剃ｽｿ逕ｨ・・*/
    bool IsTileWalkable(const FIntPoint& Cell) const;

    /** 繝ｨ繝ｼ隗偵ｒ45蠎ｦ蜊倅ｽ阪↓荳ｸ繧√ｋ */
    float RoundYawTo45Degrees(float Yaw);

    /** 遘ｻ蜍募ｮ御ｺ・ョ繝ｪ繧ｲ繝ｼ繝医ｒ繝舌う繝ｳ繝・*/
    void BindMoveFinishedDelegate();

    /** 遘ｻ蜍募ｮ御ｺ・凾縺ｮ繧ｳ繝ｼ繝ｫ繝舌ャ繧ｯ */
    UFUNCTION()
    void OnMoveFinished(AUnitBase* Unit);

    /** 謖・ｮ壹そ繝ｫ縺ｸ縺ｮ遘ｻ蜍輔ｒ髢句ｧ具ｼ医げ繝ｪ繝・ラ菴咲ｽｮ繧呈ｭ｣縺励￥險ｭ螳夲ｼ・*/
    void StartMoveToCell(const FIntPoint& TargetCell);

    //--------------------------------------------------------------------------
    // 3霆ｸ遘ｻ蜍輔・菴咲ｽｮ隱ｿ謨ｴ繝ｻ繝・ヰ繝・げ繝倥Ν繝代・
    //--------------------------------------------------------------------------

    FVector SnapToCellCenter(const FVector& WorldPos) const;
    FVector SnapToCellCenterFixedZ(const FVector& WorldPos, float FixedZ) const;
    // CodeRevision: INC-2025-00030-R1 (Migrate to UGridPathfindingSubsystem) (2025-11-16 23:55)
    float ComputeFixedZ(const AUnitBase* Unit, const UGridPathfindingSubsystem* Pathfinding) const;
    FVector AlignZToGround(const FVector& WorldPos, float TraceUp = 200.0f, float TraceDown = 2000.0f) const;
    void DebugDumpAround(const FIntPoint& Center);

    //--------------------------------------------------------------------------
    // Blueprint螳溯｣・庄閭ｽ繧､繝吶Φ繝医・繧｢繝九Γ繝ｼ繧ｷ繝ｧ繝ｳ蛻ｶ蠕｡
    //--------------------------------------------------------------------------

    UFUNCTION(BlueprintImplementableEvent, Category = "Move|Animation")
    void ExecuteMoveAnimation(const TArray<FVector>& Path);

    UFUNCTION(BlueprintNativeEvent, Category = "Move|Animation")
    bool ShouldSkipAnimation();
    virtual bool ShouldSkipAnimation_Implementation();

private:
    //--------------------------------------------------------------------------
    // CodeRevision: INC-2025-00018-R3 (Remove barrier management - Phase 3) (2025-11-17)
    // Barrier management removed - handled by other systems
    //--------------------------------------------------------------------------

    //--------------------------------------------------------------------------
    // State.Moving 繧ｿ繧ｰ邂｡逅・・繧ｭ繝｣繝・す繝･
    //--------------------------------------------------------------------------

    UPROPERTY()
    FGameplayTag TagStateMoving;

    //--------------------------------------------------------------------------
    // CodeRevision: INC-2025-00030-R1 (Migrate to UGridPathfindingSubsystem) (2025-11-16 23:55)
    // Removed CachedPathFinder - now using subsystem-based access
    //--------------------------------------------------------------------------

    mutable TWeakObjectPtr<AGameTurnManagerBase> CachedTurnManager;

    /**
     * PathFinder縺ｸ縺ｮ繧｢繧ｯ繧ｻ繧ｹ繧貞叙蠕暦ｼ医く繝｣繝・す繝･莉倥″・・
     * CodeRevision: INC-2025-00030-R2 (Migrate to UGridPathfindingSubsystem) (2025-11-17 00:40)
     * @deprecated Use GetGridPathfindingSubsystem() instead
     */
    const class UGridPathfindingSubsystem* GetPathFinder() const;

    /** Get GridPathfindingSubsystem (new subsystem-based access) */
    class UGridPathfindingSubsystem* GetGridPathfindingSubsystem() const;

    AGameTurnManagerBase* GetTurnManager() const;

    bool bMoveFinishedDelegateBound = false;

    // CodeRevision: INC-2025-00030-R3 (Add Barrier sync to GA_MoveBase) (2025-11-17 01:00)
    // P1: Fix turn hang by synchronizing player move with TurnActionBarrierSubsystem
    FGuid MoveActionId;
    int32 MoveTurnId = -1;
    bool bBarrierRegistered = false;

    //--------------------------------------------------------------------------
    // 遘ｻ蜍慕憾諷九く繝｣繝・す繝･繝ｻGridOccupancy騾｣謳ｺ
    //--------------------------------------------------------------------------

    FVector CachedStartLocWS = FVector::ZeroVector;

    /** 遘ｻ蜍募・繧ｻ繝ｫ・・nMoveFinished縺ｧ豁｣縺励＞菴咲ｽｮ險ｭ螳壹↓菴ｿ逕ｨ・・*/
    FIntPoint CachedNextCell = FIntPoint(-1, -1);

    //--------------------------------------------------------------------------
    // 繧ｭ繝｣繝・す繝･螟画焚・・nMoveFinished 縺ｧ菴ｿ逕ｨ・・
    //--------------------------------------------------------------------------

    FGameplayAbilitySpecHandle CachedSpecHandle;
    FGameplayAbilityActorInfo CachedActorInfo;
    FGameplayAbilityActivationInfo CachedActivationInfo;
    FVector CachedFirstLoc = FVector::ZeroVector;

    // Sparky菫ｮ豁｣: 蜀咲ｵゆｺ・亟豁｢繝輔Λ繧ｰ
    bool bIsEnding = false;

    // 笘・・笘・REMOVED: InProgressStack (2025-11-11) 笘・・笘・
    // ActivationOwnedTags 繧剃ｽｿ逕ｨ縺吶ｋ縺溘ａ縲∵焔蜍輔き繧ｦ繝ｳ繝医・荳崎ｦ・
};
