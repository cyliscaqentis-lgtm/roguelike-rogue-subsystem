#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TurnInitializationSubsystem.generated.h"

class APawn;
class AActor;
struct FEnemyIntent;
class AGameTurnManagerBase;
class URogueDungeonSubsystem;

/**
 * UTurnInitializationSubsystem
 * 
 * Manages turn initialization logic including:
 * - DistanceField updates
 * - GridOccupancy initialization
 * - Preliminary enemy intent generation
 * 
 * CodeRevision: INC-2025-1206-R1 (Extract turn initialization from GameTurnManagerBase) (2025-11-22 00:11)
 */
UCLASS()
class LYRAGAME_API UTurnInitializationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * Initialize GameTurnManager (BeginPlay logic)
	 */
	void InitializeTurn(int32 TurnId, APawn* PlayerPawn, const TArray<AActor*>& Enemies);

	/**
	 * Handle turn start logic (roster refresh, tag cleanup, initialization)
	 * Delegated from GameTurnManagerBase::OnTurnStartedHandler
	 */
	void OnTurnStarted(AGameTurnManagerBase* Manager, int32 TurnId);

	/**
	 * Resolve or spawn PathFinder subsystem
	 * Moved from GameTurnManagerBase for better separation of concerns
	 */
	void ResolveOrSpawnPathFinder(AGameTurnManagerBase* Manager);

	/**
	 * Resolve or spawn UnitManager actor
	 * Moved from GameTurnManagerBase for better separation of concerns
	 */
	void ResolveOrSpawnUnitManager(AGameTurnManagerBase* Manager);

	/**
	 * Initialize the GameTurnManager during BeginPlay
	 * Called from TurnSystemInitializer::InitializeTurnSystem
	 */
	void InitializeGameTurnManager(AGameTurnManagerBase* Manager);

	/**
	 * Handle dungeon ready callback
	 * Called when URogueDungeonSubsystem::OnGridReady fires
	 */
	void HandleDungeonReady(AGameTurnManagerBase* Manager, URogueDungeonSubsystem* DungeonSys);

	/**
	 * Check if all conditions are met to start the first turn
	 */
	bool CanStartFirstTurn() const;

	/**
	 * Notify that player has been possessed
	 */
	void NotifyPlayerPossessed(APawn* NewPawn);

	/**
	 * Notify that pathfinding is ready
	 */
	void NotifyPathReady();

	/**
	 * Notify that units have been spawned
	 */
	void NotifyUnitsSpawned();

	/**
	 * Mark that first turn has started
	 */
	void MarkFirstTurnStarted();

	/**
	 * Update the DistanceField for pathfinding
	 * @param PlayerPawn The player's pawn
	 * @param Enemies Array of enemy actors
	 * @return True if update was successful and all enemies are reachable
	 */
	bool UpdateDistanceField(APawn* PlayerPawn, const TArray<AActor*>& Enemies);

	/**
	 * Generate preliminary enemy intents at turn start
	 * @param PlayerPawn The player's pawn
	 * @param Enemies Array of enemy actors
	 * @param OutIntents Output array for generated intents
	 * @return True if intents were successfully generated
	 */
	bool GeneratePreliminaryIntents(APawn* PlayerPawn, const TArray<AActor*>& Enemies, TArray<FEnemyIntent>& OutIntents);

	/**
	 * Initialize GridOccupancy for the new turn
	 * @param TurnId The turn ID being started
	 * @param PlayerPawn The player's pawn
	 * @param Enemies Array of enemy actors
	 */
	void InitializeGridOccupancy(int32 TurnId, APawn* PlayerPawn, const TArray<AActor*>& Enemies);

private:
	/**
	 * Calculate the margin for DistanceField updates
	 * @param PlayerGrid Player's grid position
	 * @param EnemyPositions Set of enemy grid positions
	 * @return Calculated margin value
	 */
	int32 CalculateDistanceFieldMargin(const FIntPoint& PlayerGrid, const TSet<FIntPoint>& EnemyPositions) const;

	/**
	 * Validate that all enemies are reachable in the DistanceField
	 * @param TurnId Current turn ID for logging
	 * @param EnemyPositions Set of enemy grid positions
	 * @param Margin The margin used for the update
	 * @return True if all enemies are reachable
	 */
	bool ValidateDistanceFieldReachability(int32 TurnId, const TSet<FIntPoint>& EnemyPositions, int32 Margin) const;

	// Current turn ID for logging
	int32 CurrentTurnId = INDEX_NONE;

	/** Initialization state flags */
	bool bPathReady = false;
	bool bUnitsSpawned = false;
	bool bPlayerPossessed = false;
	bool bFirstTurnStarted = false;
};
