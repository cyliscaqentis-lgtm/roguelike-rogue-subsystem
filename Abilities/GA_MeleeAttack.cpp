// GA_MeleeAttack.cpp
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
#include "Grid/GridOccupancySubsystem.h"
// CodeRevision: INC-2025-00030-R2 (Migrate to UGridPathfindingSubsystem) (2025-11-17 00:40)
#include "Grid/GridPathfindingSubsystem.h"
#include "EngineUtils.h"
#include "Math/RotationMatrix.h"
// Lyra standard damage pattern (CombatSet.BaseDamage attribute)
#include "AbilitySystem/Attributes/LyraCombatSet.h"

// CodeRevision: INC-2025-00033-R1
// (Align GA_MeleeAttack with GA_AttackBase barrier/completion flow and enforce tile range) (2025-11-18 11:00)

struct FTargetFacingInfo
{
    FVector Location = FVector::ZeroVector;
    bool bUsedReservedCell = false;
    FIntPoint ReservedCell = FIntPoint(-1, -1);
};

// CodeRevision: INC-2025-00032-R1 (Removed FindGridLibrary() helper - use GetSubsystem() directly) (2025-01-XX XX:XX)

// CodeRevision: INC-2025-00030-R2 (Migrate to UGridPathfindingSubsystem) (2025-11-17 00:40)
// CodeRevision: INC-2025-1122-FACING-R1 (Use target's reserved cell for facing calculation even during movement) (2025-11-22)
static FTargetFacingInfo ComputeTargetFacingInfo(AActor* Target, UWorld* World, UGridPathfindingSubsystem* GridLib)
{
    FTargetFacingInfo Result;
    if (!Target)
    {
        return Result;
    }

    Result.Location = Target->GetActorLocation();
    if (!GridLib || !World)
    {
        return Result;
    }

    if (UGridOccupancySubsystem* Occupancy = World->GetSubsystem<UGridOccupancySubsystem>())
    {
        Result.ReservedCell = Occupancy->GetReservedCellForActor(Target);
        if (Result.ReservedCell.X >= 0 && Result.ReservedCell.Y >= 0)
        {
            // Always use the reserved cell position for facing calculation.
            // This ensures attackers face the target's destination (where they will be)
            // rather than their current position (where they are during movement).
            Result.Location = GridLib->GridToWorld(Result.ReservedCell, Target->GetActorLocation().Z);
            Result.bUsedReservedCell = true;
        }
    }

    return Result;
}

UGA_MeleeAttack::UGA_MeleeAttack(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;
    ReplicationPolicy = EGameplayAbilityReplicationPolicy::ReplicateYes;

    MaxExecutionTime = 5.0f;
    Damage = 28.0f;  // GA_MeleeAttack独自のDamageプロパティを使用
    RangeInTiles = 1;

    FGameplayTagContainer Tags;
    Tags.AddTag(RogueGameplayTags::Ability_Attack);
    Tags.AddTag(RogueGameplayTags::Ability_Melee);
    Tags.AddTag(RogueGameplayTags::Ability_MeleeAttack);
    SetAssetTags(Tags);

    ActivationBlockedTags.AddTag(RogueGameplayTags::State_Ability_Executing);
}

void UGA_MeleeAttack::ActivateAbility(
    const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    const FGameplayEventData* TriggerEventData)
{
    UE_LOG(LogAttackAbility, Error,
        TEXT("[GA_MeleeAttack] ===== ActivateAbility CALLED ===== Actor=%s"),
        *GetNameSafe(GetAvatarActorFromActorInfo()));

    Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

    UE_LOG(LogAttackAbility, Error,
        TEXT("[GA_MeleeAttack] After Super::ActivateAbility, attempting CommitAbility"));

    if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
    {
        UE_LOG(LogAttackAbility, Error,
            TEXT("[GA_MeleeAttack] CommitAbility FAILED - Ending ability"));
        EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEndAbility=*/true, /*bWasCancelled=*/true);
        return;
    }

    UE_LOG(LogAttackAbility, Error,
        TEXT("[GA_MeleeAttack] CommitAbility SUCCESS - Proceeding with attack logic"));

    if (TriggerEventData && TriggerEventData->TargetData.IsValid(0))
    {
        const FGameplayAbilityTargetData* TargetData = TriggerEventData->TargetData.Get(0);
        if (const FGameplayAbilityTargetData_SingleTargetHit* SingleTarget =
            static_cast<const FGameplayAbilityTargetData_SingleTargetHit*>(TargetData))
        {
            if (AActor* Target = SingleTarget->HitResult.GetActor())
            {
                TargetUnit = Target;
                UE_LOG(LogAttackAbility, Log, TEXT("[GA_MeleeAttack] %s: Target from EventData: %s"),
                    *GetNameSafe(GetAvatarActorFromActorInfo()), *GetNameSafe(Target));
            }
        }
    }

    if (!TargetUnit)
    {
        TargetUnit = FindAdjacentTarget();
        if (TargetUnit)
        {
            UE_LOG(LogAttackAbility, Warning, TEXT("[GA_MeleeAttack] %s: Using fallback adjacent search, found: %s"),
                *GetNameSafe(GetAvatarActorFromActorInfo()), *GetNameSafe(TargetUnit));
        }
        else
        {
            UE_LOG(LogAttackAbility, Warning, TEXT("[GA_MeleeAttack] %s: No target found (neither EventData nor adjacent)"),
                *GetNameSafe(GetAvatarActorFromActorInfo()));
        }
    }

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

    // CodeRevision: INC-2025-00030-R2 (Migrate to UGridPathfindingSubsystem) (2025-11-17 00:40)
    // CodeRevision: INC-2025-00032-R1 (Removed FindGridLibrary() - use GetSubsystem() directly) (2025-01-XX XX:XX)
    UWorld* World = GetWorld();
    UGridPathfindingSubsystem* GridLib = World ? World->GetSubsystem<UGridPathfindingSubsystem>() : nullptr;
    const FTargetFacingInfo FacingInfo = ComputeTargetFacingInfo(TargetUnit, World, GridLib);
    UpdateCachedTargetLocation(FacingInfo.Location, FacingInfo.ReservedCell, GridLib);

    // CodeRevision: INC-2025-1147-R1 (Rotate melee attackers toward their target using cached front-cell location) (2025-11-20 15:10)
    if (ActorInfo && ActorInfo->AvatarActor.IsValid())
    {
        if (APawn* Pawn = Cast<APawn>(ActorInfo->AvatarActor.Get()))
        {
            const FVector ToTarget = FacingInfo.Location - Pawn->GetActorLocation();
            FVector FlatDirection = ToTarget;
            FlatDirection.Z = 0.0f;

            if (!FlatDirection.IsNearlyZero())
            {
                const FRotator NewRotation = FlatDirection.Rotation();
                Pawn->SetActorRotation(NewRotation);
            }
        }
    }

    UE_LOG(LogAttackAbility, Error,
        TEXT("[GA_MeleeAttack] About to call PlayAttackMontage(), MontagePtr=%s"),
        MeleeAttackMontage ? *MeleeAttackMontage->GetName() : TEXT("NULL"));
    PlayAttackMontage();
    UE_LOG(LogAttackAbility, Error,
        TEXT("[GA_MeleeAttack] ActivateAbility COMPLETED"));
}

void UGA_MeleeAttack::PlayAttackMontage_Implementation()
{
    UE_LOG(LogAttackAbility, Error,
        TEXT("[GA_MeleeAttack] PlayAttackMontage_Implementation CALLED, Montage=%s"),
        MeleeAttackMontage ? *MeleeAttackMontage->GetName() : TEXT("NULL"));

    if (!MeleeAttackMontage)
    {
        UE_LOG(LogAttackAbility, Error, TEXT("[GA_MeleeAttack] MeleeAttackMontage is NULL - calling OnMontageCompleted immediately"));
        OnMontageCompleted();
        return;
    }

    UE_LOG(LogAttackAbility, Error, TEXT("[GA_MeleeAttack] Creating PlayMontageAndWait task..."));

    UAbilityTask_PlayMontageAndWait* Task =
        UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
            this, FName("MeleeAttack"), MeleeAttackMontage, 1.0f);
    check(Task);
    Task->OnCompleted.AddDynamic(this, &UGA_MeleeAttack::OnMontageCompleted);
    Task->OnBlendOut.AddDynamic(this, &UGA_MeleeAttack::OnMontageBlendOut);
    Task->OnCancelled.AddDynamic(this, &UGA_MeleeAttack::OnMontageCancelled);
    Task->ReadyForActivation();
}

void UGA_MeleeAttack::OnMontageCompleted()
{
    UE_LOG(LogAttackAbility, Error,
        TEXT("[GA_MeleeAttack] ===== OnMontageCompleted CALLED ===== Actor=%s"),
        *GetNameSafe(GetAvatarActorFromActorInfo()));

    if (TargetUnit && IsValid(TargetUnit))
    {
        UE_LOG(LogAttackAbility, Error, TEXT("[GA_MeleeAttack] %s: Attacking stored target: %s"),
            *GetNameSafe(GetAvatarActorFromActorInfo()), *GetNameSafe(TargetUnit));
        ApplyDamageToTarget(TargetUnit);
    }
    else
    {
        UE_LOG(LogAttackAbility, Error, TEXT("[GA_MeleeAttack] %s: No valid target to attack (TargetUnit is null or invalid)"),
            *GetNameSafe(GetAvatarActorFromActorInfo()));
    }

    // CodeRevision: INC-2025-00033-R1
    // ここから先の「完了通知＋EndAbility」は GA_AttackBase / GA_TurnActionBase に一元化する。
    // OnAttackCompleted() はサーバー権限チェックを内包しており、サーバー側のみで
    // SendCompletionEvent + EndAbility を行う。
    UE_LOG(LogAttackAbility, Error,
        TEXT("[GA_MeleeAttack] %s: Montage completed, calling OnAttackCompleted()"),
        *GetNameSafe(GetAvatarActorFromActorInfo()));

    OnAttackCompleted();

    UE_LOG(LogAttackAbility, Error,
        TEXT("[GA_MeleeAttack] %s: OnAttackCompleted RETURNED"),
        *GetNameSafe(GetAvatarActorFromActorInfo()));
}

void UGA_MeleeAttack::OnMontageBlendOut()
{
    UE_LOG(LogAttackAbility, Verbose, TEXT("[GA_MeleeAttack] %s: Montage blend out"),
        *GetNameSafe(GetAvatarActorFromActorInfo()));
}

void UGA_MeleeAttack::OnMontageCancelled()
{
    UE_LOG(LogAttackAbility, Warning, TEXT("[GA_MeleeAttack] Montage cancelled"));
    EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}

// CodeRevision: INC-2025-1122-TARGET-R1 (Check player's reserved cell for target finding during simultaneous movement) (2025-11-22)
AActor* UGA_MeleeAttack::FindAdjacentTarget()
{
    AActor* Avatar = GetAvatarActorFromActorInfo();
    if (!Avatar)
    {
        return nullptr;
    }

    AUnitBase* Unit = Cast<AUnitBase>(Avatar);
    if (!Unit)
    {
        return nullptr;
    }

    // First try the standard adjacent search (player at current position)
    TArray<AUnitBase*> AdjacentPlayers = Unit->GetAdjacentPlayers();
    if (AdjacentPlayers.Num() > 0 && IsValid(AdjacentPlayers[0]))
    {
        return AdjacentPlayers[0];
    }

    // If not found, check if player's RESERVED cell is adjacent to us
    // This handles the case where player is moving but hasn't arrived yet
    UWorld* World = GetWorld();
    if (!World)
    {
        return nullptr;
    }

    UGridOccupancySubsystem* Occupancy = World->GetSubsystem<UGridOccupancySubsystem>();
    UGridPathfindingSubsystem* GridLib = World->GetSubsystem<UGridPathfindingSubsystem>();
    if (!Occupancy || !GridLib)
    {
        return nullptr;
    }

    // Get player pawn
    APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0);
    if (!PlayerPawn)
    {
        return nullptr;
    }

    // Check if player's reserved cell is adjacent to our current position
    const FIntPoint PlayerReservedCell = Occupancy->GetReservedCellForActor(PlayerPawn);
    if (PlayerReservedCell.X < 0 || PlayerReservedCell.Y < 0)
    {
        return nullptr; // No reservation, player not moving
    }

    const FIntPoint MyCell = GridLib->WorldToGrid(Avatar->GetActorLocation());
    const int32 ChebyshevDist = FMath::Max(
        FMath::Abs(MyCell.X - PlayerReservedCell.X),
        FMath::Abs(MyCell.Y - PlayerReservedCell.Y));

    if (ChebyshevDist <= 1)
    {
        UE_LOG(LogAttackAbility, Log, TEXT("[GA_MeleeAttack] %s: Found player via reserved cell (%d,%d), MyCell=(%d,%d), Dist=%d"),
            *GetNameSafe(Avatar), PlayerReservedCell.X, PlayerReservedCell.Y, MyCell.X, MyCell.Y, ChebyshevDist);
        return PlayerPawn;
    }

    return nullptr;
}

//------------------------------------------------------------------------------
// ダメージ適用
//------------------------------------------------------------------------------

void UGA_MeleeAttack::ApplyDamageToTarget(AActor* Target)
{
    // サーバー権限チェック - クライアントでは実行しない
    if (!HasAuthority(&CurrentActivationInfo))
    {
        UE_LOG(LogAttackAbility, Warning, TEXT("[GA_MeleeAttack] ApplyDamageToTarget called on client - skipping"));
        return;
    }

    if (!Target || !MeleeAttackEffect)
    {
        UE_LOG(LogAttackAbility, Warning, TEXT("[GA_MeleeAttack] Invalid Target or Effect"));
        return;
    }

    AActor* Avatar = GetAvatarActorFromActorInfo();
    if (!Avatar)
    {
        UE_LOG(LogAttackAbility, Warning, TEXT("[GA_MeleeAttack] Avatar is null"));
        return;
    }

    UE_LOG(LogAttackAbility, Warning,
        TEXT("[GA_MeleeAttack] ==== DAMAGE DIAGNOSTIC START ===="));
    UE_LOG(LogAttackAbility, Warning,
        TEXT("[GA_MeleeAttack] Attacker=%s, Target=%s, Damage=%.2f"),
        *Avatar->GetName(), *Target->GetName(), Damage);

    // Team check diagnostic
    if (AUnitBase* AttackerUnit = Cast<AUnitBase>(Avatar))
    {
        if (AUnitBase* TargetUnitRef = Cast<AUnitBase>(Target))
        {
            UE_LOG(LogAttackAbility, Warning,
                TEXT("[GA_MeleeAttack] AttackerTeam=%d, TargetTeam=%d (SameTeam=%s)"),
                AttackerUnit->Team, TargetUnitRef->Team,
                AttackerUnit->Team == TargetUnitRef->Team ? TEXT("YES - DAMAGE BLOCKED") : TEXT("NO - OK"));
        }
    }

    // CodeRevision: INC-2025-00030-R2 (Migrate to UGridPathfindingSubsystem) (2025-11-17 00:40)
    // CodeRevision: INC-2025-00032-R1 (Removed FindGridLibrary() - use GetSubsystem() directly) (2025-01-XX XX:XX)
    UWorld* World = GetWorld();
    UGridPathfindingSubsystem* GridLib = World ? World->GetSubsystem<UGridPathfindingSubsystem>() : nullptr;
    const FTargetFacingInfo FacingInfo = ComputeTargetFacingInfo(Target, World, GridLib);
    FVector TargetFacingLocation = FacingInfo.Location;
    int32 DistanceInTiles = -1;

    if (GridLib)
    {
        FIntPoint AttackerGrid = GridLib->WorldToGrid(Avatar->GetActorLocation());
        FIntPoint TargetGrid = bHasCachedTargetCell ? CachedTargetCell : GridLib->WorldToGrid(TargetFacingLocation);

        DistanceInTiles = FMath::Max(
            FMath::Abs(AttackerGrid.X - TargetGrid.X),
            FMath::Abs(AttackerGrid.Y - TargetGrid.Y));

        UE_LOG(LogAttackAbility, Warning,
            TEXT("[GA_MeleeAttack] Attacker=(%d,%d), Target=(%d,%d), ChebyshevDistance=%d, RangeInTiles=%d (InRange=%s)"),
            AttackerGrid.X, AttackerGrid.Y, TargetGrid.X, TargetGrid.Y,
            DistanceInTiles, RangeInTiles,
            DistanceInTiles <= RangeInTiles ? TEXT("YES") : TEXT("NO - TOO FAR"));

        // CodeRevision: INC-2025-00033-R1
        // タイル距離が射程を超えている場合はダメージをスキップし、「攻撃モーションだけ発生したミス」として扱う。
        if (DistanceInTiles > RangeInTiles)
        {
            UE_LOG(LogAttackAbility, Warning,
                TEXT("[GA_MeleeAttack] Target is OUT OF RANGE (DistanceInTiles=%d, RangeInTiles=%d) - skipping damage"),
                DistanceInTiles, RangeInTiles);
            UE_LOG(LogAttackAbility, Warning,
                TEXT("[GA_MeleeAttack] ==== DAMAGE DIAGNOSTIC END (OUT OF RANGE) ===="));
            return;
        }
    }
    else
    {
        UE_LOG(LogAttackAbility, Error,
            TEXT("[GA_MeleeAttack] GridPathfindingLibrary not found - cannot calculate tile distance! (Damage will still be applied as fallback)"));
    }

    // ASC and Tags diagnostic
    if (UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target))
    {
        FGameplayTagContainer TargetTags;
        TargetASC->GetOwnedGameplayTags(TargetTags);
        UE_LOG(LogAttackAbility, Warning,
            TEXT("[GA_MeleeAttack] TargetTags=%s"),
            *TargetTags.ToStringSimple());
    }
    else
    {
        UE_LOG(LogAttackAbility, Error,
            TEXT("[GA_MeleeAttack] Target has NO AbilitySystemComponent - cannot apply damage!"));
    }

    UE_LOG(LogAttackAbility, Warning,
        TEXT("[GA_MeleeAttack] ==== DAMAGE DIAGNOSTIC END ===="));

    if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
    {
        const float FinalDamage = (Damage > 0.0f) ? Damage : 28.0f;

        if (Damage <= 0.0f)
        {
            UE_LOG(LogAttackAbility, Error,
                TEXT("[GA_MeleeAttack] WARNING: Damage=%.2f is invalid! Using fallback damage=%.2f"),
                Damage, FinalDamage);
        }

        // ===== Lyra標準パターン: CombatSet.BaseDamage属性を設定 =====
        // LyraDamageExecutionはこの属性をキャプチャして使用します
        const ULyraCombatSet* CombatSet = ASC->GetSet<ULyraCombatSet>();
        float OldBaseDamage = 0.0f;
        if (CombatSet)
        {
            OldBaseDamage = CombatSet->GetBaseDamage();
            ASC->SetNumericAttributeBase(ULyraCombatSet::GetBaseDamageAttribute(), FinalDamage);
            UE_LOG(LogAttackAbility, Warning,
                TEXT("[GA_MeleeAttack] Set attacker CombatSet.BaseDamage = %.2f (was %.2f)"),
                FinalDamage, OldBaseDamage);
        }
        else
        {
            UE_LOG(LogAttackAbility, Error,
                TEXT("[GA_MeleeAttack] WARNING: Attacker has no CombatSet! Damage may fail."));
        }

        FGameplayEffectContextHandle ContextHandle = ASC->MakeEffectContext();
        ContextHandle.AddSourceObject(this);

        FGameplayEffectSpecHandle SpecHandle = ASC->MakeOutgoingSpec(MeleeAttackEffect, 1.0f, ContextHandle);
        if (SpecHandle.IsValid())
        {
            UE_LOG(LogAttackAbility, Warning,
                TEXT("[GA_MeleeAttack] Applying GameplayEffect: %s (LyraDamageExecution will capture BaseDamage=%.2f)"),
                *MeleeAttackEffect->GetName(), FinalDamage);

            if (UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target))
            {
                FActiveGameplayEffectHandle EffectHandle = ASC->ApplyGameplayEffectSpecToTarget(*SpecHandle.Data.Get(), TargetASC);
                // Instant エフェクトの場合、ハンドルは無効だが正常に適用される
                if (EffectHandle.IsValid())
                {
                    UE_LOG(LogAttackAbility, Log,
                        TEXT("[GA_MeleeAttack] GameplayEffect applied successfully (Duration effect, handle is valid)"));
                }
                else
                {
                    UE_LOG(LogAttackAbility, Log,
                        TEXT("[GA_MeleeAttack] GameplayEffect applied (Instant effect, handle is invalid but this is normal)"));
                }
            }
        }
        else
        {
            UE_LOG(LogAttackAbility, Error,
                TEXT("[GA_MeleeAttack] Failed to create GameplayEffectSpec"));
        }

        // BaseDamageをリセット（次回の攻撃に影響しないように）
        if (CombatSet)
        {
            ASC->SetNumericAttributeBase(ULyraCombatSet::GetBaseDamageAttribute(), OldBaseDamage);
            UE_LOG(LogAttackAbility, Verbose,
                TEXT("[GA_MeleeAttack] Reset attacker CombatSet.BaseDamage to %.2f"),
                OldBaseDamage);
        }
    }
}

//------------------------------------------------------------------------------
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

    CachedTargetLocation = FVector::ZeroVector;
    CachedTargetCell = FIntPoint(-1, -1);
    bHasCachedTargetCell = false;
}

// CodeRevision: INC-2025-00030-R2 (Migrate to UGridPathfindingSubsystem) (2025-11-17 00:40)
void UGA_MeleeAttack::UpdateCachedTargetLocation(const FVector& Location, const FIntPoint& ReservedCell, const UGridPathfindingSubsystem* GridLib)
{
    CachedTargetLocation = Location;
    bHasCachedTargetCell = false;

    if (ReservedCell.X >= 0 && ReservedCell.Y >= 0)
    {
        CachedTargetCell = ReservedCell;
        bHasCachedTargetCell = true;
        return;
    }

    if (GridLib)
    {
        CachedTargetCell = GridLib->WorldToGrid(Location);
        bHasCachedTargetCell = true;
    }
}

AGameTurnManagerBase* UGA_MeleeAttack::GetTurnManager() const
{
    if (!CachedTurnManager.IsValid())
    {
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
