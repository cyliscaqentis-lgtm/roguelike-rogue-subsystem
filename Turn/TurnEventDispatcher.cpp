// ============================================================================
// TurnEventDispatcher.cpp
// ターンイベント配信Subsystem実装
// GameTurnManagerBaseから分離（2025-11-09）
// ============================================================================

#include "Turn/TurnEventDispatcher.h"
#include "Grid/URogueDungeonSubsystem.h"
#include "../ProjectDiagnostics.h"

//------------------------------------------------------------------------------
// Subsystem Lifecycle
//------------------------------------------------------------------------------

void UTurnEventDispatcher::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	bEventsEnabled = true;

	UE_LOG(LogTurnManager, Log, TEXT("[TurnEventDispatcher] Initialized"));
}

void UTurnEventDispatcher::Deinitialize()
{
	ClearAllDelegates();

	UE_LOG(LogTurnManager, Log, TEXT("[TurnEventDispatcher] Deinitialized"));

	Super::Deinitialize();
}

//------------------------------------------------------------------------------
// Event Broadcasting
//------------------------------------------------------------------------------

void UTurnEventDispatcher::BroadcastTurnStarted(int32 TurnIndex)
{
	if (!bEventsEnabled)
	{
		return;
	}

	UE_LOG(LogTurnManager, Verbose, TEXT("[TurnEventDispatcher] Broadcasting TurnStarted: TurnIndex=%d"), TurnIndex);

	OnTurnStarted.Broadcast(TurnIndex);
}

void UTurnEventDispatcher::BroadcastPlayerInputReceived()
{
	if (!bEventsEnabled)
	{
		return;
	}

	UE_LOG(LogTurnManager, Verbose, TEXT("[TurnEventDispatcher] Broadcasting PlayerInputReceived"));

	OnPlayerInputReceived.Broadcast();
}

void UTurnEventDispatcher::BroadcastFloorReady(URogueDungeonSubsystem* DungeonSubsystem)
{
	if (!bEventsEnabled)
	{
		return;
	}

	UE_LOG(LogTurnManager, Verbose, TEXT("[TurnEventDispatcher] Broadcasting FloorReady"));

	OnFloorReady.Broadcast(DungeonSubsystem);
}

void UTurnEventDispatcher::BroadcastPhaseChanged(FGameplayTag OldPhase, FGameplayTag NewPhase)
{
	if (!bEventsEnabled)
	{
		return;
	}

	UE_LOG(LogTurnManager, Log, TEXT("[TurnEventDispatcher] Phase changed: %s -> %s"),
		*OldPhase.ToString(), *NewPhase.ToString());

	OnPhaseChanged.Broadcast(OldPhase, NewPhase);
}

void UTurnEventDispatcher::BroadcastTurnEnded(int32 TurnIndex)
{
	if (!bEventsEnabled)
	{
		return;
	}

	UE_LOG(LogTurnManager, Verbose, TEXT("[TurnEventDispatcher] Broadcasting TurnEnded: TurnIndex=%d"), TurnIndex);

	OnTurnEnded.Broadcast(TurnIndex);
}

void UTurnEventDispatcher::BroadcastActionExecuted(int32 UnitID, FGameplayTag ActionTag, bool bSuccess)
{
	if (!bEventsEnabled)
	{
		return;
	}

	UE_LOG(LogTurnManager, Verbose, TEXT("[TurnEventDispatcher] Action executed: UnitID=%d, Tag=%s, Success=%d"),
		UnitID, *ActionTag.ToString(), bSuccess);

	OnActionExecuted.Broadcast(UnitID, ActionTag, bSuccess);
}

void UTurnEventDispatcher::ClearAllDelegates()
{
	OnTurnStarted.Clear();
	OnPlayerInputReceived.Clear();
	OnFloorReady.Clear();
	OnPhaseChanged.Clear();
	OnTurnEnded.Clear();
	OnActionExecuted.Clear();

	UE_LOG(LogTurnManager, Log, TEXT("[TurnEventDispatcher] All delegates cleared"));
}
