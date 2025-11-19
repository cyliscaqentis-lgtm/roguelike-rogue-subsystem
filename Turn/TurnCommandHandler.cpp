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
			// Fallback: Use Actor's physical facing
			const FVector ForwardVector = PlayerUnit->GetActorForwardVector();
			const FVector2D ForwardDir2D = FVector2D(ForwardVector.X, ForwardVector.Y).GetSafeNormal();
			FIntPoint Direction = FIntPoint(FMath::RoundToInt(ForwardDir2D.X), FMath::RoundToInt(ForwardDir2D.Y));
			if (Direction == FIntPoint::ZeroValue)
			{
				Direction = FIntPoint(1, 0); // Default to +X
			}
			TargetCell = CurrentCell + Direction;
			
			UE_LOG(LogTurnManager, Log,
				TEXT("[TurnCommandHandler] Using Actor ForwardVector for Attack Direction -> (%d,%d)"),
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

		return true;
	}
	else if (Command.CommandTag.MatchesTag(RogueGameplayTags::InputTag_Move))
	{
		UE_LOG(LogTurnManager, Log, TEXT("[TurnCommandHandler] Move command received, passing through for now."));
		return true;
	}

	return false;
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
