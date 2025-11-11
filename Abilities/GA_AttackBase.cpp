// GA_AttackBase.cpp
#include "Abilities/GA_AttackBase.h"
#include "Abilities/GA_TurnActionBase.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "Utility/RogueGameplayTags.h"

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
