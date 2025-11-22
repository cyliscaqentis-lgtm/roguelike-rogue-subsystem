// Copyright Epic Games, Inc. All Rights Reserved.

// TurnCorePhaseManager.cpp
#include "TurnCorePhaseManager.h"
#include "DistanceFieldSubsystem.h"
#include "ConflictResolverSubsystem.h"
#include "StableActorRegistry.h"
#include "Turn/GameTurnManagerBase.h"
#include "Turn/MoveReservationSubsystem.h"
#include "Turn/TurnFlowCoordinator.h"
#include "TurnSystemTypes.h"
#include "../Grid/GridOccupancySubsystem.h"
#include "../Utility/GridUtils.h"
#include "../Utility/TurnCommandEncoding.h"
#include "UObject/UnrealType.h"
#include "../AI/Enemy/EnemyThinkerBase.h"
#include "GameplayTagsManager.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemGlobals.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Controller.h"
#include "Character/LyraCharacter.h"
#include "Character/LyraPawnExtensionComponent.h"
#include "AbilitySystem/LyraAbilitySystemComponent.h"
#include "../Utility/RogueGameplayTags.h"
#include "EngineUtils.h"

DEFINE_LOG_CATEGORY(LogTurnCore);

namespace TurnCorePhaseManagerPrivate
{
    static FGameplayTag MapIntentToGameplayEvent(const FGameplayTag& IntentTag)
    {
        const FGameplayTag MoveIntent = RogueGameplayTags::AI_Intent_Move;
        const FGameplayTag AttackIntent = RogueGameplayTags::AI_Intent_Attack;
        const FGameplayTag WaitIntent = RogueGameplayTags::AI_Intent_Wait;

        if (IntentTag.MatchesTag(MoveIntent))
        {
            return RogueGameplayTags::GameplayEvent_Intent_Move;
        }
        if (IntentTag.MatchesTag(AttackIntent))
        {
            return RogueGameplayTags::GameplayEvent_Intent_Attack;
        }
        if (IntentTag.MatchesTag(WaitIntent))
        {
            return RogueGameplayTags::GameplayEvent_Intent_Wait;
        }

        // For custom intents, use the tag as-is so the ability layer can handle it.
        return IntentTag;
    }
}

static int32 ReadGenerationOrderFromBlueprint(const AActor* Actor)
{
    static const FName GenName(TEXT("GenerationOrder"));

    if (!Actor)
    {
        return TNumericLimits<int32>::Max();
    }

    if (const FIntProperty* Prop = FindFProperty<FIntProperty>(Actor->GetClass(), GenName))
    {
        if (const int32* ValuePtr = Prop->ContainerPtrToValuePtr<int32>(Actor))
        {
            const int32 Order = *ValuePtr;
            return Order >= 0 ? Order : TNumericLimits<int32>::Max();
        }
    }

    return TNumericLimits<int32>::Max();
}

void UTurnCorePhaseManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // Sanity check: core intent tags must exist in DefaultGameplayTags.ini
    static const FName RequiredTags[] = {
        FName("AI.Intent.Move"),
        FName("AI.Intent.Attack"),
        FName("AI.Intent.Wait"),
        FName("AI.Intent.Dash")
    };

    for (const FName& TagName : RequiredTags)
    {
        const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TagName, false);
        if (!ensureMsgf(Tag.IsValid(), TEXT("[TurnCore] Missing required GameplayTag: %s"), *TagName.ToString()))
        {
            UE_LOG(LogTurnCore, Error, TEXT("[TurnCore] Please add tag '%s' to DefaultGameplayTags.ini"), *TagName.ToString());
        }
    }

    ConflictResolver = GetWorld()->GetSubsystem<UConflictResolverSubsystem>();
    DistanceField    = GetWorld()->GetSubsystem<UDistanceFieldSubsystem>();
    ActorRegistry    = GetWorld()->GetSubsystem<UStableActorRegistry>();

    UE_LOG(LogTurnCore, Log, TEXT("[TurnCore] Initialized"));
}

void UTurnCorePhaseManager::Deinitialize()
{
    UE_LOG(LogTurnCore, Log, TEXT("[TurnCore] Deinitialized"));
    Super::Deinitialize();
}

// ============================================================================
// Core Phase Pipeline
// ============================================================================

void UTurnCorePhaseManager::CoreObservationPhase(const FIntPoint& PlayerCell)
{
    UE_LOG(LogTurnCore, Warning,
        TEXT("[TurnCore] CoreObservationPhase received PlayerCell=(%d, %d)"),
        PlayerCell.X, PlayerCell.Y);

    if (!DistanceField)
    {
        UE_LOG(LogTurnCore, Error, TEXT("[TurnCore] DistanceField is null"));
        return;
    }

    if (UWorld* World = GetWorld())
    {
        if (UGridOccupancySubsystem* GridOccupancy = World->GetSubsystem<UGridOccupancySubsystem>())
        {
            // CodeRevision: INC-2025-00030-R1 (Use TurnFlowCoordinator instead of GetCurrentTurnIndex) (2025-11-16 00:00)
            if (UTurnFlowCoordinator* TFC = World->GetSubsystem<UTurnFlowCoordinator>())
            {
                const int32 CurrentTurnId = TFC->GetCurrentTurnId();
                GridOccupancy->SetCurrentTurnId(CurrentTurnId);
                GridOccupancy->PurgeOutdatedReservations(CurrentTurnId);

                UE_LOG(LogTurnCore, Log,
                    TEXT("[TurnCore] ObservationPhase: Purged outdated reservations for turn %d"),
                    CurrentTurnId);
            }

            // Ensure there are no stacked actors before any AI reasoning.
            GridOccupancy->EnforceUniqueOccupancy();
        }
    }

    // CodeRevision: INC-2025-1122-PERF-R5 (Optimize DistanceField update with dynamic margin)
    int32 Margin = 100; // Default fallback
    TSet<FIntPoint> EnemyPositions;

    if (UUnitTurnStateSubsystem* UnitState = World->GetSubsystem<UUnitTurnStateSubsystem>())
    {
        TArray<AActor*> Enemies;
        UnitState->CopyEnemiesTo(Enemies);

        if (UGridPathfindingSubsystem* PathFinder = World->GetSubsystem<UGridPathfindingSubsystem>())
        {
            int32 MaxDist = 0;
            for (AActor* Enemy : Enemies)
            {
                if (IsValid(Enemy))
                {
                    FIntPoint GridPos = PathFinder->WorldToGrid(Enemy->GetActorLocation());
                    EnemyPositions.Add(GridPos);
                    
                    int32 Dist = FMath::Abs(GridPos.X - PlayerCell.X) + FMath::Abs(GridPos.Y - PlayerCell.Y);
                    if (Dist > MaxDist)
                    {
                        MaxDist = Dist;
                    }
                }
            }
            // Dynamic margin: MaxDist + buffer, clamped between 15 and 100
            Margin = FMath::Clamp(MaxDist + 10, 15, 100);
        }
    }

    UE_LOG(LogTurnCore, Log, TEXT("[TurnCore] ObservationPhase: Updating DistanceField with Margin=%d (Enemies=%d)"), Margin, EnemyPositions.Num());
    DistanceField->UpdateDistanceFieldOptimized(PlayerCell, EnemyPositions, Margin);
    UE_LOG(LogTurnCore, Log, TEXT("[TurnCore] ObservationPhase: Complete"));
}

TArray<FEnemyIntent> UTurnCorePhaseManager::CoreThinkPhase(const TArray<AActor*>& Enemies)
{
    TArray<FEnemyIntent> Intents;
    Intents.Reserve(Enemies.Num());

    for (AActor* Enemy : Enemies)
    {
        if (!Enemy)
        {
            continue;
        }

        UEnemyThinkerBase* Thinker = Enemy->FindComponentByClass<UEnemyThinkerBase>();
        if (!Thinker)
        {
            UE_LOG(LogTurnCore, Warning,
                TEXT("[TurnCore] Enemy %s has no EnemyThinkerBase component"),
                *GetNameSafe(Enemy));
            continue;
        }

        FEnemyIntent Intent = Thinker->DecideIntent();
        Intents.Add(Intent);
    }

    UE_LOG(LogTurnCore, Log, TEXT("[TurnCore] ThinkPhase: Collected %d intents"), Intents.Num());
    return Intents;
}

TArray<FResolvedAction> UTurnCorePhaseManager::CoreResolvePhase(const TArray<FEnemyIntent>& Intents)
{
    UConflictResolverSubsystem* ConflictResolverPtr = ConflictResolver.Get();
    UStableActorRegistry* ActorRegistryPtr = ActorRegistry.Get();
    UDistanceFieldSubsystem* DistanceFieldPtr = DistanceField.Get();

    if (!ConflictResolverPtr || !ActorRegistryPtr || !DistanceFieldPtr)
    {
        UE_LOG(LogTurnCore, Error,
            TEXT("[TurnCore] CoreResolvePhase: Required subsystems are null (ConflictResolver=%p, ActorRegistry=%p, DistanceField=%p)"),
            ConflictResolverPtr, ActorRegistryPtr, DistanceFieldPtr);
        return TArray<FResolvedAction>();
    }

    UGridOccupancySubsystem* GridOccupancy = GetWorld()->GetSubsystem<UGridOccupancySubsystem>();

    if (GridOccupancy)
    {
        // CodeRevision: INC-2025-00030-R1 (Use TurnFlowCoordinator instead of GetCurrentTurnIndex) (2025-11-16 00:00)
        int32 CurrentTurnId = 0;
        if (UWorld* World = GetWorld())
        {
            if (UTurnFlowCoordinator* TFC = World->GetSubsystem<UTurnFlowCoordinator>())
            {
                CurrentTurnId = TFC->GetCurrentTurnId();
            }
        }

        GridOccupancy->SetCurrentTurnId(CurrentTurnId);
        GridOccupancy->PurgeOutdatedReservations(CurrentTurnId);
    }

    if (GridOccupancy)
    {
        // Begin a new move phase: resets two-phase commit tracking for this slot.
        GridOccupancy->BeginMovePhase();
    }

    auto GetLiveCell = [&](AActor* A) -> FIntPoint
    {
        if (GridOccupancy)
        {
            const FIntPoint Cell = GridOccupancy->GetCellOfActor(A);
            if (Cell != FIntPoint::ZeroValue)
            {
                return Cell;
            }
        }
        return FIntPoint::ZeroValue;
    };

    const FGameplayTag MoveTag = RogueGameplayTags::AI_Intent_Move;
    const FGameplayTag WaitTag = RogueGameplayTags::AI_Intent_Wait;
    const FGameplayTag AttackTag = RogueGameplayTags::AI_Intent_Attack;

    ConflictResolverPtr->ClearReservations();

    for (const FEnemyIntent& Intent : Intents)
    {
        if (!Intent.Actor.IsValid())
        {
            continue;
        }

        AActor* Actor = Intent.Actor.Get();
        if (!IsValid(Actor) || Actor->IsActorBeingDestroyed())
        {
            UE_LOG(LogTurnCore, Log,
                TEXT("[TurnCore] CoreResolvePhase: Actor %s is dead/destroyed, skipping"),
                *GetNameSafe(Actor));
            continue;
        }

        const FStableActorID ActorID = ActorRegistryPtr->GetStableID(Actor);

        // Prefer the live grid cell from occupancy; fall back to intent's cached cell.
        FIntPoint LiveCurrentCell = GetLiveCell(Actor);
        if (LiveCurrentCell == FIntPoint::ZeroValue)
        {
            LiveCurrentCell = Intent.CurrentCell;
        }

        if (LiveCurrentCell != FIntPoint::ZeroValue && LiveCurrentCell != Intent.CurrentCell)
        {
            const int32 DesyncDistance = FGridUtils::ChebyshevDistance(LiveCurrentCell, Intent.CurrentCell);
            UE_LOG(LogTurnCore, Warning,
                TEXT("[TurnCore] Intent/Live desync: %s Live=(%d,%d) Intent=(%d,%d) Dist=%d"),
                *GetNameSafe(Intent.Actor.Get()),
                LiveCurrentCell.X, LiveCurrentCell.Y,
                Intent.CurrentCell.X, Intent.CurrentCell.Y,
                DesyncDistance);
        }

        FGameplayTag ResolvedAbilityTag = Intent.AbilityTag;

        // Attack intents are treated as stationary for conflict resolution:
        // they reserve their current cell and never change position during the move phase.
        FIntPoint ResolvedNextCell = Intent.NextCell;
        if (Intent.AbilityTag.MatchesTag(AttackTag))
        {
            ResolvedNextCell = LiveCurrentCell;
            UE_LOG(LogTurnCore, Verbose,
                TEXT("[TurnCore] Attack intent: %s stays at (%d,%d) (original NextCell=(%d,%d))"),
                *GetNameSafe(Actor),
                LiveCurrentCell.X, LiveCurrentCell.Y,
                Intent.NextCell.X, Intent.NextCell.Y);
        }

        const bool bIsMoveIntent = Intent.AbilityTag.MatchesTag(MoveTag);
        if (bIsMoveIntent && LiveCurrentCell != FIntPoint::ZeroValue)
        {
            const int32 MoveDistance = FGridUtils::ChebyshevDistance(LiveCurrentCell, ResolvedNextCell);
            if (MoveDistance > 1)
            {
                // CodeRevision: INC-2025-1132-R1 (Prevent teleporting move intents by gating Chebyshev distance) (2025-11-19 00:57)
                UE_LOG(LogTurnCore, Warning,
                    TEXT("[TurnCore] Anti-teleport guard: %s Live=(%d,%d) IntentCurrent=(%d,%d) IntentNext=(%d,%d) Dist=%d -> forcing WAIT"),
                    *GetNameSafe(Actor),
                    LiveCurrentCell.X, LiveCurrentCell.Y,
                    Intent.CurrentCell.X, Intent.CurrentCell.Y,
                    Intent.NextCell.X, Intent.NextCell.Y,
                    MoveDistance);
                ResolvedNextCell = LiveCurrentCell;
                ResolvedAbilityTag = WaitTag;
            }
        }

        const int32 CurrentDist = DistanceFieldPtr->GetDistance(LiveCurrentCell);
        const int32 NextDist    = DistanceFieldPtr->GetDistance(ResolvedNextCell);
        const int32 DistanceReduction = FMath::Max(0, CurrentDist - NextDist);

        FReservationEntry Entry;
        Entry.Actor            = Actor;
        Entry.ActorID          = ActorID;
        Entry.TimeSlot         = Intent.TimeSlot;
        Entry.CurrentCell      = LiveCurrentCell;
        Entry.Cell             = ResolvedNextCell;
        Entry.AbilityTag       = ResolvedAbilityTag;
        Entry.ActionTier       = 1;    // TODO: use GetActionTier if we introduce tiered abilities
        Entry.BasePriority     = 100;
        Entry.DistanceReduction= DistanceReduction;
        Entry.GenerationOrder  = ReadGenerationOrderFromBlueprint(Actor);

        ConflictResolverPtr->AddReservation(Entry);
    }

    // Run the conflict resolver to convert reservations into resolved actions.
    TArray<FResolvedAction> Resolved = ConflictResolverPtr->ResolveAllConflicts();

    // Build a stable hash for debugging determinism across runs.
    uint32 TurnHash = 2166136261u;
    auto Mix = [&](uint32 Value)
    {
        TurnHash ^= Value;
        TurnHash *= 16777619u;
    };

    for (const FResolvedAction& Action : Resolved)
    {
        Mix(static_cast<uint32>(Action.GenerationOrder));
        Mix(GetTypeHash(Action.ActorID.PersistentGUID.A));
        Mix(GetTypeHash(Action.NextCell.X));
        Mix(GetTypeHash(Action.NextCell.Y));
        Mix(static_cast<uint32>(Action.AllowedDashSteps));
        Mix(GetTypeHash(Action.FinalAbilityTag));
        Mix(static_cast<uint32>(Action.TimeSlot));
    }

    UE_LOG(LogTurnCore, Log,
        TEXT("[TurnCore] ResolvePhase: Resolved %d actions, Hash=0x%08X"),
        Resolved.Num(), TurnHash);

    // Only true movement actions should reserve destination cells with the MoveReservation subsystem.
    UMoveReservationSubsystem* MoveRes = nullptr;
    if (UWorld* World = GetWorld())
    {
        MoveRes = World->GetSubsystem<UMoveReservationSubsystem>();
    }

    if (MoveRes)
    {
        for (FResolvedAction& Action : Resolved)
        {
            const bool bIsMoveLikeAction = Action.FinalAbilityTag.MatchesTag(MoveTag);

            if (bIsMoveLikeAction && !Action.bIsWait)
            {
                const bool bReserved = MoveRes->RegisterResolvedMove(Action.SourceActor.Get(), Action.NextCell);
                if (!bReserved)
                {
                    UE_LOG(LogTurnCore, Error,
                        TEXT("[TurnCore] RegisterResolvedMove FAILED for %s -> (%d,%d) - marking as WAIT (no movement)"),
                        *GetNameSafe(Action.SourceActor.Get()), Action.NextCell.X, Action.NextCell.Y);

                    Action.bIsWait = true;
                    Action.ResolutionReason = TEXT("Move reservation failed - converted to wait");
                }
            }
            else
            {
                UE_LOG(LogTurnCore, Log,
                    TEXT("[TurnCore] Skip RegisterResolvedMove for %s (bIsWait=%d, FinalTag=%s, Reason=%s)"),
                    *GetNameSafe(Action.SourceActor.Get()),
                    Action.bIsWait ? 1 : 0,
                    *Action.FinalAbilityTag.ToString(),
                    *Action.ResolutionReason);
            }
        }
    }
    else
    {
        UE_LOG(LogTurnCore, Error, TEXT("[TurnCore] MoveReservationSubsystem not found - skipping move registration"));
    }

    return Resolved;
}

// ============================================================================
// Execute Phase
// ============================================================================

void UTurnCorePhaseManager::CoreExecutePhase(const TArray<FResolvedAction>& ResolvedActions)
{
    AGameTurnManagerBase* TurnManager = ResolveTurnManager();
    UMoveReservationSubsystem* MoveResSubsystem = nullptr;
    if (UWorld* World = GetWorld())
    {
        MoveResSubsystem = World->GetSubsystem<UMoveReservationSubsystem>();
    }
    const bool bCanDispatchViaSubsystem = (TurnManager && MoveResSubsystem);

    if (!MoveResSubsystem)
    {
        UE_LOG(LogTurnCore, Error,
            TEXT("[Execute] MoveReservationSubsystem missing - falling back to direct ASC dispatch"));
    }

    for (const FResolvedAction& Action : ResolvedActions)
    {
        if (!Action.SourceActor)
        {
            UE_LOG(LogTurnCore, Error, TEXT("[Execute] Skip: SourceActor is None"));
            continue;
        }

        if (Action.bIsWait)
        {
            // Even wait actions are forwarded so GAS can react (e.g., play idle or "blocked" animations).
            UE_LOG(LogTurnCore, Verbose,
                TEXT("[Execute] Notice: Actor=%s marked as bIsWait=true, still dispatching"),
                *GetNameSafe(Action.SourceActor.Get()));
        }

        bool bHandledByMoveSubsystem = false;
        if (bCanDispatchViaSubsystem)
        {
            bHandledByMoveSubsystem = MoveResSubsystem->DispatchResolvedMove(Action, TurnManager);
        }

        if (bHandledByMoveSubsystem)
        {
            continue;
        }

        UAbilitySystemComponent* ASC = ResolveASC(Action.SourceActor);
        if (!ASC)
        {
            UE_LOG(LogTurnCore, Error,
                TEXT("[Execute] ASC not found for %s"),
                *GetNameSafe(Action.SourceActor));
            continue;
        }

        // Build the gameplay event and send it into GAS.
        FGameplayEventData EventData;
        const FGameplayTag EventTag =
            TurnCorePhaseManagerPrivate::MapIntentToGameplayEvent(Action.FinalAbilityTag);

        EventData.EventTag   = EventTag;
        EventData.Instigator = Action.SourceActor;
        EventData.Target     = Action.SourceActor;

        if (Action.NextCell != FIntPoint(-1, -1))
        {
            const int32 EncodedCell =
                TurnCommandEncoding::PackCell(Action.NextCell.X, Action.NextCell.Y);
            EventData.EventMagnitude = static_cast<float>(EncodedCell);

            UE_LOG(LogTurnCore, Log,
                TEXT("[Execute] Encoded NextCell: (%d,%d) -> Magnitude=%.0f"),
                Action.NextCell.X, Action.NextCell.Y, EventData.EventMagnitude);
        }

        UE_LOG(LogTurnCore, Log,
            TEXT("[Execute] Sending ability event: Intent=%s, Event=%s, Actor=%s, Cell=(%d,%d)"),
            *Action.FinalAbilityTag.ToString(),
            *EventTag.ToString(),
            *GetNameSafe(Action.SourceActor),
            Action.NextCell.X, Action.NextCell.Y);

        // Log owned tags before dispatch; helps diagnose blocking tags.
        FGameplayTagContainer OwnedTags;
        ASC->GetOwnedGameplayTags(OwnedTags);
        UE_LOG(LogTurnCore, Log,
            TEXT("[Execute] DEBUG: Owned Tags BEFORE HandleGameplayEvent: %s"),
            *OwnedTags.ToStringSimple());

        const int32 NumActivated = ASC->HandleGameplayEvent(EventTag, &EventData);

        UE_LOG(LogTurnCore, Log,
            TEXT("[Execute] Ability activation result: NumActivated=%d"),
            NumActivated);
    }

    if (UGridOccupancySubsystem* GridOccupancy = GetWorld()->GetSubsystem<UGridOccupancySubsystem>())
    {
        // End of the move phase for this slot; clears two-phase commit tracking.
        GridOccupancy->EndMovePhase();
    }
}

// ============================================================================
// Cleanup Phase
// ============================================================================

void UTurnCorePhaseManager::CoreCleanupPhase()
{
    if (ConflictResolver)
    {
        ConflictResolver->ClearReservations();
    }

    UE_LOG(LogTurnCore, Log, TEXT("[TurnCore] CleanupPhase: Complete"));
}

// ============================================================================
// Gameplay Tag Accessors
// ============================================================================

const FGameplayTag& UTurnCorePhaseManager::Tag_Move()
{
    static const FGameplayTag Tag = RogueGameplayTags::AI_Intent_Move;
    return Tag;
}

const FGameplayTag& UTurnCorePhaseManager::Tag_Attack()
{
    static const FGameplayTag Tag = RogueGameplayTags::AI_Intent_Attack;
    return Tag;
}

const FGameplayTag& UTurnCorePhaseManager::Tag_Wait()
{
    static const FGameplayTag Tag = RogueGameplayTags::AI_Intent_Wait;
    return Tag;
}

// ============================================================================
// Turn Manager Resolution
// ============================================================================

AGameTurnManagerBase* UTurnCorePhaseManager::ResolveTurnManager()
{
    if (CachedTurnManager.IsValid())
    {
        return CachedTurnManager.Get();
    }

    if (UWorld* World = GetWorld())
    {
        for (TActorIterator<AGameTurnManagerBase> It(World); It; ++It)
        {
            CachedTurnManager = *It;
            return CachedTurnManager.Get();
        }
    }

    return nullptr;
}

// ============================================================================
// Intent Bucketing by Time Slot
// ============================================================================

void UTurnCorePhaseManager::BucketizeIntentsBySlot(
    const TArray<FEnemyIntent>& All,
    TArray<TArray<FEnemyIntent>>& OutBuckets,
    int32& OutMaxSlot)
{
    OutMaxSlot = 0;
    OutBuckets.Reset();

    for (const FEnemyIntent& Intent : All)
    {
        if (Intent.TimeSlot > OutMaxSlot)
        {
            OutMaxSlot = Intent.TimeSlot;
        }
    }

    OutBuckets.SetNum(OutMaxSlot + 1);

    for (const FEnemyIntent& Intent : All)
    {
        OutBuckets[Intent.TimeSlot].Add(Intent);
    }
}

// ============================================================================
// Move Phase with Time Slots
// ============================================================================

int32 UTurnCorePhaseManager::ExecuteMovePhaseWithSlots(
    const TArray<FEnemyIntent>& AllIntents,
    TArray<FResolvedAction>& OutActions)
{
    check(GetWorld() && GetWorld()->GetNetMode() != NM_Client);

    OutActions.Reset();
    OutActions.Reserve(AllIntents.Num());

    if (AllIntents.Num() == 0)
    {
        UE_LOG(LogTurnCore, Log, TEXT("[ExecuteMovePhaseWithSlots] No intents to process"));
        return 0;
    }

    TArray<TArray<FEnemyIntent>> Buckets;
    int32 MaxSlot = 0;
    BucketizeIntentsBySlot(AllIntents, Buckets, MaxSlot);

    UE_LOG(LogTurnCore, Log,
        TEXT("[ExecuteMovePhaseWithSlots] MaxTimeSlot = %d, Total Intents = %d"),
        MaxSlot, AllIntents.Num());

    for (int32 Slot = 0; Slot <= MaxSlot; ++Slot)
    {
        const TArray<FEnemyIntent>& SlotIntents = Buckets[Slot];

        if (SlotIntents.Num() == 0)
        {
            UE_LOG(LogTurnCore, Log,
                TEXT("[Move Slot %d] No intents, skipping"), Slot);
            continue;
        }

        UE_LOG(LogTurnCore, Log,
            TEXT("[Move Slot %d] Processing %d intents"),
            Slot, SlotIntents.Num());

        TArray<FResolvedAction> SlotActions = CoreResolvePhase(SlotIntents);

        for (FResolvedAction& Action : SlotActions)
        {
            Action.TimeSlot = Slot;
        }

        CoreExecutePhase(SlotActions);

        OutActions.Append(SlotActions);
    }

    UE_LOG(LogTurnCore, Log,
        TEXT("[ExecuteMovePhaseWithSlots] Complete. Total Actions = %d"),
        OutActions.Num());

    return MaxSlot;
}

// ============================================================================
// Attack Phase with Time Slots
// ============================================================================

int32 UTurnCorePhaseManager::ExecuteAttackPhaseWithSlots(
    const TArray<FEnemyIntent>& AllIntents,
    TArray<FResolvedAction>& OutActions)
{
    check(GetWorld() && GetWorld()->GetNetMode() != NM_Client);

    OutActions.Reset();

    if (AllIntents.Num() == 0)
    {
        UE_LOG(LogTurnCore, Log,
            TEXT("[ExecuteAttackPhaseWithSlots] No intents"));
        return 0;
    }

    const FGameplayTag& AttackTag = Tag_Attack();
    ensureMsgf(AttackTag.IsValid(),
        TEXT("Tag 'AI.Intent.Attack' is not registered."));

    TArray<TArray<FEnemyIntent>> Buckets;
    int32 MaxSlot = 0;
    BucketizeIntentsBySlot(AllIntents, Buckets, MaxSlot);

    UE_LOG(LogTurnCore, Log,
        TEXT("[ExecuteAttackPhaseWithSlots] MaxTimeSlot = %d"), MaxSlot);

    for (int32 Slot = 0; Slot <= MaxSlot; ++Slot)
    {
        const TArray<FEnemyIntent>& Src = Buckets[Slot];
        if (Src.Num() == 0)
        {
            continue;
        }

        TArray<FEnemyIntent> Attacks;
        Attacks.Reserve(Src.Num());

        for (const FEnemyIntent& Intent : Src)
        {
            if (Intent.AbilityTag.IsValid() && Intent.AbilityTag.MatchesTag(AttackTag))
            {
                Attacks.Add(Intent);
            }
        }

        if (Attacks.Num() == 0)
        {
            UE_LOG(LogTurnCore, Log,
                TEXT("[Attack Slot %d] No attacks"), Slot);
            continue;
        }

        UE_LOG(LogTurnCore, Log,
            TEXT("[Attack Slot %d] Processing %d attacks"),
            Slot, Attacks.Num());

        TArray<FResolvedAction> SlotActions;
        SlotActions.Reserve(Attacks.Num());

        for (const FEnemyIntent& Intent : Attacks)
        {
            AActor* IntentActor = Intent.Actor.Get();
            if (!IntentActor || !IsValid(IntentActor))
            {
                UE_LOG(LogTurnCore, Warning,
                    TEXT("[Attack Slot %d] Skipping invalid Intent.Actor (nullptr or destroyed)"),
                    Slot);
                continue;
            }

            FResolvedAction Action;
            Action.ActorID        = ActorRegistry ? ActorRegistry->GetStableID(IntentActor) : FStableActorID{};
            Action.Actor          = IntentActor;
            Action.SourceActor    = IntentActor;
            Action.FinalAbilityTag= AttackTag;
            Action.AbilityTag     = RogueGameplayTags::GameplayEvent_Intent_Attack;
            Action.NextCell       = Intent.NextCell;
            Action.TimeSlot       = Slot;
            Action.bIsWait        = false;
            Action.ResolutionReason = TEXT("Attack phase action");

            // Build target data if a target exists.
            if (AActor* TargetActor = Intent.Target.Get())
            {
                FGameplayAbilityTargetData_SingleTargetHit* TargetData =
                    new FGameplayAbilityTargetData_SingleTargetHit();

                FHitResult HitResult;
                HitResult.HitObjectHandle = FActorInstanceHandle(TargetActor);
                HitResult.Location        = TargetActor->GetActorLocation();
                TargetData->HitResult     = HitResult;

                Action.TargetData.Add(TargetData);

                UE_LOG(LogTurnCore, Log,
                    TEXT("[Attack Slot %d] %s -> Target: %s"),
                    Slot, *GetNameSafe(IntentActor), *GetNameSafe(TargetActor));
            }
            else if (AActor* TargetActorAlt = Intent.TargetActor)
            {
                FGameplayAbilityTargetData_SingleTargetHit* TargetData =
                    new FGameplayAbilityTargetData_SingleTargetHit();

                FHitResult HitResult;
                HitResult.HitObjectHandle = FActorInstanceHandle(TargetActorAlt);
                HitResult.Location        = TargetActorAlt->GetActorLocation();
                TargetData->HitResult     = HitResult;

                Action.TargetData.Add(TargetData);

                UE_LOG(LogTurnCore, Log,
                    TEXT("[Attack Slot %d] %s -> Target (Alt): %s"),
                    Slot, *GetNameSafe(IntentActor), *GetNameSafe(TargetActorAlt));
            }
            else
            {
                UE_LOG(LogTurnCore, Warning,
                    TEXT("[Attack Slot %d] %s has no target!"),
                    Slot, *GetNameSafe(IntentActor));
            }

            SlotActions.Add(Action);
        }

        // Filter out any invalid actions before executing.
        TArray<FResolvedAction> ValidActions;
        ValidActions.Reserve(SlotActions.Num());
        for (const FResolvedAction& Action : SlotActions)
        {
            if (Action.Actor.IsValid() &&
                Action.SourceActor != nullptr &&
                IsValid(Action.SourceActor))
            {
                ValidActions.Add(Action);
            }
            else
            {
                UE_LOG(LogTurnCore, Warning,
                    TEXT("[Attack Slot %d] Filtered out invalid action: Actor=%s"),
                    Slot, *GetNameSafe(Action.SourceActor));
            }
        }

        OutActions.Append(ValidActions);
    }

    UE_LOG(LogTurnCore, Log,
        TEXT("[ExecuteAttackPhaseWithSlots] Complete. Total = %d"),
        OutActions.Num());

    return MaxSlot;
}

// ============================================================================
// Think Phase with Time Slots (Quick enemies get a second action)
// ============================================================================

void UTurnCorePhaseManager::CoreThinkPhaseWithTimeSlots(
    const TArray<AActor*>& Enemies,
    TArray<FEnemyIntent>& OutIntents)
{
    OutIntents.Reset();
    OutIntents.Reserve(Enemies.Num() * 2);

    const FName TagQuick(TEXT("Quick"));

    for (AActor* Enemy : Enemies)
    {
        if (!Enemy)
        {
            continue;
        }

        // Primary (slot 0) intent
        TArray<FEnemyIntent> FirstIntents = CoreThinkPhase({ Enemy });
        if (FirstIntents.Num() > 0)
        {
            FEnemyIntent Intent = FirstIntents[0];
            Intent.TimeSlot = 0;
            OutIntents.Add(Intent);
        }

        // Quick enemies get a second (slot 1) intent
        if (Enemy->ActorHasTag(TagQuick))
        {
            TArray<FEnemyIntent> SecondIntents = CoreThinkPhase({ Enemy });
            if (SecondIntents.Num() > 0)
            {
                FEnemyIntent Intent2nd = SecondIntents[0];
                Intent2nd.TimeSlot = 1;
                OutIntents.Add(Intent2nd);
            }
        }
    }

    UE_LOG(LogTurnCore, Log,
        TEXT("[CoreThinkPhaseWithTimeSlots] Generated %d intents for %d enemies"),
        OutIntents.Num(), Enemies.Num());
}

// ============================================================================
// Ability System Resolution Helpers
// ============================================================================

UAbilitySystemComponent* UTurnCorePhaseManager::ResolveASC(AActor* Actor)
{
    static TSet<TWeakObjectPtr<AActor>> WarnedMissingASC;

    AActor* const SourceActor = Actor;
    const auto ReturnWithSuccess = [SourceActor](UAbilitySystemComponent* Result)
    {
        if (Result)
        {
            WarnedMissingASC.Remove(SourceActor);
        }
        return Result;
    };

    if (!Actor)
    {
        UE_LOG(LogTurnCore, Verbose,
            TEXT("[TurnCore] ResolveASC: Actor is null"));
        return nullptr;
    }

    // 1) Direct lookup via GAS globals
    if (UAbilitySystemComponent* ASC =
            UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor))
    {
        return ReturnWithSuccess(ASC);
    }

    // 2) Actor implements IAbilitySystemInterface
    if (IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Actor))
    {
        if (UAbilitySystemComponent* ASC = ASI->GetAbilitySystemComponent())
        {
            return ReturnWithSuccess(ASC);
        }
    }

    // 3) Direct component search on the actor
    if (UAbilitySystemComponent* ASC =
            Actor->FindComponentByClass<UAbilitySystemComponent>())
    {
        return ReturnWithSuccess(ASC);
    }

    // 4) Pawn → PlayerState / Controller paths
    if (APawn* Pawn = Cast<APawn>(Actor))
    {
        if (APlayerState* PS = Pawn->GetPlayerState<APlayerState>())
        {
            if (IAbilitySystemInterface* PSI = Cast<IAbilitySystemInterface>(PS))
            {
                if (UAbilitySystemComponent* ASC = PSI->GetAbilitySystemComponent())
                {
                    return ReturnWithSuccess(ASC);
                }
            }

            if (UAbilitySystemComponent* PS_ASC =
                    PS->FindComponentByClass<UAbilitySystemComponent>())
            {
                return ReturnWithSuccess(PS_ASC);
            }
        }
        else
        {
            UE_LOG(LogTurnCore, Verbose,
                TEXT("[TurnCore] ResolveASC: %s has no PlayerState"),
                *GetNameSafe(Pawn));
        }

        if (AController* C = Pawn->GetController())
        {
            if (IAbilitySystemInterface* CSI = Cast<IAbilitySystemInterface>(C))
            {
                if (UAbilitySystemComponent* ASC = CSI->GetAbilitySystemComponent())
                {
                    return ReturnWithSuccess(ASC);
                }
            }

            if (APlayerState* PS = C->GetPlayerState<APlayerState>())
            {
                if (IAbilitySystemInterface* PSI = Cast<IAbilitySystemInterface>(PS))
                {
                    if (UAbilitySystemComponent* ASC = PSI->GetAbilitySystemComponent())
                    {
                        return ReturnWithSuccess(ASC);
                    }
                }

                if (UAbilitySystemComponent* PS_ASC =
                        PS->FindComponentByClass<UAbilitySystemComponent>())
                {
                    return ReturnWithSuccess(PS_ASC);
                }
            }
            else
            {
                UE_LOG(LogTurnCore, Verbose,
                    TEXT("[TurnCore] ResolveASC: %s's Controller has no PlayerState"),
                    *GetNameSafe(C));
            }
        }
        else
        {
            UE_LOG(LogTurnCore, Verbose,
                TEXT("[TurnCore] ResolveASC: %s has no Controller"),
                *GetNameSafe(Pawn));
        }
    }

    // 5) Controller → PlayerState path
    if (AController* C = Cast<AController>(Actor))
    {
        if (APlayerState* PS = C->GetPlayerState<APlayerState>())
        {
            if (IAbilitySystemInterface* PSI = Cast<IAbilitySystemInterface>(PS))
            {
                if (UAbilitySystemComponent* ASC = PSI->GetAbilitySystemComponent())
                {
                    return ReturnWithSuccess(ASC);
                }
            }

            if (UAbilitySystemComponent* PS_ASC =
                    PS->FindComponentByClass<UAbilitySystemComponent>())
            {
                return ReturnWithSuccess(PS_ASC);
            }
        }
        else
        {
            UE_LOG(LogTurnCore, Verbose,
                TEXT("[TurnCore] ResolveASC: Controller %s has no PlayerState"),
                *GetNameSafe(C));
        }
    }

    // Failed to locate an ASC for this actor; warn once.
    if (!WarnedMissingASC.Contains(SourceActor))
    {
        WarnedMissingASC.Add(SourceActor);
        UE_LOG(LogTurnCore, Warning,
            TEXT("[TurnCore] ResolveASC: No ASC found for %s"),
            *GetNameSafe(SourceActor));
    }
    else
    {
        UE_LOG(LogTurnCore, VeryVerbose,
            TEXT("[TurnCore] ResolveASC: ASC still missing for %s"),
            *GetNameSafe(SourceActor));
    }

    return nullptr;
}

#include "AI/Enemy/EnemyTurnDataSubsystem.h"
#include "Turn/UnitTurnStateSubsystem.h"
#include "Grid/GridPathfindingSubsystem.h"

bool UTurnCorePhaseManager::ShouldEnsureIntents()
{
    if (const UEnemyTurnDataSubsystem* EnemyTurnData = GetWorld()->GetSubsystem<UEnemyTurnDataSubsystem>())
    {
        return !EnemyTurnData->HasAttackIntent() &&
               !EnemyTurnData->HasWaitIntent();
    }
    return false;
}

void UTurnCorePhaseManager::EnsureEnemyIntents(int32 TurnId, APawn* PlayerPawn)
{
	if (UEnemyTurnDataSubsystem* EnemyTurnData = GetWorld()->GetSubsystem<UEnemyTurnDataSubsystem>())
	{
		UGridPathfindingSubsystem* PathFinder = GetWorld()->GetSubsystem<UGridPathfindingSubsystem>();
		UUnitTurnStateSubsystem* UnitState = GetWorld()->GetSubsystem<UUnitTurnStateSubsystem>();
		if(!UnitState || !PathFinder) return;
		
		TArray<AActor*> EnemyActors;
		UnitState->CopyEnemiesTo(EnemyActors);
		
		TArray<FEnemyIntent> GeneratedIntents; // Dummy variable
		EnemyTurnData->EnsureIntentsFallback(TurnId, PlayerPawn, PathFinder, EnemyActors, GeneratedIntents);
	}
}

bool UTurnCorePhaseManager::IsGASReady(AActor* Actor)
{
    if (!Actor)
    {
        return false;
    }

    UAbilitySystemComponent* ASC =
        UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor);
    if (!ASC)
    {
        return false;
    }

    const FGameplayAbilityActorInfo* Info = ASC->AbilityActorInfo.Get();
    if (!Info || !Info->AvatarActor.IsValid())
    {
        UE_LOG(LogTurnCore, Verbose,
            TEXT("[TurnCore] IsGASReady: %s has invalid ActorInfo"),
            *GetNameSafe(Actor));
        return false;
    }

    const TArray<FGameplayAbilitySpec>& Abilities =
        ASC->GetActivatableAbilities();

    if (Abilities.Num() == 0)
    {
        UE_LOG(LogTurnCore, Verbose,
            TEXT("[TurnCore] IsGASReady: %s has ASC but no abilities yet"),
            *GetNameSafe(Actor));
        return false;
    }

    UE_LOG(LogTurnCore, Verbose,
        TEXT("[TurnCore] IsGASReady: %s has %d abilities - READY"),
        *GetNameSafe(Actor), Abilities.Num());
    return true;
}

#include "Turn/MoveReservationSubsystem.h"
#include "Utility/RogueGameplayTags.h"

namespace TurnCorePhaseManager_Private
{
    void AppendPlayerIntentIfPending(UWorld* World, APawn* PlayerPawn, TArray<FEnemyIntent>& InOutIntents)
    {
        if (!PlayerPawn || !World) return;

        UGridPathfindingSubsystem* PathFinder = World->GetSubsystem<UGridPathfindingSubsystem>();
        UMoveReservationSubsystem* MoveRes = World->GetSubsystem<UMoveReservationSubsystem>();
        if(!PathFinder || !MoveRes) return;
        
        const FVector PlayerLoc = PlayerPawn->GetActorLocation();
        const FIntPoint PlayerCurrentCell = PathFinder->WorldToGrid(PlayerLoc);

        const TMap<TWeakObjectPtr<AActor>, FIntPoint>& Reservations = MoveRes->GetPendingMoveReservations();
        TWeakObjectPtr<AActor> PlayerKey(PlayerPawn);
        const FIntPoint* PlayerNextCell = Reservations.Find(PlayerKey);

        if (PlayerNextCell && PlayerCurrentCell != *PlayerNextCell)
        {
            FEnemyIntent PlayerIntent;
            PlayerIntent.Actor = PlayerPawn;
            PlayerIntent.CurrentCell = PlayerCurrentCell;
            PlayerIntent.NextCell = *PlayerNextCell;
            PlayerIntent.AbilityTag = RogueGameplayTags::AI_Intent_Move;
            PlayerIntent.BasePriority = 200; // High priority for player
            InOutIntents.Add(PlayerIntent);
        }
    }
}

TArray<FResolvedAction> UTurnCorePhaseManager::ExecuteMovePhaseWithResolution(int32 TurnId, APawn* PlayerPawn, bool& OutCancelledPlayer)
{
    OutCancelledPlayer = false;
    UWorld* World = GetWorld();
    
    UEnemyTurnDataSubsystem* EnemyData = World->GetSubsystem<UEnemyTurnDataSubsystem>();
    if (!EnemyData) 
    { 
        UE_LOG(LogTurnCore, Error, TEXT("ExecuteMovePhaseWithResolution: EnemyData subsystem not found"));
        return {}; 
    }
    
    if (ShouldEnsureIntents())
    {
        EnsureEnemyIntents(TurnId, PlayerPawn);
    }
    
    if (EnemyData->Intents.Num() == 0) 
    { 
        UE_LOG(LogTurnCore, Log, TEXT("ExecuteMovePhaseWithResolution: No intents to process"));
        return {}; 
    }
    
    TArray<FEnemyIntent> AllIntents = EnemyData->Intents;
    
    TurnCorePhaseManager_Private::AppendPlayerIntentIfPending(World, PlayerPawn, AllIntents);
    
    TArray<FResolvedAction> ResolvedActions = CoreResolvePhase(AllIntents);
    
    if (PlayerPawn) {
        for (const FResolvedAction& Action : ResolvedActions) {
            if (IsValid(Action.SourceActor.Get()) && Action.SourceActor.Get() == PlayerPawn && Action.bIsWait) {
                OutCancelledPlayer = true;
                
                UE_LOG(LogTurnCore, Warning, TEXT("Player move cancelled by conflict resolver"));
                
                if (UAbilitySystemComponent* ASC = PlayerPawn->FindComponentByClass<UAbilitySystemComponent>()) {
                    ASC->CancelAllAbilities();
                }
                
                if(UMoveReservationSubsystem* MoveRes = World->GetSubsystem<UMoveReservationSubsystem>()) {
                    MoveRes->ReleaseMoveReservation(PlayerPawn);
                }
                
                return {};
            }
        }
    }
    
    return ResolvedActions;
}

bool UTurnCorePhaseManager::AllEnemiesReady(const TArray<AActor*>& Enemies) const
{
    int32 ReadyCount    = 0;
    int32 NotReadyCount = 0;

    for (AActor* Enemy : Enemies)
    {
        if (IsGASReady(Enemy))
        {
            ReadyCount++;
        }
        else
        {
            NotReadyCount++;
            if (NotReadyCount <= 3)
            {
                UE_LOG(LogTurnCore, Verbose,
                    TEXT("[TurnCore] AllEnemiesReady: %s not ready yet"),
                    *GetNameSafe(Enemy));
            }
        }
    }

    if (ReadyCount < Enemies.Num())
    {
        UE_LOG(LogTurnCore, Log,
            TEXT("[TurnCore] AllEnemiesReady: %d/%d enemies ready (waiting for %d)"),
            ReadyCount, Enemies.Num(), NotReadyCount);
        return false;
    }

    UE_LOG(LogTurnCore, Log,
        TEXT("[TurnCore] AllEnemiesReady: All %d enemies ready"),
        Enemies.Num());
    return true;
}
