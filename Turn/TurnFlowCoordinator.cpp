// TurnFlowCoordinator.cpp

#include "TurnFlowCoordinator.h"
<<<<<<< HEAD
=======
#include "Turn/TurnEventDispatcher.h"
>>>>>>> origin/claude/ue5-rogue-refactor-complete-011CUvsUqjPorTXvdbGRGcm4
#include "Net/UnrealNetwork.h"

void UTurnFlowCoordinator::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UTurnFlowCoordinator, CurrentTurnId);
	DOREPLIFETIME(UTurnFlowCoordinator, InputWindowId);
	DOREPLIFETIME(UTurnFlowCoordinator, CurrentTurnIndex);
	DOREPLIFETIME(UTurnFlowCoordinator, PlayerAP);
	DOREPLIFETIME(UTurnFlowCoordinator, bFirstTurnStarted);
}

void UTurnFlowCoordinator::StartFirstTurn()
{
	UWorld* World = GetWorld();
	if (!World || !World->GetAuthGameMode())
	{
		return; // サーバーのみ実行
	}

	if (bFirstTurnStarted)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TurnFlowCoordinator] StartFirstTurn already called, skipping"));
		return;
	}

	bFirstTurnStarted = true;
	CurrentTurnId = 1;
	CurrentTurnIndex = 0;
	ResetPlayerAPForTurn();

	UE_LOG(LogTemp, Log, TEXT("[TurnFlowCoordinator] First turn started: TurnId=%d"), CurrentTurnId);

<<<<<<< HEAD
	// デリゲート通知
	OnTurnStarted.Broadcast(CurrentTurnIndex);
=======
	// デリゲート通知（TurnEventDispatcher経由）
	if (UTurnEventDispatcher* EventDispatcher = World->GetSubsystem<UTurnEventDispatcher>())
	{
		EventDispatcher->BroadcastTurnStarted(CurrentTurnIndex);
	}
>>>>>>> origin/claude/ue5-rogue-refactor-complete-011CUvsUqjPorTXvdbGRGcm4
}

void UTurnFlowCoordinator::StartTurn()
{
	UWorld* World = GetWorld();
	if (!World || !World->GetAuthGameMode())
	{
		return; // サーバーのみ実行
	}

	ResetPlayerAPForTurn();

	UE_LOG(LogTemp, Log, TEXT("[TurnFlowCoordinator] Turn started: TurnId=%d, TurnIndex=%d"), CurrentTurnId, CurrentTurnIndex);

<<<<<<< HEAD
	// デリゲート通知
	OnTurnStarted.Broadcast(CurrentTurnIndex);
=======
	// デリゲート通知（TurnEventDispatcher経由）
	if (UTurnEventDispatcher* EventDispatcher = World->GetSubsystem<UTurnEventDispatcher>())
	{
		EventDispatcher->BroadcastTurnStarted(CurrentTurnIndex);
	}
>>>>>>> origin/claude/ue5-rogue-refactor-complete-011CUvsUqjPorTXvdbGRGcm4
}

void UTurnFlowCoordinator::EndTurn()
{
	UWorld* World = GetWorld();
	if (!World || !World->GetAuthGameMode())
	{
		return; // サーバーのみ実行
	}

	UE_LOG(LogTemp, Log, TEXT("[TurnFlowCoordinator] Turn ended: TurnId=%d"), CurrentTurnId);

<<<<<<< HEAD
	// デリゲート通知
	OnTurnEnded.Broadcast(CurrentTurnId);
=======
	// デリゲート通知（TurnEventDispatcher経由）
	if (UTurnEventDispatcher* EventDispatcher = World->GetSubsystem<UTurnEventDispatcher>())
	{
		EventDispatcher->BroadcastTurnEnded(CurrentTurnId);
	}
>>>>>>> origin/claude/ue5-rogue-refactor-complete-011CUvsUqjPorTXvdbGRGcm4
}

void UTurnFlowCoordinator::AdvanceTurn()
{
	UWorld* World = GetWorld();
	if (!World || !World->GetAuthGameMode())
	{
		return; // サーバーのみ実行
	}

	++CurrentTurnId;
	++CurrentTurnIndex;

	UE_LOG(LogTemp, Log, TEXT("[TurnFlowCoordinator] Turn advanced: TurnId=%d, TurnIndex=%d"), CurrentTurnId, CurrentTurnIndex);
}

bool UTurnFlowCoordinator::CanAdvanceTurn(int32 TurnId) const
{
	return TurnId == CurrentTurnId;
}

void UTurnFlowCoordinator::ConsumePlayerAP(int32 Amount)
{
	PlayerAP = FMath::Max(0, PlayerAP - Amount);
	UE_LOG(LogTemp, Log, TEXT("[TurnFlowCoordinator] AP consumed: %d, Remaining: %d"), Amount, PlayerAP);
}

void UTurnFlowCoordinator::RestorePlayerAP(int32 Amount)
{
	PlayerAP = FMath::Min(PlayerAPPerTurn, PlayerAP + Amount);
	UE_LOG(LogTemp, Log, TEXT("[TurnFlowCoordinator] AP restored: %d, Current: %d"), Amount, PlayerAP);
}

void UTurnFlowCoordinator::ResetPlayerAPForTurn()
{
	PlayerAP = PlayerAPPerTurn;
	UE_LOG(LogTemp, Log, TEXT("[TurnFlowCoordinator] AP reset to: %d"), PlayerAP);
}

bool UTurnFlowCoordinator::HasSufficientAP(int32 Required) const
{
	return PlayerAP >= Required;
}

void UTurnFlowCoordinator::QueueEnemyPhase()
{
	bEnemyPhaseQueued = true;
	UE_LOG(LogTemp, Log, TEXT("[TurnFlowCoordinator] Enemy phase queued"));
}

void UTurnFlowCoordinator::ClearEnemyPhaseQueue()
{
	bEnemyPhaseQueued = false;
	UE_LOG(LogTemp, Log, TEXT("[TurnFlowCoordinator] Enemy phase queue cleared"));
}

void UTurnFlowCoordinator::OnRep_CurrentTurnId()
{
	UE_LOG(LogTemp, Log, TEXT("[TurnFlowCoordinator] OnRep_CurrentTurnId: %d"), CurrentTurnId);
}

void UTurnFlowCoordinator::OnRep_InputWindowId()
{
	UE_LOG(LogTemp, Log, TEXT("[TurnFlowCoordinator] OnRep_InputWindowId: %d"), InputWindowId);
}
