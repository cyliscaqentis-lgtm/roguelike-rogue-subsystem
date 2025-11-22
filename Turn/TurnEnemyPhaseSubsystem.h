#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Turn/TurnSystemTypes.h"
#include "TurnEnemyPhaseSubsystem.generated.h"

class AGameTurnManagerBase;
class UTurnCorePhaseManager;
class UEnemyTurnDataSubsystem;
class UAttackPhaseExecutorSubsystem;

/**
 * Subsystem responsible for orchestrating the Enemy Phase (Move and Attack).
 * Delegates detailed resolution to TurnCorePhaseManager and AttackPhaseExecutorSubsystem.
 */
UCLASS()
class LYRAGAME_API UTurnEnemyPhaseSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * Main entry point for executing the enemy phase.
	 * Determines if the phase should be Sequential (Attack -> Move) or Simultaneous (Move only).
	 */
	void ExecuteEnemyPhase(AGameTurnManagerBase* TurnManager, int32 TurnId);

	/**
	 * Executes the move phase.
	 * If bSkipAttackCheck is false, it will first check for attack intents and redirect to ExecuteEnemyPhase if found.
	 */
	void ExecuteMovePhase(AGameTurnManagerBase* TurnManager, int32 TurnId, bool bSkipAttackCheck);

	/**
	 * Executes the simultaneous move phase (no attacks).
	 */
	void ExecuteSimultaneousPhase(AGameTurnManagerBase* TurnManager, int32 TurnId);

	/**
	 * Executes the sequential move phase (after attacks or if no attacks).
	 */
	void ExecuteSequentialMovePhase(AGameTurnManagerBase* TurnManager, int32 TurnId, const TArray<FResolvedAction>& CachedActions = TArray<FResolvedAction>());

	/**
	 * Executes attacks sequentially.
	 */
	void ExecuteAttacks(AGameTurnManagerBase* TurnManager, int32 TurnId, const TArray<FResolvedAction>& PreResolvedAttacks = TArray<FResolvedAction>());

private:
	/** Helper to resolve dependencies and log errors if missing. */
	bool ResolveDependencies(int32 TurnId, const TCHAR* ContextLabel, UTurnCorePhaseManager*& OutPhaseManager, UEnemyTurnDataSubsystem*& OutEnemyData);

	/** Helper to check if any intent has an attack tag. */
	bool DoesAnyIntentHaveAttack(const TArray<FEnemyIntent>& Intents, int32 TurnId) const;

	/** Helper to dispatch move actions via TurnManager. */
	void DispatchMoveActions(AGameTurnManagerBase* TurnManager, const TArray<FResolvedAction>& Actions);

	UPROPERTY()
	TObjectPtr<UTurnCorePhaseManager> PhaseManager;

	UPROPERTY()
	TObjectPtr<UEnemyTurnDataSubsystem> EnemyData;

	UPROPERTY()
	TObjectPtr<UAttackPhaseExecutorSubsystem> AttackExecutor;

	TArray<FResolvedAction> StoredResolvedActions;

	// Sequential mode tracking (owned by EnemyPhase subsystem)
	bool bSequentialModeActive = false;
	bool bSequentialMovePhaseStarted = false;
	bool bIsInMoveOnlyPhase = false;

public:
	// Sequential mode accessors
	bool IsSequentialModeActive() const { return bSequentialModeActive; }
	bool HasSequentialMovePhaseStarted() const { return bSequentialMovePhaseStarted; }
	bool IsInMoveOnlyPhase() const { return bIsInMoveOnlyPhase; }

	void EnterSequentialMode();
	void ResetSequentialMode();
	void MarkSequentialMoveOnlyPhase();
};
