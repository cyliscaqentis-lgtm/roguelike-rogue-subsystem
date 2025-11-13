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
#include "Grid/GridPathfindingLibrary.h"
#include "EngineUtils.h"
#include "Character/UnitBase.h"

UGA_MeleeAttack::UGA_MeleeAttack(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;
    ReplicationPolicy = EGameplayAbilityReplicationPolicy::ReplicateYes;

    MaxExecutionTime = 5.0f;
    BaseDamage = 28.0f;
    RangeInTiles = 1; // 近接攻撃は隣接（1マス）のみ

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
    // ★★★ CRITICAL DIAGNOSTIC (2025-11-12): アビリティ起動の確認 ★★★
    UE_LOG(LogTemp, Error,
        TEXT("[GA_MeleeAttack] ===== ActivateAbility CALLED ===== Actor=%s"),
        *GetNameSafe(GetAvatarActorFromActorInfo()));

    Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

    UE_LOG(LogTemp, Error,
        TEXT("[GA_MeleeAttack] After Super::ActivateAbility, attempting CommitAbility"));

    if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
    {
        UE_LOG(LogTemp, Error,
            TEXT("[GA_MeleeAttack] CommitAbility FAILED - Ending ability"));
        EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEndAbility=*/true, /*bWasCancelled=*/true);
        return;
    }

    UE_LOG(LogTemp, Error,
        TEXT("[GA_MeleeAttack] CommitAbility SUCCESS - Proceeding with attack logic"));

    // ★★★ FIX (2025-11-11): EventDataからターゲットを抽出 ★★★
    // AI決定時に保存されたターゲットを使用（実行時検索を回避）
    if (TriggerEventData && TriggerEventData->TargetData.IsValid(0))
    {
        const FGameplayAbilityTargetData* TargetData = TriggerEventData->TargetData.Get(0);
        if (const FGameplayAbilityTargetData_SingleTargetHit* SingleTarget =
            static_cast<const FGameplayAbilityTargetData_SingleTargetHit*>(TargetData))
        {
            if (AActor* Target = SingleTarget->HitResult.GetActor())
            {
                TargetUnit = Target;
                UE_LOG(LogTemp, Log, TEXT("[GA_MeleeAttack] %s: Target from EventData: %s"),
                    *GetNameSafe(GetAvatarActorFromActorInfo()), *GetNameSafe(Target));
            }
        }
    }

    // フォールバック：EventDataにターゲットがない場合は従来の検索方式
    if (!TargetUnit)
    {
        TargetUnit = FindAdjacentTarget();
        if (TargetUnit)
        {
            UE_LOG(LogTemp, Warning, TEXT("[GA_MeleeAttack] %s: Using fallback adjacent search, found: %s"),
                *GetNameSafe(GetAvatarActorFromActorInfo()), *GetNameSafe(TargetUnit));
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[GA_MeleeAttack] %s: No target found (neither EventData nor adjacent)"),
                *GetNameSafe(GetAvatarActorFromActorInfo()));
        }
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

    //==========================================================================
    // ★★★ CRITICAL FIX (2025-11-11): ターゲットの方を向く ★★★
    // 理由: 敵がプレイヤーを攻撃する際、ターゲットの方向を向いていなかった
    // ★★★ FIX (2025-11-13): UnitBaseのMulticastRPCで全クライアントに通知 ★★★
    //==========================================================================
    if (TargetUnit && ActorInfo && ActorInfo->AvatarActor.IsValid())
    {
        AActor* Avatar = ActorInfo->AvatarActor.Get();
        if (AUnitBase* UnitAvatar = Cast<AUnitBase>(Avatar))
        {
            if (IsValid(TargetUnit))
            {
                const FVector AttackerLoc = Avatar->GetActorLocation();
                const FVector TargetLoc = TargetUnit->GetActorLocation();
                FVector ToTarget = TargetLoc - AttackerLoc;
                ToTarget.Z = 0.0f;  // 水平方向のみ（Z軸は無視）

                UE_LOG(LogTemp, Warning, TEXT("[GA_MeleeAttack] ===== ROTATION DIAGNOSTIC ====="));
                UE_LOG(LogTemp, Warning, TEXT("[GA_MeleeAttack] Attacker: %s at (%.1f, %.1f, %.1f)"),
                    *GetNameSafe(Avatar), AttackerLoc.X, AttackerLoc.Y, AttackerLoc.Z);
                UE_LOG(LogTemp, Warning, TEXT("[GA_MeleeAttack] Target: %s at (%.1f, %.1f, %.1f)"),
                    *GetNameSafe(TargetUnit), TargetLoc.X, TargetLoc.Y, TargetLoc.Z);
                UE_LOG(LogTemp, Warning, TEXT("[GA_MeleeAttack] Direction vector: (%.1f, %.1f) Length: %.1f"),
                    ToTarget.X, ToTarget.Y, ToTarget.Size());

                if (!ToTarget.IsNearlyZero())
                {
                    FRotator NewRotation = ToTarget.Rotation();

                    UE_LOG(LogTemp, Warning, TEXT("[GA_MeleeAttack] Calling Multicast_SetRotation with Yaw=%.1f"),
                        NewRotation.Yaw);

                    // AvatarがUnitBaseの場合、MulticastRPCで全クライアントに回転を通知
                    UnitAvatar->Multicast_SetRotation(NewRotation);

                    UE_LOG(LogTemp, Warning, TEXT("[GA_MeleeAttack] Multicast_SetRotation call completed"));
                }
                else
                {
                    UE_LOG(LogTemp, Error, TEXT("[GA_MeleeAttack] %s: Cannot rotate - target is at same location (distance=%.3f)"),
                        *GetNameSafe(Avatar), ToTarget.Size());
                }
            }
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("[GA_MeleeAttack] %s: Avatar is not a UnitBase - cannot use Multicast RPC"),
                *GetNameSafe(Avatar));
        }
    }
    else if (!TargetUnit)
    {
        UE_LOG(LogTemp, Error, TEXT("[GA_MeleeAttack] %s: Cannot rotate - no target"),
            *GetNameSafe(GetAvatarActorFromActorInfo()));
    }

    // 攻撃モンタージュを再生
    UE_LOG(LogTemp, Error,
        TEXT("[GA_MeleeAttack] About to call PlayAttackMontage(), MontagePtr=%s"),
        MeleeAttackMontage ? *MeleeAttackMontage->GetName() : TEXT("NULL"));
    PlayAttackMontage();
    UE_LOG(LogTemp, Error,
        TEXT("[GA_MeleeAttack] ActivateAbility COMPLETED"));
}

//------------------------------------------------------------------------------
// 繝｢繝ｳ繧ｿ繝ｼ繧ｸ繝･蜀咲函・・++繝・ヵ繧ｩ繝ｫ繝亥ｮ溯｣・ｼ・//------------------------------------------------------------------------------

void UGA_MeleeAttack::PlayAttackMontage_Implementation()
{
    UE_LOG(LogTemp, Error,
        TEXT("[GA_MeleeAttack] PlayAttackMontage_Implementation CALLED, Montage=%s"),
        MeleeAttackMontage ? *MeleeAttackMontage->GetName() : TEXT("NULL"));

    if (!MeleeAttackMontage)
    {
        UE_LOG(LogTemp, Error, TEXT("[GA_MeleeAttack] MeleeAttackMontage is NULL - calling OnMontageCompleted immediately"));
        OnMontageCompleted();
        return;
    }

    // ★★★ DIAGNOSTIC (2025-11-13): モンタージュの長さと開始時刻を出力 ★★★
    const float MontageLength = MeleeAttackMontage->GetPlayLength();
    const double StartTime = FPlatformTime::Seconds();
    UE_LOG(LogTemp, Error, TEXT("[GA_MeleeAttack] Montage length: %.3f seconds, StartTime=%.3f (if >5 seconds, this is TOO LONG!)"),
        MontageLength, StartTime);

    UE_LOG(LogTemp, Error, TEXT("[GA_MeleeAttack] Creating PlayMontageAndWait task..."));

    UAbilityTask_PlayMontageAndWait* Task =
        UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
            this, FName("MeleeAttack"), MeleeAttackMontage, 1.0f);
    check(Task);
    Task->OnCompleted.AddDynamic(this, &UGA_MeleeAttack::OnMontageCompleted);
    Task->OnBlendOut.AddDynamic(this, &UGA_MeleeAttack::OnMontageBlendOut);
    Task->OnCancelled.AddDynamic(this, &UGA_MeleeAttack::OnMontageCancelled);
    Task->ReadyForActivation();

    UE_LOG(LogTemp, Error, TEXT("[GA_MeleeAttack] PlayMontageAndWait task activated at %.3f, waiting for completion..."),
        StartTime);
}

//------------------------------------------------------------------------------
// 繝｢繝ｳ繧ｿ繝ｼ繧ｸ繝･螳御ｺ・さ繝ｼ繝ｫ繝舌ャ繧ｯ
//------------------------------------------------------------------------------

void UGA_MeleeAttack::OnMontageCompleted()
{
    // ★★★ DIAGNOSTIC (2025-11-13): 完了タイミングを診断 ★★★
    const double CurrentTime = FPlatformTime::Seconds();
    UE_LOG(LogTemp, Error,
        TEXT("[GA_MeleeAttack] ===== OnMontageCompleted CALLED ===== Actor=%s, Time=%.3f"),
        *GetNameSafe(GetAvatarActorFromActorInfo()), CurrentTime);

    // ★★★ FIX (2025-11-11): ActivateAbilityで保存されたターゲットを使用 ★★★
    // 実行時に再検索せず、AI決定時に保存されたターゲットを攻撃
    if (TargetUnit && IsValid(TargetUnit))
    {
        UE_LOG(LogTemp, Error, TEXT("[GA_MeleeAttack] %s: Attacking stored target: %s"),
            *GetNameSafe(GetAvatarActorFromActorInfo()), *GetNameSafe(TargetUnit));
        ApplyDamageToTarget(TargetUnit);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[GA_MeleeAttack] %s: No valid target to attack (TargetUnit is null or invalid)"),
            *GetNameSafe(GetAvatarActorFromActorInfo()));
    }

    //==========================================================================
    // ★★★ CRITICAL FIX (2025-11-11): 遅延を削除して即座にEndAbilityを呼ぶ ★★★
    // 理由: 0.2秒の遅延がターンをまたいで完了通知が届く原因だった
    //       OnMontageCompletedが呼ばれた時点でモンタージュは完了済みなので遅延不要
    //==========================================================================
    UE_LOG(LogTemp, Error, TEXT("[GA_MeleeAttack] %s: Montage completed, calling EndAbility immediately"),
        *GetNameSafe(GetAvatarActorFromActorInfo()));
    EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
    UE_LOG(LogTemp, Error, TEXT("[GA_MeleeAttack] EndAbility RETURNED"));

    // ★★★ 削除: 0.2秒遅延タイマー（ターンブロック問題の原因）
    // FTimerHandle DelayHandle;
    // if (UWorld* World = GetWorld())
    // {
    //     World->GetTimerManager().SetTimer(
    //         DelayHandle,
    //         [this]()
    //         {
    //             EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
    //         },
    //         0.2f,
    //         false);
    // }
}

void UGA_MeleeAttack::OnMontageBlendOut()
{
    //==========================================================================
    // ★★★ FIX (2025-11-11): BlendOut時も適切に処理 ★★★
    // OnCompletedで既にEndAbilityが呼ばれているはずだが、
    // 念のためBlendOutでも呼ぶ（二重呼び出しは内部で防御される）
    //==========================================================================
    UE_LOG(LogTemp, Verbose, TEXT("[GA_MeleeAttack] %s: Montage blend out"),
        *GetNameSafe(GetAvatarActorFromActorInfo()));

    // OnCompletedが先に呼ばれている可能性が高いため、
    // ここでは何もしない（OnCompletedで即座にEndAbilityを呼ぶようになった）
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

    AActor* Avatar = GetAvatarActorFromActorInfo();
    if (!Avatar)
    {
        UE_LOG(LogTemp, Warning, TEXT("[GA_MeleeAttack] Avatar is null"));
        return;
    }

    // ★★★ DIAGNOSTIC (2025-11-12): ダメージ倍率がゼロになる原因を特定 ★★★
    UE_LOG(LogTemp, Warning,
        TEXT("[GA_MeleeAttack] ==== DAMAGE DIAGNOSTIC START ===="));
    UE_LOG(LogTemp, Warning,
        TEXT("[GA_MeleeAttack] Attacker=%s, Target=%s, BaseDamage=%.2f"),
        *Avatar->GetName(), *Target->GetName(), BaseDamage);

    // Team check diagnostic
    if (AUnitBase* AttackerUnit = Cast<AUnitBase>(Avatar))
    {
        if (AUnitBase* TargetUnitRef = Cast<AUnitBase>(Target))
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[GA_MeleeAttack] AttackerTeam=%d, TargetTeam=%d (SameTeam=%s)"),
                AttackerUnit->Team, TargetUnitRef->Team,
                AttackerUnit->Team == TargetUnitRef->Team ? TEXT("YES - DAMAGE BLOCKED") : TEXT("NO - OK"));
        }
    }

    // ★★★ FIX (2025-11-12): タイルベースの距離計算（斜め対応） ★★★
    int32 DistanceInTiles = -1;
    AGridPathfindingLibrary* GridLib = nullptr;

    // GridPathfindingLibraryを取得
    if (UWorld* World = GetWorld())
    {
        for (TActorIterator<AGridPathfindingLibrary> It(World); It; ++It)
        {
            GridLib = *It;
            break;
        }
    }

    if (GridLib)
    {
        FIntPoint AttackerGrid = GridLib->WorldToGrid(Avatar->GetActorLocation());
        FIntPoint TargetGrid = GridLib->WorldToGrid(Target->GetActorLocation());

        // マンハッタン距離（斜めも正しく計算される）
        DistanceInTiles = FMath::Abs(AttackerGrid.X - TargetGrid.X) + FMath::Abs(AttackerGrid.Y - TargetGrid.Y);

        UE_LOG(LogTemp, Warning,
            TEXT("[GA_MeleeAttack] Attacker=(%d,%d), Target=(%d,%d), DistanceInTiles=%d, RangeInTiles=%d (InRange=%s)"),
            AttackerGrid.X, AttackerGrid.Y, TargetGrid.X, TargetGrid.Y,
            DistanceInTiles, RangeInTiles,
            DistanceInTiles <= RangeInTiles ? TEXT("YES") : TEXT("NO - TOO FAR"));
    }
    else
    {
        UE_LOG(LogTemp, Error,
            TEXT("[GA_MeleeAttack] GridPathfindingLibrary not found - cannot calculate tile distance!"));
    }

    // ASC and Tags diagnostic
    if (UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target))
    {
        FGameplayTagContainer TargetTags;
        TargetASC->GetOwnedGameplayTags(TargetTags);
        UE_LOG(LogTemp, Warning,
            TEXT("[GA_MeleeAttack] TargetTags=%s"),
            *TargetTags.ToStringSimple());
    }
    else
    {
        UE_LOG(LogTemp, Error,
            TEXT("[GA_MeleeAttack] Target has NO AbilitySystemComponent - cannot apply damage!"));
    }

    UE_LOG(LogTemp, Warning,
        TEXT("[GA_MeleeAttack] ==== DAMAGE DIAGNOSTIC END ===="));

    if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
    {
        FGameplayEffectContextHandle ContextHandle = ASC->MakeEffectContext();
        ContextHandle.AddSourceObject(this);

        FGameplayEffectSpecHandle SpecHandle = ASC->MakeOutgoingSpec(MeleeAttackEffect, 1.0f, ContextHandle);
        if (SpecHandle.IsValid())
        {
            SpecHandle.Data->SetSetByCallerMagnitude(RogueGameplayTags::SetByCaller_Damage, BaseDamage);

            UE_LOG(LogTemp, Warning,
                TEXT("[GA_MeleeAttack] Applying GameplayEffect: %s with SetByCaller damage=%.2f"),
                *MeleeAttackEffect->GetName(), BaseDamage);

            if (UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target))
            {
                FActiveGameplayEffectHandle EffectHandle = ASC->ApplyGameplayEffectSpecToTarget(*SpecHandle.Data.Get(), TargetASC);
                UE_LOG(LogTemp, Warning,
                    TEXT("[GA_MeleeAttack] GameplayEffect applied, Handle valid=%s"),
                    EffectHandle.IsValid() ? TEXT("YES") : TEXT("NO - EFFECT FAILED"));
            }
        }
        else
        {
            UE_LOG(LogTemp, Error,
                TEXT("[GA_MeleeAttack] Failed to create GameplayEffectSpec"));
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
