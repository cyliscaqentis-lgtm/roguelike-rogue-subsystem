// Copyright Epic Games, Inc. All Rights Reserved.
// CodeRevision: INC-2025-1122-R2 (Extract move reservation from GameTurnManagerBase) (2025-11-22)

#include "Turn/MoveReservationSubsystem.h"
#include "Turn/GameTurnManagerBase.h"
#include "Turn/TurnActionBarrierSubsystem.h"
#include "Grid/GridPathfindingSubsystem.h"
#include "Grid/GridOccupancySubsystem.h"
#include "Character/UnitBase.h"
#include "Character/UnitMovementComponent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Utility/RogueGameplayTags.h"
#include "Utility/TurnTagCleanupUtils.h"
#include "Utility/TurnAuthorityUtils.h"
#include "Utility/TurnCommandEncoding.h"
#include "Turn/PlayerInputProcessor.h"

DEFINE_LOG_CATEGORY(LogMoveReservation);

void UMoveReservationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    UE_LOG(LogMoveReservation, Log, TEXT("MoveReservationSubsystem initialized"));
}

void UMoveReservationSubsystem::Deinitialize()
{
    ClearResolvedMoves();
    Super::Deinitialize();
}

bool UMoveReservationSubsystem::RegisterResolvedMove(AActor* Actor, const FIntPoint& Cell)
{
    if (!Actor)
    {
        UE_LOG(LogMoveReservation, Error, TEXT("[RegisterResolvedMove] FAILED: Actor is null"));
        return false;
    }

    TWeakObjectPtr<AActor> ActorKey(Actor);
    PendingMoveReservations.FindOrAdd(ActorKey) = Cell;

    UWorld* World = GetWorld();
    if (World)
    {
        if (UGridOccupancySubsystem* Occupancy = World->GetSubsystem<UGridOccupancySubsystem>())
        {
            const bool bReserved = Occupancy->ReserveCellForActor(Actor, Cell);
            if (!bReserved)
            {
                UE_LOG(LogMoveReservation, Error,
                    TEXT("[RegisterResolvedMove] FAILED: %s cannot reserve (%d,%d) - already reserved"),
                    *GetNameSafe(Actor), Cell.X, Cell.Y);
                return false;
            }

            UE_LOG(LogMoveReservation, Verbose,
                TEXT("[RegisterResolvedMove] SUCCESS: %s reserved (%d,%d)"),
                *GetNameSafe(Actor), Cell.X, Cell.Y);
            return true;
        }
    }

    UE_LOG(LogMoveReservation, Error, TEXT("[RegisterResolvedMove] FAILED: GridOccupancySubsystem not available"));
    return false;
}

bool UMoveReservationSubsystem::IsMoveAuthorized(AActor* Actor, const FIntPoint& Cell) const
{
    if (!Actor || PendingMoveReservations.Num() == 0)
    {
        return true;
    }

    TWeakObjectPtr<AActor> ActorKey(Actor);
    if (const FIntPoint* ReservedCell = PendingMoveReservations.Find(ActorKey))
    {
        return *ReservedCell == Cell;
    }

    return true;
}

bool UMoveReservationSubsystem::HasReservationFor(AActor* Actor, const FIntPoint& Cell) const
{
    if (!Actor)
    {
        return false;
    }

    TWeakObjectPtr<AActor> ActorKey(Actor);
    if (const FIntPoint* ReservedCell = PendingMoveReservations.Find(ActorKey))
    {
        return *ReservedCell == Cell;
    }

    return false;
}

void UMoveReservationSubsystem::ReleaseMoveReservation(AActor* Actor)
{
    if (!Actor)
    {
        return;
    }

    TWeakObjectPtr<AActor> ActorKey(Actor);
    PendingMoveReservations.Remove(ActorKey);

    if (AUnitBase* Unit = Cast<AUnitBase>(Actor))
    {
        PendingPlayerFallbackMoves.Remove(Unit);
    }

    if (UWorld* World = GetWorld())
    {
        if (UGridOccupancySubsystem* Occupancy = World->GetSubsystem<UGridOccupancySubsystem>())
        {
            Occupancy->ReleaseReservationForActor(Actor);
        }
    }
}

void UMoveReservationSubsystem::ClearResolvedMoves()
{
    PendingMoveReservations.Reset();
    PendingPlayerFallbackMoves.Reset();
    PendingMoveActionRegistrations.Reset();

    for (const TWeakObjectPtr<AUnitBase>& UnitPtr : ActiveMoveDelegates)
    {
        if (AUnitBase* Unit = UnitPtr.Get())
        {
            if (UUnitMovementComponent* MovementComp = Unit->MovementComp.Get())
            {
                MovementComp->OnMoveFinished.RemoveDynamic(this, &UMoveReservationSubsystem::HandleManualMoveFinished);
            }
        }
    }
    ActiveMoveDelegates.Reset();

    if (UWorld* World = GetWorld())
    {
        if (UGridOccupancySubsystem* Occupancy = World->GetSubsystem<UGridOccupancySubsystem>())
        {
            Occupancy->ClearAllReservations();
        }
    }
}

bool UMoveReservationSubsystem::DispatchResolvedMove(const FResolvedAction& Action, AGameTurnManagerBase* TurnManager)
{
    UWorld* World = GetWorld();
    if (!World || !IsAuthorityLike(World))
    {
        return false;
    }

    // Cache TurnManager for callbacks
    CachedTurnManager = TurnManager;

    AActor* SourceActor = Action.SourceActor ? Action.SourceActor.Get() : nullptr;
    if (!SourceActor && Action.Actor.IsValid())
    {
        SourceActor = Action.Actor.Get();
    }

    if (!SourceActor)
    {
        UE_LOG(LogMoveReservation, Warning, TEXT("[DispatchResolvedMove] Missing source actor"));
        return false;
    }

    AUnitBase* Unit = Cast<AUnitBase>(SourceActor);
    if (!Unit)
    {
        UE_LOG(LogMoveReservation, Warning,
            TEXT("[DispatchResolvedMove] %s is not a UnitBase"), *GetNameSafe(SourceActor));
        ReleaseMoveReservation(SourceActor);
        return false;
    }

    // Handle wait/no-op actions
    if (HandleWaitResolvedAction(Unit, Action))
    {
        return false;
    }

    if (!IsMoveAuthorized(Unit, Action.NextCell))
    {
        UE_LOG(LogMoveReservation, Error,
            TEXT("[DispatchResolvedMove] AUTHORIZATION FAILED: %s tried to move to (%d,%d)"),
            *GetNameSafe(Unit), Action.NextCell.X, Action.NextCell.Y);

        TWeakObjectPtr<AActor> ActorKey(Unit);
        if (const FIntPoint* ReservedCell = PendingMoveReservations.Find(ActorKey))
        {
            UE_LOG(LogMoveReservation, Error,
                TEXT("[DispatchResolvedMove] Reserved cell is (%d,%d), but Action.NextCell is (%d,%d)"),
                ReservedCell->X, ReservedCell->Y, Action.NextCell.X, Action.NextCell.Y);
        }

        ReleaseMoveReservation(Unit);
        return false;
    }

    UGridPathfindingSubsystem* PathFinder = World->GetSubsystem<UGridPathfindingSubsystem>();
    if (!PathFinder)
    {
        UE_LOG(LogMoveReservation, Error, TEXT("[DispatchResolvedMove] PathFinder missing"));
        ReleaseMoveReservation(Unit);
        return false;
    }

    const bool bIsPlayerUnit = (Unit->Team == 0);
    if (bIsPlayerUnit)
    {
        return HandlePlayerResolvedMove(Unit, Action, TurnManager);
    }

    return HandleEnemyResolvedMove(Unit, Action, PathFinder);
}

bool UMoveReservationSubsystem::HandleWaitResolvedAction(AUnitBase* Unit, const FResolvedAction& Action)
{
    if (!Unit)
    {
        return false;
    }

    // Wait or "no-op" move: register with barrier and complete immediately
    if (Action.NextCell != FIntPoint(-1, -1) && Action.NextCell != Action.CurrentCell)
    {
        return false;
    }

    ReleaseMoveReservation(Unit);

    if (UWorld* World = GetWorld())
    {
        if (UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>())
        {
            const int32 RegisteredTurnId = Barrier->GetCurrentTurnId();
            const FGuid ActionId = Barrier->RegisterAction(Unit, RegisteredTurnId);
            if (ActionId.IsValid())
            {
                Barrier->CompleteAction(Unit, RegisteredTurnId, ActionId);
                UE_LOG(LogMoveReservation, Verbose,
                    TEXT("[DispatchResolvedMove] Registered+Completed WAIT action: Actor=%s TurnId=%d"),
                    *GetNameSafe(Unit), RegisteredTurnId);
            }
        }
    }

    return true;
}

bool UMoveReservationSubsystem::HandlePlayerResolvedMove(AUnitBase* Unit, const FResolvedAction& Action, AGameTurnManagerBase* TurnManager)
{
    if (!Unit || !TurnManager)
    {
        return false;
    }

    if (TurnManager->bPlayerMoveInProgress)
    {
        UE_LOG(LogMoveReservation, Log,
            TEXT("[DispatchResolvedMove] Player move already in progress, skipping for %s"),
            *GetNameSafe(Unit));
        return true;
    }

    if (TriggerPlayerMoveAbility(Action, Unit, TurnManager))
    {
        return true;
    }

    UE_LOG(LogMoveReservation, Error,
        TEXT("[DispatchResolvedMove] Player GA trigger failed - Move BLOCKED"));
    ReleaseMoveReservation(Unit);
    return false;
}

bool UMoveReservationSubsystem::HandleEnemyResolvedMove(AUnitBase* Unit, const FResolvedAction& Action, UGridPathfindingSubsystem* PathFinder)
{
    if (!Unit || !PathFinder)
    {
        if (Unit)
        {
            ReleaseMoveReservation(Unit);
        }
        return false;
    }

    const FVector StartWorld = Unit->GetActorLocation();
    const FVector EndWorld = PathFinder->GridToWorld(Action.NextCell, StartWorld.Z);

    TArray<FVector> PathPoints;
    PathPoints.Add(EndWorld);

    Unit->MoveUnit(PathPoints);

    RegisterManualMoveDelegate(Unit, /*bIsPlayerFallback*/ false);

    if (UWorld* World = GetWorld())
    {
        if (UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>())
        {
            const int32 RegisteredTurnId = Barrier->GetCurrentTurnId();
            const FGuid ActionId = Barrier->RegisterAction(Unit, RegisteredTurnId);
            if (ActionId.IsValid())
            {
                FManualMoveBarrierInfo Info;
                Info.TurnId = RegisteredTurnId;
                Info.ActionId = ActionId;
                PendingMoveActionRegistrations.Add(Unit, Info);
                UE_LOG(LogMoveReservation, Log,
                    TEXT("[DispatchResolvedMove] Registered enemy move: Actor=%s, TurnId=%d, ActionId=%s"),
                    *GetNameSafe(Unit), RegisteredTurnId, *ActionId.ToString());
            }
        }
    }

    return true;
}

void UMoveReservationSubsystem::RegisterManualMoveDelegate(AUnitBase* Unit, bool bIsPlayerFallback)
{
    if (!Unit)
    {
        return;
    }

    if (bIsPlayerFallback)
    {
        PendingPlayerFallbackMoves.Add(Unit);
    }

    if (ActiveMoveDelegates.Contains(Unit))
    {
        return;
    }

    if (UUnitMovementComponent* MovementComp = Unit->MovementComp.Get())
    {
        MovementComp->OnMoveFinished.AddDynamic(this, &UMoveReservationSubsystem::HandleManualMoveFinished);
        ActiveMoveDelegates.Add(Unit);

        UE_LOG(LogMoveReservation, Verbose,
            TEXT("[RegisterManualMoveDelegate] Bound delegate for %s"),
            *GetNameSafe(Unit));
        return;
    }

    UE_LOG(LogMoveReservation, Error,
        TEXT("[RegisterManualMoveDelegate] Failed for %s: MovementComp missing!"),
        *GetNameSafe(Unit));
}

void UMoveReservationSubsystem::HandleManualMoveFinished(AUnitBase* Unit)
{
    UWorld* World = GetWorld();
    if (!World || !IsAuthorityLike(World) || !Unit)
    {
        return;
    }

    if (ActiveMoveDelegates.Contains(Unit))
    {
        if (UUnitMovementComponent* MovementComp = Unit->MovementComp.Get())
        {
            MovementComp->OnMoveFinished.RemoveDynamic(this, &UMoveReservationSubsystem::HandleManualMoveFinished);
        }
        ActiveMoveDelegates.Remove(Unit);
    }

    if (FManualMoveBarrierInfo* BarrierInfo = PendingMoveActionRegistrations.Find(Unit))
    {
        if (BarrierInfo->ActionId.IsValid() && BarrierInfo->TurnId != INDEX_NONE)
        {
            if (UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>())
            {
                Barrier->CompleteAction(Unit, BarrierInfo->TurnId, BarrierInfo->ActionId);
                UE_LOG(LogMoveReservation, Log,
                    TEXT("[HandleManualMoveFinished] Completed barrier for %s TurnId=%d"),
                    *GetNameSafe(Unit), BarrierInfo->TurnId);
            }
        }

        PendingMoveActionRegistrations.Remove(Unit);
    }

    const bool bPlayerFallback = PendingPlayerFallbackMoves.Remove(Unit) > 0;
    if (bPlayerFallback && CachedTurnManager.IsValid())
    {
        if (UPlayerInputProcessor* InputProc = World->GetSubsystem<UPlayerInputProcessor>())
        {
            InputProc->OnPlayerMoveFinalized(CachedTurnManager.Get(), Unit);
        }
    }

    ReleaseMoveReservation(Unit);
}

bool UMoveReservationSubsystem::IsPendingPlayerFallback(AUnitBase* Unit) const
{
    return Unit && PendingPlayerFallbackMoves.Contains(Unit);
}

bool UMoveReservationSubsystem::RemoveFromPendingPlayerFallback(AUnitBase* Unit)
{
    return Unit && PendingPlayerFallbackMoves.Remove(Unit) > 0;
}

// CodeRevision: INC-2025-1122-R4 (Move TriggerPlayerMoveAbility to MoveReservationSubsystem) (2025-11-22)
bool UMoveReservationSubsystem::TriggerPlayerMoveAbility(const FResolvedAction& Action, AUnitBase* Unit, AGameTurnManagerBase* TurnManager)
{
    if (!Unit || !TurnManager)
    {
        return false;
    }

    UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Unit);
    if (!ASC)
    {
        UE_LOG(LogMoveReservation, Warning, TEXT("[TriggerPlayerMove] Player ASC missing"));
        return false;
    }

    const int32 ClearedCount = TurnTagCleanupUtils::CleanseBlockingTags(ASC, Unit);

    const int32 AbilityCount = ASC->GetActivatableAbilities().Num();
    if (AbilityCount == 0)
    {
        UE_LOG(LogMoveReservation, Warning,
            TEXT("[TriggerPlayerMove] No abilities granted to %s - ASC may not be initialized"),
            *GetNameSafe(Unit));
    }
    else
    {
        UE_LOG(LogMoveReservation, Log,
            TEXT("[TriggerPlayerMove] %s has %d abilities (cleared %d blocking tags)"),
            *GetNameSafe(Unit), AbilityCount, ClearedCount);
    }

    const FIntPoint Delta = Action.NextCell - Action.CurrentCell;
    const int32 DirX = FMath::Clamp(Delta.X, -1, 1);
    const int32 DirY = FMath::Clamp(Delta.Y, -1, 1);

    if (DirX == 0 && DirY == 0)
    {
        UE_LOG(LogMoveReservation, Warning, TEXT("[TriggerPlayerMove] Invalid delta (0,0)"));
        return false;
    }

    FGameplayEventData EventData;
    EventData.EventTag = RogueGameplayTags::GameplayEvent_Intent_Move;
    EventData.Instigator = Unit;
    EventData.Target = Unit;
    EventData.EventMagnitude = static_cast<float>(TurnCommandEncoding::PackCell(Action.NextCell.X, Action.NextCell.Y));
    EventData.OptionalObject = TurnManager;

    const int32 TriggeredCount = ASC->HandleGameplayEvent(EventData.EventTag, &EventData);
    if (TriggeredCount > 0)
    {
        TurnManager->CachedPlayerCommand.Direction = FVector(static_cast<double>(DirX), static_cast<double>(DirY), 0.0);
        TurnManager->bPlayerMoveInProgress = true;

        if (UWorld* World = GetWorld())
        {
            if (UPlayerInputProcessor* InputProc = World->GetSubsystem<UPlayerInputProcessor>())
            {
                InputProc->SetPlayerMoveState(TurnManager->CachedPlayerCommand.Direction, true);
            }
        }

        UE_LOG(LogMoveReservation, Log,
            TEXT("[TriggerPlayerMove] Player move ability triggered toward (%d,%d)"),
            Action.NextCell.X, Action.NextCell.Y);
        return true;
    }

    FGameplayTagContainer CurrentTags;
    ASC->GetOwnedGameplayTags(CurrentTags);
    UE_LOG(LogMoveReservation, Error,
        TEXT("[TriggerPlayerMove] HandleGameplayEvent returned 0 for %s (AbilityCount=%d, OwnedTags=%s)"),
        *GetNameSafe(Unit), AbilityCount, *CurrentTags.ToStringSimple());

    return false;
}
