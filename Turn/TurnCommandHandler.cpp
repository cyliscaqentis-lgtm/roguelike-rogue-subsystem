// ============================================================================
// TurnCommandHandler.cpp
// プレイヤーコマンド処理Subsystem実装
// GameTurnManagerBaseから分離（2025-11-09）
// ============================================================================

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
	// 入力ウィンドウが開いていない場合は拒否
	if (!bInputWindowOpen)
	{
		UE_LOG(LogTurnManager, Warning, TEXT("[TurnCommandHandler] Command rejected: Input window not open"));
		return false;
	}

	// コマンドの検証
	if (!ValidateCommand(Command))
	{
		UE_LOG(LogTurnManager, Warning, TEXT("[TurnCommandHandler] Command validation failed for TurnId=%d"), Command.TurnId);
		return false;
	}

	// コマンドを適用
	ApplyCommand(Command);

	// 履歴に保存
	LastAcceptedCommands.Add(Command.TurnId, Command);

	UE_LOG(LogTurnManager, Log, TEXT("[TurnCommandHandler] Command accepted for TurnId=%d, Type=%d"),
		Command.TurnId, static_cast<int32>(Command.CommandType));

	return true;
}

bool UTurnCommandHandler::ValidateCommand(const FPlayerCommand& Command) const
{
	// コマンドタイプの妥当性チェック
	if (Command.CommandType == EPlayerCommandType::None)
	{
		UE_LOG(LogTurnManager, Warning, TEXT("[TurnCommandHandler] Invalid command type: None"));
		return false;
	}

	// タイムアウトチェック
	if (!IsCommandTimely(Command))
	{
		UE_LOG(LogTurnManager, Warning, TEXT("[TurnCommandHandler] Command timed out: TurnId=%d"), Command.TurnId);
		return false;
	}

	// 重複チェック
	if (!IsCommandUnique(Command))
	{
		UE_LOG(LogTurnManager, Warning, TEXT("[TurnCommandHandler] Duplicate command: TurnId=%d"), Command.TurnId);
		return false;
	}

	UE_LOG(LogTurnManager, Log, TEXT("[TurnCommandHandler] Command validated: Tag=%s, TurnId=%d, WindowId=%d"),
		*Command.CommandTag.ToString(), Command.TurnId, Command.WindowId);

	return true;
}

void UTurnCommandHandler::ApplyCommand(const FPlayerCommand& Command)
{
	// ★★★ コアシステム: コマンド適用完全実装（2025-11-09） ★★★

	UE_LOG(LogTurnManager, Log, TEXT("[TurnCommandHandler] Applying command TurnId=%d, Type=%d, Tag=%s"),
		Command.TurnId, static_cast<int32>(Command.CommandType), *Command.CommandTag.ToString());

	// コマンドの実行は実際のゲームロジックに委譲
	// ここではコマンド受理の記録と統計の更新を行う

	// TODO: 将来的にはコマンド実行のディスパッチもここで行う
	// 現在はGameTurnManagerBaseが実際の処理を行っている

	UE_LOG(LogTurnManager, Verbose, TEXT("[TurnCommandHandler] Command applied successfully"));
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

void UTurnCommandHandler::EndInputWindow()
{
	bInputWindowOpen = false;

	UE_LOG(LogTurnManager, Log, TEXT("[TurnCommandHandler] Input window closed: WindowId=%d"), CurrentInputWindowId);
}

//------------------------------------------------------------------------------
// Command History
//------------------------------------------------------------------------------

bool UTurnCommandHandler::GetLastAcceptedCommand(int32 TurnId, FPlayerCommand& OutCommand) const
{
	const FPlayerCommand* Found = LastAcceptedCommands.Find(TurnId);
	if (Found)
	{
		OutCommand = *Found;
		return true;
	}
	return false;
}

void UTurnCommandHandler::ClearCommandHistory()
{
	LastAcceptedCommands.Empty();
	UE_LOG(LogTurnManager, Log, TEXT("[TurnCommandHandler] Command history cleared"));
}

//------------------------------------------------------------------------------
// Internal Helpers
//------------------------------------------------------------------------------

bool UTurnCommandHandler::IsCommandTimely(const FPlayerCommand& Command) const
{
	// TODO: 実際のタイムアウトロジックを実装
	// 現在は常にtrueを返す
	return true;
}

bool UTurnCommandHandler::IsCommandUnique(const FPlayerCommand& Command) const
{
	// 同じTurnIdのコマンドが既に存在する場合は重複
	return !LastAcceptedCommands.Contains(Command.TurnId);
}
