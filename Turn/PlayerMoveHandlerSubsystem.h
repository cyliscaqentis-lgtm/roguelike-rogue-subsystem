#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PlayerMoveHandlerSubsystem.generated.h"

class APawn;
class AActor;
struct FEnemyIntent;
struct FGameplayEventData;

/**
 * UPlayerMoveHandlerSubsystem
 * 
 * Manages post-player-move logic including:
 * - AI knowledge updates (observations, intents)
 * - DistanceField updates
 * - Turn notification validation
 * 
 * CodeRevision: INC-2025-1206-R2 (Extract player move completion logic from GameTurnManagerBase) (2025-11-22 00:22)
 */
UCLASS()
class LYRAGAME_API UPlayerMoveHandlerSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * Handle player move completion processing
	 * @param Payload Event data from the move completion
	 * @param CurrentTurnId Current turn ID
	 * @param OutEnemyActors Updated enemy list (should be populated by caller)
	 * @param OutFinalIntents Generated intents output
	 * @return True if processing was successful
	 */
	bool HandlePlayerMoveCompletion(
		const FGameplayEventData* Payload,
		int32 CurrentTurnId,
		const TArray<AActor*>& EnemyActors,
		TArray<FEnemyIntent>& OutFinalIntents);

	/**
	 * Update AI knowledge based on player's final position
	 * @param PlayerPawn The player's pawn
	 * @param Enemies Array of enemy actors
	 * @param OutIntents Output array for generated intents
	 * @return True if update was successful
	 */
	bool UpdateAIKnowledge(
		APawn* PlayerPawn,
		const TArray<AActor*>& Enemies,
		TArray<FEnemyIntent>& OutIntents);

	/**
	 * Update DistanceField for player's final position
	 * @param PlayerCell Player's grid coordinates
	 * @return True if update was successful
	 */
	bool UpdateDistanceFieldForFinalPosition(const FIntPoint& PlayerCell);

private:
	/**
	 * Validate that the turn notification is current
	 * @param NotifiedTurn Turn ID from the notification
	 * @param CurrentTurn Current turn ID
	 * @return True if notification is valid
	 */
	bool ValidateTurnNotification(int32 NotifiedTurn, int32 CurrentTurn) const;
};
