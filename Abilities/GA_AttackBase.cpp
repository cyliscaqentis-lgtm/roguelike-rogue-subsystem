// GA_AttackBase.cpp
#include "Abilities/GA_AttackBase.h"
#include "Abilities/GA_TurnActionBase.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "Utility/RogueGameplayTags.h"
#include "Turn/TurnActionBarrierSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogAttackAbility, Log, All);

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
        UE_LOG(LogTemp, Log,
            TEXT("[GA_AttackBase::PostInitProperties] Trigger already registered: %s"),
            *RogueGameplayTags::GameplayEvent_Intent_Attack.ToString());
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

    const float Distance = FVector::Dist(Avatar->GetActorLocation(), Target->GetActorLocation());
    if (Distance > Range)
    {
        UE_LOG(LogTemp, Warning, TEXT("[GA_AttackBase] Target out of range: %.2f > %.2f"), Distance, Range);
        OnAttackMiss(Target);
        OnAttackCompleted();
        return;
    }

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

    SendCompletionEvent(false);
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

    const float ActualRange = (SearchRange > 0.0f) ? SearchRange : Range;

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

    // Barrierに攻撃アクションを登録
    AttackTurnId = Barrier->GetCurrentTurnId();
    AttackActionId = Barrier->RegisterAction(Avatar, AttackTurnId);
    bBarrierRegistered = true;

    UE_LOG(LogAttackAbility, Log,
        TEXT("[GA_AttackBase] ActivateAbility: Registered attack with Barrier (TurnId=%d, ActionId=%s, Actor=%s)"),
        AttackTurnId, *AttackActionId.ToString(), *Avatar->GetName());
}

void UGA_AttackBase::EndAbility(const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    bool bReplicateEndAbility,
    bool bWasCancelled)
{
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

                    bBarrierRegistered = false;
                }
            }
        }
    }

    // 親クラスのEndAbilityを呼ぶ（タイムアウトクリア、State.Ability.Executing削除など）
    Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
