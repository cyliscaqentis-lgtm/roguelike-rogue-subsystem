// PlayerInputProcessor.cpp

#include "PlayerInputProcessor.h"
#include "Turn/TurnFlowCoordinator.h"
#include "Net/UnrealNetwork.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/PlayerController.h"
#include "Utility/RogueGameplayTags.h"

void UPlayerInputProcessor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UPlayerInputProcessor, bWaitingForPlayerInput);
}

void UPlayerInputProcessor::OpenInputWindow(int32 TurnId)
{
	UWorld* World = GetWorld();
	if (!World || !World->GetAuthGameMode())
	{
		return; // サーバーのみ実行
	}

	CurrentInputWindowTurnId = TurnId;
	bWaitingForPlayerInput = true;
	ApplyWaitInputGate(true);

	// TurnFlowCoordinatorのInputWindowIdを更新
	if (UTurnFlowCoordinator* TFC = World->GetSubsystem<UTurnFlowCoordinator>())
	{
		// InputWindowIdはTurnFlowCoordinatorで管理される
		// ここでは入力受付開始のみを処理
	}

	UE_LOG(LogTemp, Log, TEXT("[PlayerInputProcessor] Input window opened for TurnId=%d"), TurnId);
}

void UPlayerInputProcessor::CloseInputWindow()
{
	UWorld* World = GetWorld();
	if (!World || !World->GetAuthGameMode())
	{
		return; // サーバーのみ実行
	}

	bWaitingForPlayerInput = false;
	ApplyWaitInputGate(false);

	UE_LOG(LogTemp, Log, TEXT("[PlayerInputProcessor] Input window closed"));
}

bool UPlayerInputProcessor::IsInputOpen_Server() const
{
	UWorld* World = GetWorld();
	if (!World || !World->GetAuthGameMode())
	{
		return false;
	}

	return bWaitingForPlayerInput;
}

bool UPlayerInputProcessor::ValidateCommand(const FPlayerCommand& Command, int32 ExpectedWindowId)
{
	if (Command.WindowId != ExpectedWindowId)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[PlayerInputProcessor] Command WindowId mismatch: Expected=%d, Got=%d"),
			ExpectedWindowId, Command.WindowId);
		return false;
	}

	return true;
}

void UPlayerInputProcessor::ProcessPlayerCommand(const FPlayerCommand& Command)
{
	UWorld* World = GetWorld();
	if (!World || !World->GetAuthGameMode())
	{
		return; // サーバーのみ実行
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
	if (!World || !World->GetAuthGameMode())
	{
		return; // サーバーのみ実行
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
