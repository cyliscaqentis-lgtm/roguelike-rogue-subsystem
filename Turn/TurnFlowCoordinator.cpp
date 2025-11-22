// TurnFlowCoordinator.cpp

#include "TurnFlowCoordinator.h"
#include "Net/UnrealNetwork.h"
#include "Utility/TurnAuthorityUtils.h"

DEFINE_LOG_CATEGORY(LogTurnFlow);

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
	if (!IsAuthorityLike(World))
	{
		return;
	}

	if (bFirstTurnStarted)
	{
		UE_LOG(LogTurnFlow, Warning, TEXT("[TurnFlowCoordinator] StartFirstTurn already called, skipping"));
		return;
	}

	bFirstTurnStarted = true;
	CurrentTurnId = 1;
	CurrentTurnIndex = 0;
	ResetPlayerAPForTurn();

	UE_LOG(LogTurnFlow, Log, TEXT("[TurnFlowCoordinator] First turn started: TurnId=%d"), CurrentTurnId);
}

void UTurnFlowCoordinator::StartTurn()
{
	UWorld* World = GetWorld();
	if (!IsAuthorityLike(World))
	{
		return;
	}

	ResetPlayerAPForTurn();

	UE_LOG(LogTurnFlow, Log,
		TEXT("[TurnFlowCoordinator] Turn started: TurnId=%d, TurnIndex=%d"),
		CurrentTurnId, CurrentTurnIndex);
}

void UTurnFlowCoordinator::EndTurn()
{
	UWorld* World = GetWorld();
	if (!IsAuthorityLike(World))
	{
		return;
	}

	UE_LOG(LogTurnFlow, Log, TEXT("[TurnFlowCoordinator] Turn ended: TurnId=%d"), CurrentTurnId);
}

void UTurnFlowCoordinator::AdvanceTurn()
{
	UWorld* World = GetWorld();
	if (!IsAuthorityLike(World))
	{
		return;
	}

	++CurrentTurnId;
	++CurrentTurnIndex;

	UE_LOG(LogTurnFlow, Log,
		TEXT("[TurnFlowCoordinator] Turn advanced: TurnId=%d, TurnIndex=%d"),
		CurrentTurnId, CurrentTurnIndex);
}

bool UTurnFlowCoordinator::CanAdvanceTurn(int32 TurnId) const
{
	return TurnId == CurrentTurnId;
}

void UTurnFlowCoordinator::ConsumePlayerAP(int32 Amount)
{
	PlayerAP = FMath::Max(0, PlayerAP - Amount);
	UE_LOG(LogTurnFlow, Log,
		TEXT("[TurnFlowCoordinator] AP consumed: %d, Remaining: %d"),
		Amount, PlayerAP);
}

void UTurnFlowCoordinator::RestorePlayerAP(int32 Amount)
{
	PlayerAP = FMath::Min(PlayerAPPerTurn, PlayerAP + Amount);
	UE_LOG(LogTurnFlow, Log,
		TEXT("[TurnFlowCoordinator] AP restored: %d, Current: %d"),
		Amount, PlayerAP);
}

void UTurnFlowCoordinator::ResetPlayerAPForTurn()
{
	PlayerAP = PlayerAPPerTurn;
	UE_LOG(LogTurnFlow, Log,
		TEXT("[TurnFlowCoordinator] AP reset to: %d"),
		PlayerAP);
}

bool UTurnFlowCoordinator::HasSufficientAP(int32 Required) const
{
	return PlayerAP >= Required;
}

void UTurnFlowCoordinator::QueueEnemyPhase()
{
	bEnemyPhaseQueued = true;
	UE_LOG(LogTurnFlow, Log, TEXT("[TurnFlowCoordinator] Enemy phase queued"));
}

void UTurnFlowCoordinator::ClearEnemyPhaseQueue()
{
	bEnemyPhaseQueued = false;
	UE_LOG(LogTurnFlow, Log, TEXT("[TurnFlowCoordinator] Enemy phase queue cleared"));
}

void UTurnFlowCoordinator::OnRep_CurrentTurnId()
{
	UE_LOG(LogTurnFlow, Log,
		TEXT("[TurnFlowCoordinator] OnRep_CurrentTurnId: %d"),
		CurrentTurnId);
}

void UTurnFlowCoordinator::OnRep_InputWindowId()
{
	UE_LOG(LogTurnFlow, Log,
		TEXT("[TurnFlowCoordinator] OnRep_InputWindowId: %d"),
		InputWindowId);
}

void UTurnFlowCoordinator::OpenNewInputWindow()
{
	UWorld* World = GetWorld();
	if (!IsAuthorityLike(World))
	{
		UE_LOG(LogTurnFlow, Warning,
			TEXT("[TurnFlowCoordinator] OpenNewInputWindow: Not authority, ignoring"));
		return;
	}

	++InputWindowId;

	UE_LOG(LogTurnFlow, Log,
		TEXT("[TFC] OpenNewInputWindow: TurnId=%d, WindowId=%d"),
		CurrentTurnId, InputWindowId);
}
