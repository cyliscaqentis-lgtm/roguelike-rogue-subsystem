// GA_AttackBase.cpp
#include "Abilities/GA_AttackBase.h"
#include "Abilities/GA_TurnActionBase.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "Utility/RogueGameplayTags.h"
#include "Turn/TurnActionBarrierSubsystem.h"
#include "Turn/AttackPhaseExecutorSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogAttackAbility, Log, All);

// CodeRevision: INC-2025-00033-R2
// (Reset Barrier state in EndAbility to avoid stale ActionId/TurnId reuse) (2025-11-18 11:00)

UGA_AttackBase::UGA_AttackBase(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;
    ReplicationPolicy = EGameplayAbilityReplicationPolicy::ReplicateYes;

    MaxExecutionTime = 5.0f;

    // AbilityTags設定（コンストラクタ）
    FGameplayTagContainer Tags;
    Tags.AddTag(RogueGameplayTags::Ability_Category_Attack);  // ネイティブタグを使用
    SetAssetTags(Tags);

    // ★★★ CRITICAL FIX (2025-11-11): GA_MoveBaseパターンに合わせて設定 ★★★

    // ActivationBlockedTags: アビリティ起動をブロックするタグ
    ActivationBlockedTags.AddTag(RogueGameplayTags::State_Action_InProgress);
    ActivationBlockedTags.AddTag(RogueGameplayTags::State_Ability_Executing);

    // ActivationOwnedTags: アビリティ起動時に自動追加されるタグ
    // これにより、ActivateAbility で自動追加、EndAbility で自動削除される
    ActivationOwnedTags.AddTag(RogueGameplayTags::State_Action_InProgress);

    // AbilityTriggers: このアビリティを起動するGameplayEventを登録
    // ★★★ これがないと、GameplayEvent.Intent.Attack が来ても起動しない！ ★★★
    FAbilityTriggerData Trigger;
    Trigger.TriggerTag = RogueGameplayTags::GameplayEvent_Intent_Attack;
    Trigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
    AbilityTriggers.Add(Trigger);

    UE_LOG(LogTemp, Log,
        TEXT("[GA_AttackBase] Constructor: Registered trigger %s, ActivationBlocked=%d, ActivationOwned=%d"),
        *Trigger.TriggerTag.ToString(),
        ActivationBlockedTags.Num(),
        ActivationOwnedTags.Num());
}

//==============================================================================
// ★★★ FIX (2025-11-12): BP上書き対策 - PostInitPropertiesで強制設定 ★★★
//==============================================================================

void UGA_AttackBase::PostInitProperties()
{
    Super::PostInitProperties();

    // ★★★ BP継承時にAbilityTriggersが空になる問題を修正 ★★★
    // PostInitPropertiesはBPのデフォルト値設定後に呼ばれるため、
    // ここで再度設定すれば確実にTriggerが登録される

    bool bNeedsFix = true;

    // 既にGameplayEvent.Intent.Attackが登録されているかチェック
    for (const FAbilityTriggerData& Trigger : AbilityTriggers)
    {
        if (Trigger.TriggerTag == RogueGameplayTags::GameplayEvent_Intent_Attack)
        {
            bNeedsFix = false;
            break;
        }
    }

    if (bNeedsFix)
    {
        // AbilityTriggersをクリアして再設定
        AbilityTriggers.Empty();

        FAbilityTriggerData Trigger;
        Trigger.TriggerTag = RogueGameplayTags::GameplayEvent_Intent_Attack;
        Trigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
        AbilityTriggers.Add(Trigger);

        UE_LOG(LogTemp, Warning,
            TEXT("[GA_AttackBase::PostInitProperties] ★ FIXED: Re-registered trigger %s (was missing or cleared by BP)"),
            *Trigger.TriggerTag.ToString());
    }
    else
    {
        // This log is redundant because the trigger being registered is normal.
        // const FGameplayTag AttackEventTag = RogueGameplayTags::GameplayEvent_Intent_Attack;
        // UE_LOG(LogTemp, Log,
        //     TEXT("[GA_AttackBase::PostInitProperties] Trigger already registered: %s"),
        //     *AttackEventTag.ToString());
    }
}

void UGA_AttackBase::ExecuteAttack_Implementation(AActor* Target)
{
    if (!Target)
    {
        UE_LOG(LogTemp, Warning, TEXT("[GA_AttackBase] Target is null"));
        OnAttackCompleted();
        return;
    }

    AActor* Avatar = GetAvatarActorFromActorInfo();
    if (!Avatar)
    {
        UE_LOG(LogTemp, Warning, TEXT("[GA_AttackBase] Avatar is null"));
        OnAttackCompleted();
        return;
    }

    // ★★★ 注意: 距離チェックは子クラス（GA_MeleeAttackなど）で実装 ★★★
    // 基底クラスではタイルベース距離を計算できないため、ここでは省略

    const float Damage = CalculateDamage(Target);

    if (Damage > 0.0f)
    {
        UE_LOG(LogTemp, Verbose, TEXT("[GA_AttackBase] Attack hit: %.2f damage to %s"), Damage, *Target->GetName());
        OnAttackHit(Target, Damage);
    }
    else
    {
        UE_LOG(LogTemp, Verbose, TEXT("[GA_AttackBase] Attack missed: %s"), *Target->GetName());
        OnAttackMiss(Target);
    }
}

float UGA_AttackBase::CalculateDamage_Implementation(AActor* Target) const
{
    const float FinalDamage = FMath::Max(0.0f, BaseDamage);
    return FinalDamage;
}

void UGA_AttackBase::OnAttackHit_Implementation(AActor* Target, float DamageDealt)
{
    UE_LOG(LogTemp, Verbose, TEXT("[GA_AttackBase] OnAttackHit: %s (%.2f damage)"), *Target->GetName(), DamageDealt);
}

void UGA_AttackBase::OnAttackMiss_Implementation(AActor* Target)
{
    UE_LOG(LogTemp, Verbose, TEXT("[GA_AttackBase] OnAttackMiss: %s"), *Target->GetName());
}

void UGA_AttackBase::OnAttackCompleted()
{
    if (!HasAuthority(&CurrentActivationInfo))
    {
        UE_LOG(LogTemp, Warning, TEXT("[GA_AttackBase] OnAttackCompleted called on client (ignored)"));
        return;
    }

    UE_LOG(LogTemp, Verbose, TEXT("[GA_AttackBase] Attack completed"));

    // EndAbility will handle tag cleanup, barrier completion, and sending the completion event via its parent class.
    // The previous call to SendCompletionEvent(false) here was causing a race condition.
    EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

TArray<AActor*> UGA_AttackBase::FindTargetsInRange(float SearchRange) const
{
    TArray<AActor*> FoundTargets;

    AActor* Avatar = GetAvatarActorFromActorInfo();
    if (!Avatar)
    {
        return FoundTargets;
    }

    // ★★★ タイルベース射程をWorld距離（cm）に概算変換 ★★★
    // タイルサイズ = 100cm と仮定
    const float TileSize = 100.0f;
    const float DefaultRangeInWorld = RangeInTiles * TileSize;
    const float ActualRange = (SearchRange > 0.0f) ? SearchRange : DefaultRangeInWorld;

    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(Avatar->GetWorld(), AActor::StaticClass(), AllActors);

    const FVector AvatarLocation = Avatar->GetActorLocation();
    for (AActor* Actor : AllActors)
    {
        if (Actor && Actor != Avatar)
        {
            const float Distance = FVector::Dist(AvatarLocation, Actor->GetActorLocation());
            if (Distance <= ActualRange)
            {
                FoundTargets.Add(Actor);
            }
        }
    }

    UE_LOG(LogTemp, Verbose, TEXT("[GA_AttackBase] Found %d targets in range %.2f"), FoundTargets.Num(), ActualRange);
    return FoundTargets;
}

//==============================================================================
// ★★★ Barrier統合 (2025-11-12): 攻撃アクションをBarrierに登録 ★★★
//==============================================================================

void UGA_AttackBase::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    const FGameplayEventData* TriggerEventData)
{
    // 親クラスのActivateAbilityを呼ぶ（タイムアウトタイマー、State.Ability.Executing追加など）
    Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

    // ★★★ Barrier登録：攻撃もターン進行のブロック対象として管理 ★★★
    AActor* Avatar = GetAvatarActorFromActorInfo();
    if (!Avatar)
    {
        UE_LOG(LogAttackAbility, Warning,
            TEXT("[GA_AttackBase] ActivateAbility: Avatar is null, cannot register with Barrier"));
        return;
    }

    UWorld* World = Avatar->GetWorld();
    if (!World)
    {
        UE_LOG(LogAttackAbility, Warning,
            TEXT("[GA_AttackBase] ActivateAbility: World is null"));
        return;
    }

    UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>();
    if (!Barrier)
    {
        UE_LOG(LogAttackAbility, Warning,
            TEXT("[GA_AttackBase] ActivateAbility: TurnActionBarrierSubsystem not found"));
        return;
    }

    // ★★★ BUGFIX [INC-2025-TIMING]: Retrieve pre-registered ActionId instead of registering again ★★★
    // AttackPhaseExecutor pre-registers all attacks to prevent premature turn completion
    UAttackPhaseExecutorSubsystem* AttackExecutor = World->GetSubsystem<UAttackPhaseExecutorSubsystem>();
    if (!AttackExecutor)
    {
        UE_LOG(LogAttackAbility, Error,
            TEXT("[GA_AttackBase] ActivateAbility: AttackPhaseExecutorSubsystem not found! Cannot retrieve ActionId"));
        return;
    }

    AttackTurnId = Barrier->GetCurrentTurnId();
    AttackActionId = AttackExecutor->GetActionIdForActor(Avatar);
    bBarrierRegistered = AttackActionId.IsValid();

    // ★★★ FIX: Player Attack Support (2025-11-19) ★★★
    // If the attack was NOT pre-registered (e.g. Player input during their turn),
    // we must register it with the Barrier now, similar to GA_MoveBase.
    if (!bBarrierRegistered)
    {
        UE_LOG(LogAttackAbility, Warning,
            TEXT("[GA_AttackBase] ActivateAbility: ActionId not found in AttackExecutor (Normal for Player Input). Registering with Barrier directly."));

        AttackActionId = Barrier->RegisterAction(Avatar, AttackTurnId);
        bBarrierRegistered = AttackActionId.IsValid();

        if (bBarrierRegistered)
        {
             UE_LOG(LogAttackAbility, Log,
                TEXT("[GA_AttackBase] ActivateAbility: Registered with Barrier (TurnId=%d, ActionId=%s, Actor=%s)"),
                AttackTurnId, *AttackActionId.ToString(), *Avatar->GetName());
        }
        else
        {
             UE_LOG(LogAttackAbility, Error,
                TEXT("[GA_AttackBase] ActivateAbility: Failed to register with Barrier! (TurnId=%d, Actor=%s)"),
                AttackTurnId, *Avatar->GetName());
             return;
        }
    }
    else
    {
        UE_LOG(LogAttackAbility, Log,
            TEXT("[GA_AttackBase] ActivateAbility: Retrieved pre-registered ActionId from AttackExecutor (TurnId=%d, ActionId=%s, Actor=%s)"),
            AttackTurnId, *AttackActionId.ToString(), *Avatar->GetName());
    }
}

void UGA_AttackBase::EndAbility(const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    bool bReplicateEndAbility,
    bool bWasCancelled)
{
    // To fix a race condition, we must call the parent EndAbility FIRST.
    // This ensures all gameplay tags (like State.Action.InProgress) are removed
    // BEFORE we notify the barrier. Notifying the barrier can synchronously
    // trigger the next turn, and if the tags are still present, input will be blocked.
    Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);

    // ★★★ Barrier完了通知：攻撃終了をBarrierに通知してターン進行を許可 ★★★
    if (bBarrierRegistered)
    {
        AActor* Avatar = GetAvatarActorFromActorInfo();
        if (Avatar)
        {
            if (UWorld* World = Avatar->GetWorld())
            {
                if (UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>())
                {
                    Barrier->CompleteAction(Avatar, AttackTurnId, AttackActionId);

                    UE_LOG(LogAttackAbility, Log,
                        TEXT("[GA_AttackBase] EndAbility: Completed attack in Barrier (TurnId=%d, ActionId=%s, Actor=%s)"),
                        AttackTurnId, *AttackActionId.ToString(), *Avatar->GetName());
                }
            }
        }
    }

    // CodeRevision: INC-2025-00033-R2
    // Ability再利用時に古い TurnId / ActionId を誤用しないよう、ここで必ずクリアする。
    AttackTurnId = INDEX_NONE;
    AttackActionId.Invalidate();
    bBarrierRegistered = false;
}
