// TurnCommandHandler.cpp
#include "Turn/TurnCommandHandler.h"
#include "Player/PlayerControllerBase.h"
#include "../ProjectDiagnostics.h"

//------------------------------------------------------------------------------
// Subsystem Lifecycle
//------------------------------------------------------------------------------

void UTurnCommandHandler::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	CurrentInputWindowId = 0;
	bInputWindowOpen = false;
	LastAcceptedCommands.Empty();

	UE_LOG(LogTurnManager, Log, TEXT("[TurnCommandHandler] Initialized"));
}

void UTurnCommandHandler::Deinitialize()
{
	LastAcceptedCommands.Empty();

	UE_LOG(LogTurnManager, Log, TEXT("[TurnCommandHandler] Deinitialized"));

	Super::Deinitialize();
}

//------------------------------------------------------------------------------
// Command Processing
//------------------------------------------------------------------------------

bool UTurnCommandHandler::ProcessPlayerCommand(const FPlayerCommand& Command)
{
	if (!bInputWindowOpen)
	{
		UE_LOG(LogTurnManager, Warning, TEXT("[TurnCommandHandler] Command rejected: Input window not open"));
		return false;
	}

	if (!ValidateCommand(Command))
	{
		UE_LOG(LogTurnManager, Warning, TEXT("[TurnCommandHandler] Command validation failed for TurnId=%d"), Command.TurnId);
		return false;
	}

	UE_LOG(LogTurnManager, Log, TEXT("[TurnCommandHandler] Command validated (not yet marked as accepted): TurnId=%d, Tag=%s"),
		Command.TurnId, *Command.CommandTag.ToString());

	return true;
}

bool UTurnCommandHandler::ValidateCommand(const FPlayerCommand& Command) const
{
	if (!Command.CommandTag.IsValid())
	{
		UE_LOG(LogTurnManager, Warning, TEXT("[TurnCommandHandler] Invalid command tag"));
		return false;
	}

	if (!IsCommandTimely(Command))
	{
		UE_LOG(LogTurnManager, Warning, TEXT("[TurnCommandHandler] Command timed out: TurnId=%d"), Command.TurnId);
		return false;
	}

	if (!IsCommandUnique(Command))
	{
		UE_LOG(LogTurnManager, Warning, TEXT("[TurnCommandHandler] Duplicate command: TurnId=%d"), Command.TurnId);
		return false;
	}

	UE_LOG(LogTurnManager, Log, TEXT("[TurnCommandHandler] Command validated: Tag=%s, TurnId=%d, WindowId=%d"),
		*Command.CommandTag.ToString(), Command.TurnId, Command.WindowId);

	return true;
}

void UTurnCommandHandler::MarkCommandAsAccepted(const FPlayerCommand& Command)
{
	LastAcceptedCommands.Add(Command.TurnId, Command);

	UE_LOG(LogTurnManager, Log, TEXT("[TurnCommandHandler] ★ Command MARKED AS ACCEPTED: TurnId=%d, Tag=%s"),
		Command.TurnId, *Command.CommandTag.ToString());
}

//------------------------------------------------------------------------------
// Input Window Management
//------------------------------------------------------------------------------

void UTurnCommandHandler::BeginInputWindow(int32 WindowId)
{
	CurrentInputWindowId = WindowId;
	bInputWindowOpen = true;

	UE_LOG(LogTurnManager, Log, TEXT("[TurnCommandHandler] Input window opened: WindowId=%d"), WindowId);
}

//------------------------------------------------------------------------------
// Internal Helpers
//------------------------------------------------------------------------------

bool UTurnCommandHandler::IsCommandTimely(const FPlayerCommand& Command) const
{
	return true;
}

bool UTurnCommandHandler::IsCommandUnique(const FPlayerCommand& Command) const
{
	return !LastAcceptedCommands.Contains(Command.TurnId);
}
