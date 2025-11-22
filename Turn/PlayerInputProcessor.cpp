// PlayerInputProcessor.cpp

#include "PlayerInputProcessor.h"
#include "Turn/TurnFlowCoordinator.h"
#include "Net/UnrealNetwork.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/PlayerController.h"
#include "Utility/RogueGameplayTags.h"
#include "Utility/TurnAuthorityUtils.h"
#include "Turn/GameTurnManagerBase.h"
#include "Turn/MoveReservationSubsystem.h"
#include "Turn/TurnCommandHandler.h"
#include "Kismet/GameplayStatics.h"

void UPlayerInputProcessor::OpenTurnInputWindow(AGameTurnManagerBase* Manager, int32 TurnId)
{
	UWorld* World = GetWorld();
	if (!IsAuthorityLike(World))
	{
		return;
	}

	UTurnFlowCoordinator* TFC = World->GetSubsystem<UTurnFlowCoordinator>();
	if (TFC)
	{
		TFC->OpenNewInputWindow();
		const int32 WindowId = TFC->GetCurrentInputWindowId();
		
		// Update internal state
		OpenInputWindow(TurnId, WindowId);

		// Notify CommandHandler
		if (UTurnCommandHandler* CmdHandler = World->GetSubsystem<UTurnCommandHandler>())
		{
			CmdHandler->BeginInputWindow(WindowId);
		}

		// Update Manager state (if needed for replication, though we are moving away from it)
		// Manager->WaitingForPlayerInput = true; // Managed via OnRep/SetWaitingForPlayerInput_ServerLike in Processor? 
		// Actually Manager still has WaitingForPlayerInput replicated property. 
		// For now, we keep Manager's property in sync or rely on Processor's property if clients use it.
		// But Manager::WaitingForPlayerInput is used in many places.
		// Let's assume Manager delegates to us, so we should update our state, and Manager might update its own via callback or we update it here.
		// Since Manager passed 'this', we can update it.
		if (Manager)
		{
			Manager->WaitingForPlayerInput = true;
			Manager->OnRep_WaitingForPlayerInput(); // Manually call OnRep for server
		}
	}
}

void UPlayerInputProcessor::CloseTurnInputWindow(AGameTurnManagerBase* Manager)
{
	CloseInputWindow();
	if (Manager)
	{
		Manager->WaitingForPlayerInput = false;
		Manager->OnRep_WaitingForPlayerInput();
	}
}

void UPlayerInputProcessor::OnPlayerMoveFinalized(AGameTurnManagerBase* Manager, AActor* CompletedActor)
{
	CloseTurnInputWindow(Manager);
	
	// Release reservation
	if (UWorld* World = GetWorld())
	{
		if (UMoveReservationSubsystem* MoveRes = World->GetSubsystem<UMoveReservationSubsystem>())
		{
			MoveRes->ReleaseMoveReservation(CompletedActor);
		}
	}

	// Remove tags
	if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(CompletedActor))
	{
		ASC->RemoveLooseGameplayTag(RogueGameplayTags::Phase_Player_WaitInput);
	}
}

void UPlayerInputProcessor::OnPlayerPossessed(AGameTurnManagerBase* Manager, APawn* NewPawn)
{
	if (!Manager) return;

	// Logic from Manager::NotifyPlayerPossessed
	// We need to access Manager's bTurnStarted, bDeferOpenOnPossess.
	// Since they are public/protected, we might need to be friend or they need to be public.
	// They are public in GameTurnManagerBase.h (checked in Step 294).
	
	if (Manager->bTurnStarted && !Manager->WaitingForPlayerInput)
	{
		OpenTurnInputWindow(Manager, Manager->CurrentTurnId);
		Manager->bDeferOpenOnPossess = false;
	}
	else if (Manager->bTurnStarted && Manager->bDeferOpenOnPossess)
	{
		OpenTurnInputWindow(Manager, Manager->CurrentTurnId);
		Manager->bDeferOpenOnPossess = false;
	}
	else if (!Manager->bTurnStarted)
	{
		Manager->bDeferOpenOnPossess = true;
		Manager->TryStartFirstTurn();
	}
}

bool UPlayerInputProcessor::IsInputOpen_Server(const AGameTurnManagerBase* Manager) const
{
	if (!bWaitingForPlayerInput)
	{
		return false;
	}

	// Check ASC tag
	UWorld* World = GetWorld();
	if (APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr)
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Pawn))
			{
				return ASC->HasMatchingGameplayTag(RogueGameplayTags::Gate_Input_Open);
			}
		}
	}
	return false;
}

void UPlayerInputProcessor::MarkMoveInProgress(bool bInProgress)
{
	UWorld* World = GetWorld();
	if (APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr)
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Pawn))
			{
				if (bInProgress)
				{
					ASC->AddLooseGameplayTag(RogueGameplayTags::State_Action_InProgress);
				}
				else
				{
					ASC->RemoveLooseGameplayTag(RogueGameplayTags::State_Action_InProgress);
				}
			}
		}
	}
}

void UPlayerInputProcessor::SetPlayerMoveState(const FVector& Direction, bool bInProgress)
{
	CachedPlayerCommand.Direction = Direction;
	MarkMoveInProgress(bInProgress);
}

void UPlayerInputProcessor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UPlayerInputProcessor, bWaitingForPlayerInput);
}

void UPlayerInputProcessor::OpenInputWindow(int32 TurnId, int32 WindowId)
{
	UWorld* World = GetWorld();
	if (!IsAuthorityLike(World))
	{
		return;
	}

	CurrentAcceptedTurnId = TurnId;
	CurrentAcceptedWindowId = WindowId;
	bInputWindowOpen = true;

	// レガシー互換用
	CurrentInputWindowTurnId = TurnId;
	bWaitingForPlayerInput = true;

	ApplyWaitInputGate(true);

	UE_LOG(LogTemp, Log,
		TEXT("[PIP] OpenInputWindow: TurnId=%d, WindowId=%d"),
		TurnId, WindowId);
}

void UPlayerInputProcessor::CloseInputWindow()
{
	UWorld* World = GetWorld();
	if (!IsAuthorityLike(World))
	{
		return;
	}

	bInputWindowOpen = false;
	bWaitingForPlayerInput = false;
	ApplyWaitInputGate(false);

	UE_LOG(LogTemp, Log,
		TEXT("[PIP] CloseInputWindow: TurnId=%d, WindowId=%d"),
		CurrentAcceptedTurnId, CurrentAcceptedWindowId);
}

bool UPlayerInputProcessor::IsInputOpen_Server() const
{
	UWorld* World = GetWorld();
	if (!IsAuthorityLike(World))
	{
		return false;
	}

	return bWaitingForPlayerInput;
}

bool UPlayerInputProcessor::ValidateCommand(const FPlayerCommand& Command)
{
	if (!bInputWindowOpen)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[PIP] RejectCommand: InputWindowClosed (Cmd Turn=%d, Window=%d)"),
			Command.TurnId, Command.WindowId);
		return false;
	}

	if (Command.TurnId != CurrentAcceptedTurnId)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[PIP] RejectCommand: TurnId mismatch (Expected=%d, Got=%d)"),
			CurrentAcceptedTurnId, Command.TurnId);
		return false;
	}

	if (Command.WindowId != CurrentAcceptedWindowId)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[PIP] RejectCommand: WindowId mismatch (Expected=%d, Got=%d)"),
			CurrentAcceptedWindowId, Command.WindowId);
		return false;
	}

	return true;
}

void UPlayerInputProcessor::ProcessPlayerCommand(const FPlayerCommand& Command)
{
	UWorld* World = GetWorld();
	if (!IsAuthorityLike(World))
	{
		return;
	}

	// コマンドをキャッシュ
	CachedPlayerCommand = Command;

	UE_LOG(LogTemp, Log,
		TEXT("[PlayerInputProcessor] Command processed: WindowId=%d, Tag=%s"),
		Command.WindowId,
		*Command.CommandTag.ToString());

	// ★★★ CRITICAL FIX (2025-11-11): Gemini分析により判明 ★★★
	// 問題: ProcessPlayerCommand時点で入力ウィンドウを閉じていたが、
	//       これはMovePrecheck前なので、拒否される可能性がある。
	//       拒否されても入力ウィンドウは既に閉じられているため、再入力できない。
	//
	// 修正: 入力ウィンドウを閉じるタイミングをMovePrecheck成功後に移動
	//       - MovePrecheck成功時: GameTurnManagerBase::MovePrecheck()内で閉じる
	//       - MovePrecheck失敗時: 閉じない（再入力を許可）
	//
	// 削除: NotifyPlayerInputReceived();
	//
	// Note: デリゲート通知（BroadcastPlayerInputReceived）も不要になった。
	//       ターン進行はMovePrecheck成功時のバリア機構で管理される。

	UE_LOG(LogTemp, Log,
		TEXT("[PlayerInputProcessor] Command cached (window still open for retry)"));
}

bool UPlayerInputProcessor::IsValidWindowId(int32 WindowId) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	UTurnFlowCoordinator* TFC = World->GetSubsystem<UTurnFlowCoordinator>();
	if (!TFC)
	{
		return false;
	}

	return WindowId == TFC->GetCurrentInputWindowId();
}

void UPlayerInputProcessor::ApplyWaitInputGate(bool bOpen)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// プレイヤーPawnのASCを取得
	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC)
	{
		return;
	}

	APawn* PlayerPawn = PC->GetPawn();
	if (!PlayerPawn)
	{
		return;
	}

	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(PlayerPawn);
	if (!ASC)
	{
		return;
	}

	if (bOpen)
	{
		// ゲート開く：タグ追加
		ASC->AddLooseGameplayTag(RogueGameplayTags::Phase_Player_WaitInput);
		ASC->AddLooseGameplayTag(RogueGameplayTags::Gate_Input_Open);

		UE_LOG(LogTemp, Log, TEXT("[PlayerInputProcessor] Gate OPENED (tags added)"));
	}
	else
	{
		// ゲート閉じる：タグ削除
		ASC->RemoveLooseGameplayTag(RogueGameplayTags::Phase_Player_WaitInput);
		ASC->RemoveLooseGameplayTag(RogueGameplayTags::Gate_Input_Open);

		UE_LOG(LogTemp, Log, TEXT("[PlayerInputProcessor] Gate CLOSED (tags removed)"));
	}
}

void UPlayerInputProcessor::SetWaitingForPlayerInput_ServerLike(bool bNew)
{
	UWorld* World = GetWorld();
	if (!IsAuthorityLike(World))
	{
		return;
	}

	bWaitingForPlayerInput = bNew;
	ApplyWaitInputGate(bNew);
}

void UPlayerInputProcessor::OnRep_WaitingForPlayerInput()
{
	UE_LOG(LogTemp, Log,
		TEXT("[PlayerInputProcessor] OnRep_WaitingForPlayerInput: %s"),
		bWaitingForPlayerInput ? TEXT("OPEN") : TEXT("CLOSED"));
}
