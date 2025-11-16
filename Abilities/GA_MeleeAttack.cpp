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

struct FTargetFacingInfo
{
    FVector Location = FVector::ZeroVector;
    bool bUsedReservedCell = false;
    FIntPoint ReservedCell = FIntPoint(-1, -1);
};

// CodeRevision: INC-2025-00030-R2 (Migrate to UGridPathfindingSubsystem) (2025-11-17 00:40)
static UGridPathfindingSubsystem* FindGridLibrary(UWorld* World)
{
    if (!World)
    {
        return nullptr;
    }

    return World->GetSubsystem<UGridPathfindingSubsystem>();
}

// CodeRevision: INC-2025-00030-R2 (Migrate to UGridPathfindingSubsystem) (2025-11-17 00:40)
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
            const FIntPoint ActualCell = GridLib->WorldToGrid(Result.Location);
            if (ActualCell == Result.ReservedCell)
            {
                Result.Location = GridLib->GridToWorld(Result.ReservedCell, Target->GetActorLocation().Z);
                Result.bUsedReservedCell = true;
            }
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
    BaseDamage = 28.0f;
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
    UWorld* World = GetWorld();
    UGridPathfindingSubsystem* GridLib = FindGridLibrary(World);
    const FTargetFacingInfo FacingInfo = ComputeTargetFacingInfo(TargetUnit, World, GridLib);
    UpdateCachedTargetLocation(FacingInfo.Location, FacingInfo.ReservedCell, GridLib);
    FVector TargetFacingLocation = FacingInfo.Location;

    if (TargetUnit && ActorInfo && ActorInfo->AvatarActor.IsValid())
    {
        AActor* Avatar = ActorInfo->AvatarActor.Get();
        if (Avatar && IsValid(TargetUnit))
        {
            FVector ToTarget = TargetFacingLocation - Avatar->GetActorLocation();
            ToTarget.Z = 0.0f;  // Ignore vertical axis

            if (!ToTarget.IsNearlyZero())
            {
            const FVector DirectionToTarget = ToTarget.GetSafeNormal();
            // CodeRevision: INC-2025-00022-R1 (Correct melee attack rotation) (2025-11-17 19:00)
            const FRotator NewRotation = DirectionToTarget.Rotation();
            Avatar->SetActorRotation(NewRotation);

                UE_LOG(LogTemp, Log, TEXT("[GA_MeleeAttack] %s: Rotated to face target %s (Yaw=%.1f)"),
                    *GetNameSafe(Avatar), *GetNameSafe(TargetUnit), NewRotation.Yaw);

                if (FacingInfo.bUsedReservedCell)
                {
                    UE_LOG(LogTemp, Log,
                        TEXT("[GA_MeleeAttack] %s: Using reserved cell %s for facing rotation"),
                        *GetNameSafe(Avatar), *FacingInfo.ReservedCell.ToString());
                }
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("[GA_MeleeAttack] %s: Cannot rotate - target is at same location"),
                    *GetNameSafe(Avatar));
            }
        }
    }
    else if (!TargetUnit)
    {
        UE_LOG(LogTemp, Warning, TEXT("[GA_MeleeAttack] %s: Cannot rotate - no target"),
            *GetNameSafe(GetAvatarActorFromActorInfo()));
    }

    UE_LOG(LogTemp, Error,
        TEXT("[GA_MeleeAttack] About to call PlayAttackMontage(), MontagePtr=%s"),
        MeleeAttackMontage ? *MeleeAttackMontage->GetName() : TEXT("NULL"));
    PlayAttackMontage();
    UE_LOG(LogTemp, Error,
        TEXT("[GA_MeleeAttack] ActivateAbility COMPLETED"));
}

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

    UE_LOG(LogTemp, Error, TEXT("[GA_MeleeAttack] Creating PlayMontageAndWait task..."));

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
    UE_LOG(LogTemp, Error,
        TEXT("[GA_MeleeAttack] ===== OnMontageCompleted CALLED ===== Actor=%s"),
        *GetNameSafe(GetAvatarActorFromActorInfo()));

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

    UE_LOG(LogTemp, Error, TEXT("[GA_MeleeAttack] %s: Montage completed, calling EndAbility immediately"),
        *GetNameSafe(GetAvatarActorFromActorInfo()));
    EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
    UE_LOG(LogTemp, Error, TEXT("[GA_MeleeAttack] EndAbility RETURNED"));
}

void UGA_MeleeAttack::OnMontageBlendOut()
{
    UE_LOG(LogTemp, Verbose, TEXT("[GA_MeleeAttack] %s: Montage blend out"),
        *GetNameSafe(GetAvatarActorFromActorInfo()));
}

void UGA_MeleeAttack::OnMontageCancelled()
{
    UE_LOG(LogTemp, Warning, TEXT("[GA_MeleeAttack] Montage cancelled"));
    EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}

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

    // CodeRevision: INC-2025-00030-R2 (Migrate to UGridPathfindingSubsystem) (2025-11-17 00:40)
    UWorld* World = GetWorld();
    UGridPathfindingSubsystem* GridLib = FindGridLibrary(World);
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

        UE_LOG(LogTemp, Warning,
            TEXT("[GA_MeleeAttack] Attacker=(%d,%d), Target=(%d,%d), ChebyshevDistance=%d, RangeInTiles=%d (InRange=%s)"),
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
            const float FinalDamage = (BaseDamage > 0.0f) ? BaseDamage : 28.0f;
            
            if (BaseDamage <= 0.0f)
            {
                UE_LOG(LogTemp, Error,
                    TEXT("[GA_MeleeAttack] WARNING: BaseDamage=%.2f is invalid! Using fallback damage=%.2f"),
                    BaseDamage, FinalDamage);
            }
            
            SpecHandle.Data->SetSetByCallerMagnitude(RogueGameplayTags::SetByCaller_Damage, FinalDamage);

            UE_LOG(LogTemp, Warning,
                TEXT("[GA_MeleeAttack] Applying GameplayEffect: %s with SetByCaller damage=%.2f (BaseDamage=%.2f)"),
                *MeleeAttackEffect->GetName(), FinalDamage, BaseDamage);

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

