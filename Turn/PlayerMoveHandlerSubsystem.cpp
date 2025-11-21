// CodeRevision: INC-2025-1206-R2 (Extract player move completion logic from GameTurnManagerBase) (2025-11-22 00:22)

#include "Turn/PlayerMoveHandlerSubsystem.h"
#include "Grid/GridPathfindingSubsystem.h"
#include "Grid/GridOccupancySubsystem.h"
#include "Turn/DistanceFieldSubsystem.h"
#include "AI/Enemy/EnemyAISubsystem.h"
#include "AI/Enemy/EnemyTurnDataSubsystem.h"
#include "Turn/TurnSystemTypes.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "AbilitySystemGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogPlayerMove, Log, All);

void UPlayerMoveHandlerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogPlayerMove, Log, TEXT("PlayerMoveHandlerSubsystem initialized"));
}

void UPlayerMoveHandlerSubsystem::Deinitialize()
{
	Super::Deinitialize();
	UE_LOG(LogPlayerMove, Log, TEXT("PlayerMoveHandlerSubsystem deinitialized"));
}

bool UPlayerMoveHandlerSubsystem::ValidateTurnNotification(int32 NotifiedTurn, int32 CurrentTurn) const
{
	if (NotifiedTurn != CurrentTurn)
	{
		UE_LOG(LogPlayerMove, Warning,
			TEXT("IGNORE stale move notification (notified=%d, current=%d)"),
			NotifiedTurn, CurrentTurn);
		return false;
	}
	return true;
}

bool UPlayerMoveHandlerSubsystem::UpdateDistanceFieldForFinalPosition(const FIntPoint& PlayerCell)
{
	UWorld* World = GetWorld();
	if (!World || PlayerCell == FIntPoint(-1, -1))
	{
		UE_LOG(LogPlayerMove, Warning, TEXT("Cannot update DistanceField: Invalid World or PlayerCell"));
		return false;
	}

	UDistanceFieldSubsystem* DF = World->GetSubsystem<UDistanceFieldSubsystem>();
	if (!DF)
	{
		UE_LOG(LogPlayerMove, Error, TEXT("DistanceFieldSubsystem not available"));
		return false;
	}

	DF->UpdateDistanceField(PlayerCell);
	UE_LOG(LogPlayerMove, Log, TEXT("DistanceField updated for player at (%d,%d)"), 
		PlayerCell.X, PlayerCell.Y);
	
	return true;
}

bool UPlayerMoveHandlerSubsystem::UpdateAIKnowledge(
	APawn* PlayerPawn,
	const TArray<AActor*>& Enemies,
	TArray<FEnemyIntent>& OutIntents)
{
	UWorld* World = GetWorld();
	if (!World || !PlayerPawn || Enemies.Num() == 0)
	{
		UE_LOG(LogPlayerMove, Warning, 
			TEXT("Cannot update AI knowledge: World=%s, PlayerPawn=%s, Enemies=%d"),
			World ? TEXT("Valid") : TEXT("NULL"),
			PlayerPawn ? TEXT("Valid") : TEXT("NULL"),
			Enemies.Num());
		return false;
	}

	UEnemyAISubsystem* EnemyAISys = World->GetSubsystem<UEnemyAISubsystem>();
	UEnemyTurnDataSubsystem* EnemyData = World->GetSubsystem<UEnemyTurnDataSubsystem>();
	UGridOccupancySubsystem* Occupancy = World->GetSubsystem<UGridOccupancySubsystem>();
	UGridPathfindingSubsystem* PathFinder = World->GetSubsystem<UGridPathfindingSubsystem>();

	if (!EnemyAISys || !EnemyData || !Occupancy || !PathFinder)
	{
		UE_LOG(LogPlayerMove, Error, 
			TEXT("Required subsystems not available: EnemyAI=%d, EnemyData=%d, Occupancy=%d, PathFinder=%d"),
			EnemyAISys != nullptr, EnemyData != nullptr, Occupancy != nullptr, PathFinder != nullptr);
		return false;
	}

	// Get final player position
	const FIntPoint FinalPlayerCell = Occupancy->GetCellOfActor(PlayerPawn);
	if (FinalPlayerCell == FIntPoint(-1, -1))
	{
		UE_LOG(LogPlayerMove, Error, TEXT("Cannot get player cell position"));
		return false;
	}

	// Update DistanceField
	UpdateDistanceFieldForFinalPosition(FinalPlayerCell);

	// Rebuild Observations
	TArray<FEnemyObservation> Observations;
	EnemyAISys->BuildObservations(Enemies, FinalPlayerCell, PathFinder, Observations);
	EnemyData->Observations = Observations;

	// Collect Intents
	EnemyAISys->CollectIntents(Observations, Enemies, OutIntents);

	// Commit to Data
	EnemyData->SaveIntents(OutIntents);

	UE_LOG(LogPlayerMove, Log, 
		TEXT("AI Knowledge Updated: Player at (%d,%d). %d Intents generated."),
		FinalPlayerCell.X, FinalPlayerCell.Y, OutIntents.Num());

	return true;
}

bool UPlayerMoveHandlerSubsystem::HandlePlayerMoveCompletion(
	const FGameplayEventData* Payload,
	int32 CurrentTurnId,
	const TArray<AActor*>& EnemyActors,
	TArray<FEnemyIntent>& OutFinalIntents)
{
	// 1. Validate turn notification
	const int32 NotifiedTurn = Payload ? static_cast<int32>(Payload->EventMagnitude) : -1;
	if (!ValidateTurnNotification(NotifiedTurn, CurrentTurnId))
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogPlayerMove, Error, TEXT("[Turn %d] World not available"), CurrentTurnId);
		return false;
	}

	// 2. Get player pawn
	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0);
	if (!PlayerPawn)
	{
		UE_LOG(LogPlayerMove, Error, TEXT("[Turn %d] PlayerPawn not found"), CurrentTurnId);
		return false;
	}

	// 3. Update AI knowledge (includes DistanceField update, observations, and intents)
	if (!UpdateAIKnowledge(PlayerPawn, EnemyActors, OutFinalIntents))
	{
		UE_LOG(LogPlayerMove, Error, TEXT("[Turn %d] Failed to update AI knowledge"), CurrentTurnId);
		return false;
	}

	UE_LOG(LogPlayerMove, Log, 
		TEXT("[Turn %d] Player move completion handled: %d enemies, %d intents"),
		CurrentTurnId, EnemyActors.Num(), OutFinalIntents.Num());

	return true;
}
