#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TurnSystemInitializer.generated.h"

class AGameTurnManagerBase;

/**
 * UTurnSystemInitializer
 *
 * Helper subsystem that encapsulates the heavy initialization sequence
 * previously embedded in AGameTurnManagerBase::InitializeTurnSystem.
 *
 * Responsibilities:
 * - Resolve and cache core turn subsystems for the GameTurnManager
 * - Initialize the action barrier and attack executor delegates
 * - Ensure PathFinder and enemy roster are ready for the first turn
 * - Bind the TurnAbilityCompleted gameplay event delegate
 *
 * CodeRevision: INC-2025-1207-R1 (Extract core turn initialization from GameTurnManagerBase into UTurnSystemInitializer) (2025-11-22 00:40)
 */
UCLASS()
class LYRAGAME_API UTurnSystemInitializer : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Perform the full initialization sequence for the given TurnManager. */
	bool InitializeTurnSystem(AGameTurnManagerBase* TurnManager);
};

