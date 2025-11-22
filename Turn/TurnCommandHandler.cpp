// TurnCommandHandler.cpp
#include "Turn/TurnCommandHandler.h"
#include "Player/PlayerControllerBase.h"
#include "Utility/RogueGameplayTags.h"
#include "Character/UnitBase.h"
#include "Grid/GridOccupancySubsystem.h"
#include "Grid/GridPathfindingSubsystem.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Utility/TurnCommandEncoding.h"
#include "Kismet/GameplayStatics.h"
#include "../Utility/ProjectDiagnostics.h"
#include "Engine/ActorInstanceHandle.h"
#include "Turn/TurnFlowCoordinator.h"
#include "Turn/GameTurnManagerBase.h"
#include "Turn/MoveReservationSubsystem.h"
#include "AI/Enemy/EnemyTurnDataSubsystem.h"

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

	// CodeRevision: INC-2025-1134-R1 (Execute validated commands directly in handler) (2025-12-13 09:30)
	UE_LOG(LogTurnManager, Log,
		TEXT("[TurnCommandHandler] Command received and validated: TurnId=%d, Tag=%s, TargetCell=(%d,%d)"),
		Command.TurnId, *Command.CommandTag.ToString(), Command.TargetCell.X, Command.TargetCell.Y);

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	AUnitBase* PlayerUnit = Cast<AUnitBase>(PlayerPawn);
	if (!PlayerUnit)
	{
		UE_LOG(LogTurnManager, Error, TEXT("[TurnCommandHandler] Could not find Player Unit to execute command."));
		return false;
	}

	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(PlayerUnit);
	if (!ASC)
	{
		UE_LOG(LogTurnManager, Error, TEXT("[TurnCommandHandler] Player Unit has no AbilitySystemComponent."));
		return false;
	}

	// CodeRevision: INC-2025-1136-R1 (Final attack dispatch uses front-cell GameplayEvent) (2025-12-13 10:00)
	if (Command.CommandTag.MatchesTag(RogueGameplayTags::Command_Player_Attack) || Command.CommandTag.MatchesTag(RogueGameplayTags::InputTag_Attack))
	{
		// Attack abilities are triggered via GameplayEvent.Intent.Attack
		UGridPathfindingSubsystem* PathFinder = GetWorld()->GetSubsystem<UGridPathfindingSubsystem>();
		UGridOccupancySubsystem* Occupancy = GetWorld()->GetSubsystem<UGridOccupancySubsystem>();
		if (!PathFinder || !Occupancy)
		{
			UE_LOG(LogTurnManager, Error, TEXT("[TurnCommandHandler] PathFinder or Occupancy subsystem not found."));
			return false;
		}

		const FIntPoint CurrentCell = PathFinder->WorldToGrid(PlayerUnit->GetActorLocation());
		FIntPoint TargetCell = FIntPoint::ZeroValue;
		bool bTargetFound = false;

		// Strategy: Determine "Front" based on Input
		// 1. If Command.TargetCell is provided (e.g. Mouse Cursor Position), use direction towards it.
		// 2. If not, fallback to Actor's ForwardVector.

		if (Command.TargetCell != FIntPoint::ZeroValue && Command.TargetCell != FIntPoint(-1, -1) && Command.TargetCell != CurrentCell)
		{
			// Calculate direction from Player to Command Target
			FVector2D DirectionDir = FVector2D(Command.TargetCell.X - CurrentCell.X, Command.TargetCell.Y - CurrentCell.Y);
			if (!DirectionDir.IsNearlyZero())
			{
				DirectionDir.Normalize();
				FIntPoint Direction = FIntPoint(FMath::RoundToInt(DirectionDir.X), FMath::RoundToInt(DirectionDir.Y));
				
				// Ensure we have a valid direction (at least one non-zero component)
				if (Direction != FIntPoint::ZeroValue)
				{
					TargetCell = CurrentCell + Direction;
					bTargetFound = true;
					
					UE_LOG(LogTurnManager, Log,
						TEXT("[TurnCommandHandler] Input Direction derived from TargetCell (%d,%d) -> Dir(%d,%d) -> AttackCell(%d,%d)"),
						Command.TargetCell.X, Command.TargetCell.Y, Direction.X, Direction.Y, TargetCell.X, TargetCell.Y);
				}
			}
		}

		if (!bTargetFound)
		{
			// Fallback: Use Command.Direction (set by Input_TurnFacing) or Actor's ForwardVector
			FVector AttackDirection = Command.Direction;
			
			// If Command.Direction is not set or zero, use ForwardVector
			if (AttackDirection.IsNearlyZero())
			{
				AttackDirection = PlayerUnit->GetActorForwardVector();
				UE_LOG(LogTurnManager, Log,
					TEXT("[TurnCommandHandler] Command.Direction is zero, using ForwardVector"));
			}
			else
			{
				UE_LOG(LogTurnManager, Log,
					TEXT("[TurnCommandHandler] Using Command.Direction from Input_TurnFacing (%.2f, %.2f)"),
					AttackDirection.X, AttackDirection.Y);
			}
			
			const FVector2D Direction2D = FVector2D(AttackDirection.X, AttackDirection.Y).GetSafeNormal();
			FIntPoint Direction = FIntPoint(FMath::RoundToInt(Direction2D.X), FMath::RoundToInt(Direction2D.Y));
			if (Direction == FIntPoint::ZeroValue)
			{
				Direction = FIntPoint(1, 0); // Default to +X
			}
			TargetCell = CurrentCell + Direction;
			
			UE_LOG(LogTurnManager, Log,
				TEXT("[TurnCommandHandler] Attack Direction -> (%d,%d)"),
				TargetCell.X, TargetCell.Y);
		}

		AActor* TargetActor = Occupancy->GetActorAtCell(TargetCell);
		if (TargetActor == PlayerUnit || (TargetActor && !TargetActor->IsA(AUnitBase::StaticClass())))
		{
			TargetActor = nullptr;
		}

		FGameplayEventData AttackEventData;
		AttackEventData.EventTag = RogueGameplayTags::GameplayEvent_Intent_Attack;
		AttackEventData.Instigator = PlayerPawn;
		AttackEventData.Target = TargetActor;
		AttackEventData.OptionalObject = this;
		AttackEventData.EventMagnitude = static_cast<float>(TurnCommandEncoding::PackCell(TargetCell.X, TargetCell.Y));

		FGameplayAbilityTargetData_SingleTargetHit* TargetData = new FGameplayAbilityTargetData_SingleTargetHit();
		TargetData->HitResult.Location = PathFinder->GridToWorld(TargetCell, PlayerPawn->GetActorLocation().Z);
		TargetData->HitResult.ImpactPoint = TargetData->HitResult.Location;
		if (TargetActor)
		{
			TargetData->HitResult.HitObjectHandle = FActorInstanceHandle(TargetActor);
		}
		AttackEventData.TargetData.Add(TargetData);

		UE_LOG(LogTurnManager, Log,
			TEXT("[TurnCommandHandler] Sending GameplayEvent '%s' to trigger attack. TargetActor: '%s', TargetCell: (%d,%d)"),
			*AttackEventData.EventTag.ToString(),
			*GetNameSafe(TargetActor),
			TargetCell.X, TargetCell.Y);

		ASC->HandleGameplayEvent(AttackEventData.EventTag, &AttackEventData);

		// CodeRevision: INC-2025-1122-SIMUL-R4 (Trigger enemy phase for attack commands too) (2025-11-22)
		// Attack commands also need to trigger enemy phase for simultaneous movement
		// Player is not moving, so no reservation needed - use current position for intent generation
		{
			AGameTurnManagerBase* TurnManager = nullptr;
			TArray<AActor*> FoundActors;
			UGameplayStatics::GetAllActorsOfClass(GetWorld(), AGameTurnManagerBase::StaticClass(), FoundActors);
			if (FoundActors.Num() > 0)
			{
				TurnManager = Cast<AGameTurnManagerBase>(FoundActors[0]);
			}

			if (TurnManager)
			{
				UE_LOG(LogTurnManager, Log, TEXT("[TurnCommandHandler] Player attack - triggering enemy phase for simultaneous movement."));
				TurnManager->ExecuteEnemyPhase();
			}
		}

		return true;
	}
	// CodeRevision: INC-2025-1145-R1 (Treat move commands as validated so GameTurnManagerBase can run MovePrecheck and ACK instead of dropping input) (2025-11-20 14:00)
	else if (Command.CommandTag.MatchesTag(RogueGameplayTags::InputTag_Move))
	{
		// Delegate to TryExecuteMoveCommand
		return TryExecuteMoveCommand(Command);
	}

	return false;
}

bool UTurnCommandHandler::TryExecuteMoveCommand(const FPlayerCommand& Command)
{
	UWorld* World = GetWorld();
	if (!World) return false;

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0);
	if (!PlayerPawn) return false;

	UGridPathfindingSubsystem* PathFinder = World->GetSubsystem<UGridPathfindingSubsystem>();
	if (!PathFinder) return false;

	UTurnFlowCoordinator* TurnFlowCoordinator = World->GetSubsystem<UTurnFlowCoordinator>();

	const int32 DirX = FMath::RoundToInt(Command.Direction.X);
	const int32 DirY = FMath::RoundToInt(Command.Direction.Y);

	// Calculate target cell before packing
	FIntPoint TargetCell = Command.TargetCell;
	if (TargetCell.X == 0 && TargetCell.Y == 0)
	{
		const FIntPoint CurrentCell = PathFinder->WorldToGrid(PlayerPawn->GetActorLocation());
		TargetCell = FIntPoint(CurrentCell.X + DirX, CurrentCell.Y + DirY);
	}

	const FIntPoint CurrentCell = PathFinder->WorldToGrid(PlayerPawn->GetActorLocation());
	
	// Validation
	FString FailureReason;
	const bool bMoveValid = PathFinder->IsMoveValid(CurrentCell, TargetCell, PlayerPawn, FailureReason);

	// Swap detection
	bool bSwapDetected = false;
	if (!bMoveValid && FailureReason.Contains(TEXT("occupied")))
	{
		if (UGridOccupancySubsystem* OccSys = World->GetSubsystem<UGridOccupancySubsystem>())
		{
			AActor* OccupyingActor = OccSys->GetActorAtCell(TargetCell);
			if (OccupyingActor && OccupyingActor != PlayerPawn)
			{
				if (UEnemyTurnDataSubsystem* EnemyTurnDataSys = World->GetSubsystem<UEnemyTurnDataSubsystem>())
				{
					for (const FEnemyIntent& Intent : EnemyTurnDataSys->Intents)
					{
						if (Intent.Actor.Get() == OccupyingActor && Intent.NextCell == CurrentCell)
						{
							bSwapDetected = true;
							UE_LOG(LogTurnManager, Warning,
								TEXT("[MovePrecheck] SWAP DETECTED: Player (%d,%d)->(%d,%d), Enemy %s (%d,%d)->(%d,%d)"),
								CurrentCell.X, CurrentCell.Y, TargetCell.X, TargetCell.Y,
								*GetNameSafe(OccupyingActor),
								TargetCell.X, TargetCell.Y, Intent.NextCell.X, Intent.NextCell.Y);
							break;
						}
					}
				}
			}
		}
	}

	if (!bMoveValid)
	{
		UE_LOG(LogTurnManager, Warning,
			TEXT("[MovePrecheck] BLOCKED: %s | Cell (%d,%d) | From=(%d,%d) | Applying FACING ONLY (No Turn)"),
			*FailureReason, TargetCell.X, TargetCell.Y, CurrentCell.X, CurrentCell.Y);

		// Apply Facing Only
		const float Yaw = FMath::Atan2(Command.Direction.Y, Command.Direction.X) * 180.f / PI;
		FRotator NewRotation = PlayerPawn->GetActorRotation();
		NewRotation.Yaw = Yaw;
		PlayerPawn->SetActorRotation(NewRotation);

		if (APlayerController* PC = Cast<APlayerController>(PlayerPawn->GetController()))
		{
			if (APlayerControllerBase* TPCB = Cast<APlayerControllerBase>(PC))
			{
				const int32 WindowIdForTurn = TurnFlowCoordinator ? TurnFlowCoordinator->GetCurrentInputWindowId() : 0;
				TPCB->Client_ApplyFacingNoTurn(WindowIdForTurn, FVector2D(Command.Direction.X, Command.Direction.Y));
				TPCB->Client_NotifyMoveRejected();
			}
		}

		return false; // Move rejected, but handled
	}

	// Reservation
	// Note: We need to access GameTurnManagerBase for RegisterResolvedMove if we want to keep using it,
	// OR we should move reservation logic to OccupancySubsystem or here.
	// For now, let's assume we can use OccupancySubsystem directly if possible, or we might need to expose it.
	// However, RegisterResolvedMove is in GTMB.
	// To decouple, we should use GridOccupancySubsystem directly if possible.
	// But GTMB::RegisterResolvedMove does: Occupancy->ReserveCell + adding to ResolvedMoves array.
	// The ResolvedMoves array is used for conflict resolution.
	// If we move this logic here, we need to ensure ConflictResolver can still see it.
	
	// For this step, to avoid breaking too much, I will use GridOccupancySubsystem to reserve,
	// but we might miss the "ResolvedMoves" tracking if that's critical for ConflictResolver.
	// Actually, ConflictResolver uses `ResolvedMoves` from GTMB.
	// So we might need to call back to GTMB or move `ResolvedMoves` to a subsystem (e.g. ConflictResolver itself).
	
	// Let's assume for now we can just reserve on Occupancy.
	// Wait, if I don't add to ResolvedMoves, ConflictResolver won't know about the player's move?
	// The Player's move is usually immediate or part of the batch?
	// In `ExecuteSimultaneousPhase`, it uses `ResolvedMoves`.
	
	// OK, I cannot fully decouple without moving `ResolvedMoves`.
	// But I can at least do the validation here.
	// Let's call back to GTMB for the actual reservation if needed, OR better:
	// Move `ResolvedMoves` to `ConflictResolverSubsystem`?
	// Or just let GTMB handle the reservation part?
	
	// Note: Grid reservation is handled by the move ability itself
	// We just validate that the target cell is walkable

	// If successful:
	MarkCommandAsAccepted(Command);

	// Send ACK
	if (APlayerController* PC = Cast<APlayerController>(PlayerPawn->GetController()))
	{
		if (APlayerControllerBase* TPCB = Cast<APlayerControllerBase>(PC))
		{
			const int32 WindowIdForTurn = TurnFlowCoordinator ? TurnFlowCoordinator->GetCurrentInputWindowId() : 0;
			TPCB->Client_ConfirmCommandAccepted(WindowIdForTurn);
		}
	}

	// Trigger Gameplay Event for Move
	// Get TurnManager to pass in OptionalObject so GA_MoveBase can retrieve TurnId
	AGameTurnManagerBase* TurnManager = nullptr;
	{
		TArray<AActor*> FoundActors;
		UGameplayStatics::GetAllActorsOfClass(World, AGameTurnManagerBase::StaticClass(), FoundActors);
		if (FoundActors.Num() > 0)
		{
			TurnManager = Cast<AGameTurnManagerBase>(FoundActors[0]);
		}
	}

	FGameplayEventData EventData;
	EventData.EventTag = RogueGameplayTags::GameplayEvent_Intent_Move;
	EventData.Instigator = PlayerPawn;
	EventData.Target = PlayerPawn;
	EventData.OptionalObject = TurnManager; // Must be TurnManager, not 'this'!
	EventData.EventMagnitude = static_cast<float>(TurnCommandEncoding::PackCell(TargetCell.X, TargetCell.Y));

	if (!TurnManager)
	{
		UE_LOG(LogTurnManager, Warning,
			TEXT("[TurnCommandHandler] TurnManager not found - GA_MoveBase may not send completion event"));
	}

	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(PlayerPawn);
	if (ASC)
	{
		ASC->HandleGameplayEvent(EventData.EventTag, &EventData);
	}

	// CodeRevision: INC-2025-1122-SIMUL-R1 (Trigger enemy phase immediately for simultaneous movement) (2025-11-22)
	// CodeRevision: INC-2025-1122-SIMUL-R2 (Use GridOccupancySubsystem::GetReservedCellForActor instead of custom variable) (2025-11-22)
	// CodeRevision: INC-2025-1122-SIMUL-R3 (Register player move reservation before ExecuteEnemyPhase via MoveReservationSubsystem) (2025-11-22)
	// Trigger enemy phase immediately after player command is accepted
	// This ensures enemies start moving at the same time as the player (simultaneous movement)
	if (TurnManager)
	{
		// Register player's target cell reservation BEFORE ExecuteEnemyPhase
		// so that GetReservedCellForActor returns the correct target cell for intent generation
		if (UMoveReservationSubsystem* MoveRes = World->GetSubsystem<UMoveReservationSubsystem>())
		{
			const bool bReserved = MoveRes->RegisterResolvedMove(PlayerPawn, TargetCell);
			if (bReserved)
			{
				UE_LOG(LogTurnManager, Log, TEXT("[TurnCommandHandler] Player move reserved (%d,%d) - triggering enemy phase for simultaneous movement."),
					TargetCell.X, TargetCell.Y);
			}
			else
			{
				UE_LOG(LogTurnManager, Warning, TEXT("[TurnCommandHandler] Player move reservation FAILED for (%d,%d) - enemy phase will use current position."),
					TargetCell.X, TargetCell.Y);
			}
		}

		TurnManager->ExecuteEnemyPhase();
	}

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

	UE_LOG(LogTurnManager, Log, TEXT("[TurnCommandHandler] Command marked as accepted: TurnId=%d, Tag=%s"),
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
