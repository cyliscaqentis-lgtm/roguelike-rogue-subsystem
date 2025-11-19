// TurnCommandHandler.cpp
#include "Turn/TurnCommandHandler.h"
#include "Player/PlayerControllerBase.h"
#include "Utility/RogueGameplayTags.h"
#include "Character/UnitBase.h"
#include "Grid/GridOccupancySubsystem.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
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

	if (Command.CommandTag.MatchesTag(RogueGameplayTags::Command_Player_Attack) || Command.CommandTag.MatchesTag(RogueGameplayTags::InputTag_Attack))
	{
		// ☁E�E☁E改訂: Attack abilities are triggered via GameplayEvent.Intent.Attack
		AActor* TargetActor = Command.TargetActor.Get();
		if (!TargetActor)
		{
			if (UGridOccupancySubsystem* Occupancy = GetWorld()->GetSubsystem<UGridOccupancySubsystem>())
			{
				TargetActor = Occupancy->GetActorAtCell(Command.TargetCell);
			}
		}

		if (!TargetActor)
		{
			UE_LOG(LogTurnManager, Warning,
				TEXT("[TurnCommandHandler] Attack command failed: No valid target at cell (%d,%d)"),
				Command.TargetCell.X, Command.TargetCell.Y);
			return false;
		}

		FGameplayEventData AttackEventData;
		AttackEventData.EventTag = RogueGameplayTags::GameplayEvent_Intent_Attack;
		AttackEventData.Instigator = PlayerPawn;
		AttackEventData.Target = TargetActor;
		AttackEventData.OptionalObject = this;

		FGameplayAbilityTargetData_SingleTargetHit* TargetData = new FGameplayAbilityTargetData_SingleTargetHit();
		FHitResult HitResult;
		HitResult.HitObjectHandle = FActorInstanceHandle(TargetActor);
		HitResult.Location = TargetActor->GetActorLocation();
		HitResult.ImpactPoint = HitResult.Location;
		TargetData->HitResult = HitResult;

		AttackEventData.TargetData.Add(TargetData);

		UE_LOG(LogTurnManager, Log,
			TEXT("[TurnCommandHandler] Sending GameplayEvent '%s' to trigger attack on '%s'"),
			*AttackEventData.EventTag.ToString(), *GetNameSafe(TargetActor));

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
