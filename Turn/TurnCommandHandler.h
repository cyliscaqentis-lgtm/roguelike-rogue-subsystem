// ============================================================================
// TurnCommandHandler.h
// Player command processing World Subsystem
// Extracted from GameTurnManagerBase (2025-11-09)
// ============================================================================

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Turn/TurnSystemTypes.h"
#include "TurnCommandHandler.generated.h"

class APlayerControllerBase;

/**
 * World subsystem responsible for processing player commands.
 *
 * Responsibilities:
 * - Command validation
 * - Marking commands as accepted
 * - Managing the input window lifecycle
 */
UCLASS()
class LYRAGAME_API UTurnCommandHandler : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// ========== Subsystem Lifecycle ==========

	/** Initialize the subsystem and internal state. */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** Deinitialize the subsystem and clear internal state. */
	virtual void Deinitialize() override;

	// ========== Command Processing ==========

	/**
	 * Process a player command.
	 *
	 * CodeRevision: INC-2025-1134-R1 (Execute validated commands inside handler) (2025-12-13 09:30)
	 *
	 * After validation, executes appropriate actions based on the command tag.
	 *
	 * @param Command The command to process.
	 * @return true if the command is accepted and execution path continues.
	 * @return false if the command is rejected or cannot be processed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Turn|Command")
	bool ProcessPlayerCommand(const FPlayerCommand& Command);

	/**
	 * Validate the basic correctness and acceptability of a command.
	 *
	 * This includes:
	 * - Tag validity
	 * - Timeliness check
	 * - Duplicate command check
	 *
	 * @param Command The command to validate.
	 * @return true if the command is valid.
	 * @return false if the command fails any validation step.
	 */
	UFUNCTION(BlueprintCallable, Category = "Turn|Command")
	bool ValidateCommand(const FPlayerCommand& Command) const;

	/**
	 * Mark a command as "accepted".
	 *
	 * Code note:
	 * - Intended to be called only after MovePrecheck succeeds.
	 * - Prevents re-submitted commands from being treated as duplicates
	 *   when a previously accepted command is later rejected by another step.
	 *
	 * @param Command The command to mark as accepted.
	 */
	UFUNCTION(BlueprintCallable, Category = "Turn|Command")
	void MarkCommandAsAccepted(const FPlayerCommand& Command);

	/**
	 * Begin an input window for receiving player commands.
	 *
	 * @param WindowId Identifier for the input window.
	 */
	UFUNCTION(BlueprintCallable, Category = "Turn|Command")
	void BeginInputWindow(int32 WindowId);

protected:
	// ========== Internal State ==========

	/** Map of the last accepted commands keyed by TurnId (TurnId -> Command). */
	UPROPERTY(Transient)
	TMap<int32, FPlayerCommand> LastAcceptedCommands;

	/** The current input window identifier. */
	UPROPERTY(Transient)
	int32 CurrentInputWindowId = 0;

	/** Whether the input window is currently open. */
	UPROPERTY(Transient)
	bool bInputWindowOpen = false;

	// ========== Internal Helpers ==========

	/**
	 * Check whether the command has not timed out.
	 *
	 * Note:
	 * - Currently a placeholder that always returns true.
	 * - Intended extension point for time-based validation.
	 *
	 * @param Command The command to check.
	 * @return true if the command is considered timely.
	 */
	bool IsCommandTimely(const FPlayerCommand& Command) const;

	/**
	 * Check whether the command is not a duplicate of a previously accepted command.
	 *
	 * @param Command The command to check.
	 * @return true if the command is not a duplicate.
	 */
	bool IsCommandUnique(const FPlayerCommand& Command) const;
};
