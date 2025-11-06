#include "Abilities/GA_MeleeAttack.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "GameplayEffect.h"
#include "Kismet/GameplayStatics.h"
#include "Rogue/Character/UnitBase.h"
#include "Player/LyraPlayerState.h"
#include "Turn/GameTurnManagerBase.h"
#include "Utility/RogueGameplayTags.h"
#include "Animation/AnimMontage.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

UGA_MeleeAttack::UGA_MeleeAttack(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;
    ReplicationPolicy = EGameplayAbilityReplicationPolicy::ReplicateYes;

    MaxExecutionTime = 5.0f;
    BaseDamage = 28.0f;
    Range = 150.0f; // 隣接判定用（1.5タイル相当）

    // アセットタグ
    FGameplayTagContainer Tags;
    Tags.AddTag(RogueGameplayTags::Ability_Attack);
    Tags.AddTag(RogueGameplayTags::Ability_Melee);
    Tags.AddTag(RogueGameplayTags::Ability_MeleeAttack);  // ネイティブタグを使用
    SetAssetTags(Tags);

    ActivationBlockedTags.AddTag(RogueGameplayTags::State_Ability_Executing);
}

//------------------------------------------------------------------------------
// ActivateAbility・医Γ繧､繝ｳ繝ｭ繧ｸ繝・け・・//------------------------------------------------------------------------------

void UGA_MeleeAttack::ActivateAbility(
    const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    const FGameplayEventData* TriggerEventData)
{
    Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

    if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEndAbility=*/true, /*bWasCancelled=*/true);
        return;
    }

    // 入力を一時無効化（プレイヤー操作のみ）
    if (ActorInfo && ActorInfo->AvatarActor.IsValid())
    {
        if (APawn* Pawn = Cast<APawn>(ActorInfo->AvatarActor.Get()))
        {
            if (APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
            {
                PC->SetIgnoreMoveInput(true);
                bInputDisabled = true;
            }
        }
    }

    // 攻撃モンタージュを再生
    PlayAttackMontage();
}

//------------------------------------------------------------------------------
// 繝｢繝ｳ繧ｿ繝ｼ繧ｸ繝･蜀咲函・・++繝・ヵ繧ｩ繝ｫ繝亥ｮ溯｣・ｼ・//------------------------------------------------------------------------------

void UGA_MeleeAttack::PlayAttackMontage_Implementation()
{
    if (!MeleeAttackMontage)
    {
        UE_LOG(LogTemp, Warning, TEXT("[GA_MeleeAttack] MeleeAttackMontage is null"));
        OnMontageCompleted();
        return;
    }

    UAbilityTask_PlayMontageAndWait* Task =
        UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
            this, FName("MeleeAttack"), MeleeAttackMontage, 1.0f);
    check(Task);
    Task->OnCompleted.AddDynamic(this, &UGA_MeleeAttack::OnMontageCompleted);
    Task->OnBlendOut.AddDynamic(this, &UGA_MeleeAttack::OnMontageBlendOut);
    Task->OnCancelled.AddDynamic(this, &UGA_MeleeAttack::OnMontageCancelled);
    Task->ReadyForActivation();
}

//------------------------------------------------------------------------------
// 繝｢繝ｳ繧ｿ繝ｼ繧ｸ繝･螳御ｺ・さ繝ｼ繝ｫ繝舌ャ繧ｯ
//------------------------------------------------------------------------------

void UGA_MeleeAttack::OnMontageCompleted()
{
    if (AActor* Target = FindAdjacentTarget())
    {
        ApplyDamageToTarget(Target);
    }

    FTimerHandle DelayHandle;
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().SetTimer(
            DelayHandle,
            [this]()
            {
                EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
            },
            0.2f,
            false);
    }
}

void UGA_MeleeAttack::OnMontageBlendOut()
{
    // 必要なら追従処理
}

void UGA_MeleeAttack::OnMontageCancelled()
{
    UE_LOG(LogTemp, Warning, TEXT("[GA_MeleeAttack] Montage cancelled"));
    EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}

//------------------------------------------------------------------------------
// 髫｣謗･繧ｿ繝ｼ繧ｲ繝・ヨ讀懃ｴ｢
//------------------------------------------------------------------------------

AActor* UGA_MeleeAttack::FindAdjacentTarget()
{
    AActor* Avatar = GetAvatarActorFromActorInfo();
    if (!Avatar)
    {
        return nullptr;
    }

    if (AUnitBase* Unit = Cast<AUnitBase>(Avatar))
    {
        TArray<AUnitBase*> AdjacentPlayers = Unit->GetAdjacentPlayers();
        if (AdjacentPlayers.Num() > 0 && IsValid(AdjacentPlayers[0]))
        {
            return AdjacentPlayers[0];
        }
    }

    return nullptr;
}

//------------------------------------------------------------------------------
// 繝繝｡繝ｼ繧ｸ驕ｩ逕ｨ
//------------------------------------------------------------------------------

void UGA_MeleeAttack::ApplyDamageToTarget(AActor* Target)
{
    if (!Target || !MeleeAttackEffect)
    {
        UE_LOG(LogTemp, Warning, TEXT("[GA_MeleeAttack] Invalid Target or Effect"));
        return;
    }

    if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
    {
        FGameplayEffectContextHandle ContextHandle = ASC->MakeEffectContext();
        ContextHandle.AddSourceObject(this);

        FGameplayEffectSpecHandle SpecHandle = ASC->MakeOutgoingSpec(MeleeAttackEffect, 1.0f, ContextHandle);
        if (SpecHandle.IsValid())
        {
            const FGameplayTag DamageTag = FGameplayTag::RequestGameplayTag(TEXT("SetByCaller.Damage"));
            SpecHandle.Data->SetSetByCallerMagnitude(DamageTag, BaseDamage);

            if (UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target))
            {
                ASC->ApplyGameplayEffectSpecToTarget(*SpecHandle.Data.Get(), TargetASC);
            }
        }
    }
}

//------------------------------------------------------------------------------
// EndAbility・医け繝ｪ繝ｼ繝ｳ繧｢繝・・・・
//------------------------------------------------------------------------------

void UGA_MeleeAttack::EndAbility(
    const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    bool bReplicateEndAbility,
    bool bWasCancelled)
{
    if (bInputDisabled && ActorInfo && ActorInfo->AvatarActor.IsValid())
    {
        if (APawn* Pawn = Cast<APawn>(ActorInfo->AvatarActor.Get()))
        {
            if (APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
            {
                PC->SetIgnoreMoveInput(false);
            }
        }
    }
    bInputDisabled = false;

    Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

//------------------------------------------------------------------------------
// TurnManager蜿門ｾ励・繝ｫ繝代・
//------------------------------------------------------------------------------

AGameTurnManagerBase* UGA_MeleeAttack::GetTurnManager() const
{
    if (!CachedTurnManager.IsValid())
    {
        // 笘・・笘・繧ｭ繝｣繝・す繝･縺檎┌蜉ｹ縺ｪ蝣ｴ蜷医・蜀榊叙蠕励ｒ隧ｦ縺ｿ繧・笘・・笘・
        if (UWorld* World = GetWorld())
        {
            TArray<AActor*> FoundActors;
            UGameplayStatics::GetAllActorsOfClass(World, AGameTurnManagerBase::StaticClass(), FoundActors);
            if (FoundActors.Num() > 0)
            {
                const_cast<UGA_MeleeAttack*>(this)->CachedTurnManager = Cast<AGameTurnManagerBase>(FoundActors[0]);
            }
        }
    }
    return CachedTurnManager.Get();
}
