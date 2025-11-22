// CodeRevision: INC-2025-1207-R1 (Extract core turn initialization from GameTurnManagerBase into UTurnSystemInitializer) (2025-11-22 00:40)

#include "Turn/TurnSystemInitializer.h"

#include "Turn/GameTurnManagerBase.h"
#include "Turn/TurnActionBarrierSubsystem.h"
#include "Turn/TurnCommandHandler.h"
#include "Turn/TurnDebugSubsystem.h"
#include "Turn/TurnFlowCoordinator.h"
#include "Turn/PlayerInputProcessor.h"
#include "Turn/AttackPhaseExecutorSubsystem.h"
#include "Turn/UnitTurnStateSubsystem.h"
#include "AI/Enemy/EnemyAISubsystem.h"
#include "AI/Enemy/EnemyTurnDataSubsystem.h"
#include "Grid/GridPathfindingSubsystem.h"
#include "Grid/GridOccupancySubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "GameFramework/SpectatorPawn.h"

bool UTurnSystemInitializer::InitializeTurnSystem(AGameTurnManagerBase* TurnManager)
{
	if (!TurnManager)
	{
		return false;
	}

	UWorld* World = TurnManager->GetWorld();
	if (!World)
	{
		UE_LOG(LogTurnManager, Error, TEXT("InitializeTurnSystem: World is null"));
		return false;
	}

	// 1) Begin current turn on the barrier so move/attack registration uses a valid TurnId.
	if (UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>())
	{
		Barrier->BeginTurn(TurnManager->CurrentTurnId);
		UE_LOG(LogTurnManager, Log,
			TEXT("[Turn %d] InitializeTurnSystem: Barrier::BeginTurn(%d) called"),
			TurnManager->CurrentTurnId, TurnManager->CurrentTurnId);
	}

	// 2) Resolve core subsystems used by GameTurnManagerBase.
	TurnManager->CommandHandler = World->GetSubsystem<UTurnCommandHandler>();
	TurnManager->DebugSubsystem = World->GetSubsystem<UTurnDebugSubsystem>();
	TurnManager->TurnFlowCoordinator = World->GetSubsystem<UTurnFlowCoordinator>();
	TurnManager->PlayerInputProcessor = World->GetSubsystem<UPlayerInputProcessor>();

	if (!TurnManager->CommandHandler)
	{
		UE_LOG(LogTurnManager, Error, TEXT("InitializeTurnSystem: Failed to get UTurnCommandHandler subsystem"));
	}
	if (!TurnManager->DebugSubsystem)
	{
		UE_LOG(LogTurnManager, Error, TEXT("InitializeTurnSystem: Failed to get UTurnDebugSubsystem subsystem"));
	}
	if (!TurnManager->TurnFlowCoordinator)
	{
		UE_LOG(LogTurnManager, Error, TEXT("InitializeTurnSystem: Failed to get UTurnFlowCoordinator subsystem"));
	}
	if (!TurnManager->PlayerInputProcessor)
	{
		UE_LOG(LogTurnManager, Error, TEXT("InitializeTurnSystem: Failed to get UPlayerInputProcessor subsystem"));
	}

	TurnManager->EnemyAISubsystem = World->GetSubsystem<UEnemyAISubsystem>();
	TurnManager->UnitTurnStateSubsystem = World->GetSubsystem<UUnitTurnStateSubsystem>();

	if (!TurnManager->EnemyAISubsystem)
	{
		UE_LOG(LogTurnManager, Error, TEXT("InitializeTurnSystem: Failed to get EnemyAISubsystem!"));
		return false;
	}

	if (!TurnManager->UnitTurnStateSubsystem)
	{
		UE_LOG(LogTurnManager, Error, TEXT("InitializeTurnSystem: Failed to get UnitTurnStateSubsystem!"));
		return false;
	}

	TurnManager->UnitTurnStateSubsystem->ResetTurn(TurnManager->CurrentTurnId);

	UE_LOG(LogTurnManager, Log, TEXT("InitializeTurnSystem: Subsystems initialized: CommandHandler=%s, DebugSubsystem=%s, TurnFlowCoordinator=%s, PlayerInputProcessor=%s, UnitTurnState=%s"),
		TurnManager->CommandHandler ? TEXT("OK") : TEXT("FAIL"),
		TurnManager->DebugSubsystem ? TEXT("OK") : TEXT("FAIL"),
		TurnManager->TurnFlowCoordinator ? TEXT("OK") : TEXT("FAIL"),
		TurnManager->PlayerInputProcessor ? TEXT("OK") : TEXT("FAIL"),
		TurnManager->UnitTurnStateSubsystem ? TEXT("OK") : TEXT("FAIL"));

	// 3) Initialize the player pawn (including SpectatorPawn → BPPlayerUnit possession fallback).
	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0);
	if (PlayerPawn)
	{
		UE_LOG(LogTurnManager, Log, TEXT("InitializeTurnSystem: PlayerPawn found: %s"), *PlayerPawn->GetName());

		if (PlayerPawn->IsA(ASpectatorPawn::StaticClass()))
		{
			UE_LOG(LogTurnManager, Warning, TEXT("InitializeTurnSystem: SpectatorPawn detected, searching for BPPlayerUnit..."));
			TArray<AActor*> FoundActors;
			UGameplayStatics::GetAllActorsOfClass(World, APawn::StaticClass(), FoundActors);
			for (AActor* Actor : FoundActors)
			{
				if (Actor && Actor->GetName().Contains(TEXT("BPPlayerUnit_C")))
				{
					if (APawn* PlayerUnit = Cast<APawn>(Actor))
					{
						if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
						{
							PC->Possess(PlayerUnit);
							PlayerPawn = PlayerUnit;
							UE_LOG(LogTurnManager, Warning, TEXT("InitializeTurnSystem: Possessed PlayerUnit: %s"), *PlayerUnit->GetName());
							break;
						}
					}
				}
			}

			if (PlayerPawn->IsA(ASpectatorPawn::StaticClass()))
			{
				UE_LOG(LogTurnManager, Warning, TEXT("InitializeTurnSystem: Could not find/possess BPPlayerUnit (may be possessed later)"));
			}
		}
	}
	else
	{
		UE_LOG(LogTurnManager, Error, TEXT("InitializeTurnSystem: Failed to get PlayerPawn"));
	}

	if (TurnManager->UnitTurnStateSubsystem)
	{
		TurnManager->UnitTurnStateSubsystem->UpdatePlayerPawn(PlayerPawn);
	}

	// 4) Ensure PathFinder is available.
	if (!TurnManager->PathFinder)
	{
		UE_LOG(LogTurnManager, Warning, TEXT("InitializeTurnSystem: PathFinder not injected, attempting resolve..."));
		if (UTurnInitializationSubsystem* InitSys = World->GetSubsystem<UTurnInitializationSubsystem>())
		{
			InitSys->ResolveOrSpawnPathFinder(TurnManager);
		}
	}

	if (!TurnManager->PathFinder)
	{
		UE_LOG(LogTurnManager, Error, TEXT("InitializeTurnSystem: PathFinder not available after resolve"));
		return false;
	}

	UE_LOG(LogTurnManager, Log, TEXT("InitializeTurnSystem: PathFinder ready: %s"), *GetNameSafe(TurnManager->PathFinder));

	// 5) Collect initial enemies.
	TurnManager->RefreshEnemyRoster(PlayerPawn, TurnManager->CurrentTurnId, TEXT("InitializeTurnSystem"));
	const int32 EnemyCount = TurnManager->UnitTurnStateSubsystem ? TurnManager->UnitTurnStateSubsystem->GetCachedEnemyRefs().Num() : 0;
	UE_LOG(LogTurnManager, Log, TEXT("InitializeTurnSystem: CollectEnemies completed (%d enemies)"), EnemyCount);

	// 6) Resolve EnemyTurnData subsystem.
	if (UEnemyTurnDataSubsystem* EnemyTurnDataSys = World->GetSubsystem<UEnemyTurnDataSubsystem>())
	{
		UE_LOG(LogTurnManager, Log, TEXT("InitializeTurnSystem: Subsystems initialized (EnemyAI + EnemyTurnData)"));
	}
	else
	{
		UE_LOG(LogTurnManager, Error, TEXT("InitializeTurnSystem: Failed to get EnemyTurnDataSubsystem!"));
		return false;
	}

	// 7) Bind Barrier and AttackExecutor delegates.
	if (UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>())
	{
		Barrier->OnAllMovesFinished.RemoveDynamic(TurnManager, &AGameTurnManagerBase::HandleMovePhaseCompleted);
		Barrier->OnAllMovesFinished.AddDynamic(TurnManager, &AGameTurnManagerBase::HandleMovePhaseCompleted);
		UE_LOG(LogTurnManager, Log, TEXT("InitializeTurnSystem: Barrier delegate bound to HandleMovePhaseCompleted"));

		if (UAttackPhaseExecutorSubsystem* ExecutorSubsystem = World->GetSubsystem<UAttackPhaseExecutorSubsystem>())
		{
			TurnManager->AttackExecutor = ExecutorSubsystem;
			TurnManager->AttackExecutor->OnFinished.RemoveDynamic(TurnManager, &AGameTurnManagerBase::HandleAttackPhaseCompleted);
			TurnManager->AttackExecutor->OnFinished.AddDynamic(TurnManager, &AGameTurnManagerBase::HandleAttackPhaseCompleted);
			UE_LOG(LogTurnManager, Log, TEXT("InitializeTurnSystem: AttackExecutor delegate bound"));
		}
		else
		{
			UE_LOG(LogTurnManager, Warning, TEXT("InitializeTurnSystem: AttackPhaseExecutorSubsystem not found"));
		}
	}
	else
	{
		if (TurnManager->SubsystemRetryCount < 3)
		{
			TurnManager->SubsystemRetryCount++;
			UE_LOG(LogTurnManager, Warning, TEXT("InitializeTurnSystem: Barrier not found, retrying... (%d/3)"), TurnManager->SubsystemRetryCount);
			TurnManager->bHasInitialized = false;

			World->GetTimerManager().SetTimer(TurnManager->SubsystemRetryHandle, TurnManager, &AGameTurnManagerBase::InitializeTurnSystem, 0.5f, false);
			return false;
		}
		else
		{
			UE_LOG(LogTurnManager, Error, TEXT("InitializeTurnSystem: TurnActionBarrierSubsystem not found after 3 retries"));
		}
	}

	// 8) Bind TurnAbilityCompleted handler on the player ASC.
	if (UAbilitySystemComponent* ASC = TurnManager->GetPlayerASC())
	{
		if (TurnManager->PlayerMoveCompletedHandle.IsValid())
		{
			if (FGameplayEventMulticastDelegate* Delegate = ASC->GenericGameplayEventCallbacks.Find(TurnManager->Tag_TurnAbilityCompleted))
			{
				Delegate->Remove(TurnManager->PlayerMoveCompletedHandle);
			}
			TurnManager->PlayerMoveCompletedHandle.Reset();
		}

		FGameplayEventMulticastDelegate& Delegate = ASC->GenericGameplayEventCallbacks.FindOrAdd(TurnManager->Tag_TurnAbilityCompleted);
		TurnManager->PlayerMoveCompletedHandle = Delegate.AddUObject(TurnManager, &AGameTurnManagerBase::OnPlayerMoveCompleted);

		UE_LOG(LogTurnManager, Log, TEXT("InitializeTurnSystem: Bound to Gameplay.Event.Turn.Ability.Completed event"));
	}
	else
	{
		UE_LOG(LogTurnManager, Error, TEXT("InitializeTurnSystem: GetPlayerASC() returned null"));
		return false;
	}

	UE_LOG(LogTurnManager, Log, TEXT("InitializeTurnSystem: Initialization completed successfully"));
	return true;
}

