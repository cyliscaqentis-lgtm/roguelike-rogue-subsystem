# Rogue CodeRevision Log & Usage Guidelines

## Overview

When modifying code in this project, add a `CodeRevision` comment to track version changes. This helps external agents like Claude Code understand the change history, so please check this file when editing related files.

## CodeRevision Log

- `CodeRevision: INC-2025-00001-R1` - Added barrier notification before `AGameTurnManagerBase::EndEnemyTurn()` (barrier synchronization fix).
- `CodeRevision: INC-2025-00001-R2` - Added `WindowId` ACK handling in `APlayerControllerBase::Client_ConfirmCommandAccepted_Implementation`.
- `CodeRevision: INC-2025-00002-R1` - Added detailed pathfinding retry logic to `DistanceFieldSubsystem::GetNextStepTowardsPlayer()`.
- `CodeRevision: INC-2025-00003-R1` - Updated `response_note.md` and this file to include "CodeRevision" usage policy (2025-11-14 22:24).
- `CodeRevision: INC-2025-00004-R1` - Fixed GridOccupancy corruption by dropping stale turn notifications in `OnAttacksFinished` (2025-11-14 22:50).
- `CodeRevision: INC-2025-00005-R1` - Updated `TurnManager` debug messages to ASCII, updated `DungeonFloorGenerator::BSP_Split` to use latest `stack.Pop()` API (2025-11-14 23:00).
- `CodeRevision: INC-2025-00006-R1` - Removed unused legacy functions from `AGameTurnManagerBase`: `RunTurn()`, `ProcessPlayerCommand()`, `StartTurnMoves()` (2025-11-15 20:30)
- `CodeRevision: INC-2025-00007-R1` - Removed obsolete ability tracking functions (`NotifyAbilityStarted()`, `OnAbilityCompleted()`, `WaitForAbilityCompletion_Implementation()`) and AI pipeline functions (`BuildObservations_Implementation()`, `ComputeEnemyIntent_Implementation()`) replaced by `UTurnActionBarrierSubsystem` and `EnemyAISubsystem`. Updated `GA_WaitBase` to use Barrier subsystem exclusively (2025-11-15 21:00)
- `CodeRevision: INC-2025-00008-R1` - Removed Blueprint-exposed wall manipulation functions (`SetWallCell()`, `SetWallAtWorldPosition()`, `SetWallRect()`) from `AGameTurnManagerBase`. Removed duplicate input window functions (`OpenInputWindowForPlayer()`, `CloseInputWindowForPlayer()`) and unified input window management through `UPlayerInputProcessor` API (`OpenInputWindow()`, `CloseInputWindow()`). Updated call sites in `NotifyPlayerPossessed()` and `OnPlayerCommandAccepted_Implementation()` to use the unified API (2025-11-15 22:00)
- `CodeRevision: INC-2025-00009-R1` - Removed redundant wrapper functions `TM_ReturnGridStatus()` and `TM_GridChangeVector()` from `AGameTurnManagerBase`. These functions were simple wrappers around `ADungeonFloorGenerator` methods and created confusion with duplicate functionality in `AGridPathfindingLibrary`. Blueprint code should use `GetFloorGenerator()->ReturnGridStatus()` or `GetFloorGenerator()->GridChangeVector()` directly instead (2025-11-15 22:30)
- `CodeRevision: INC-2025-00010-R1` - Removed redundant wrapper function `ExecuteSimultaneousMoves()` from `AGameTurnManagerBase`. This function was a simple wrapper that only called `ExecuteSimultaneousPhase()` and was unused (no C++ call sites, not exposed to Blueprint). Code should call `ExecuteSimultaneousPhase()` directly instead (2025-11-15 22:45)
- `CodeRevision: INC-2025-00011-R1` - Fixed garbled and Japanese comments in `GameTurnManagerBase.h` and `GameTurnManagerBase.cpp`, translated all comments to English. Fixed encoding issues including garbled characters (繝, 縺, ☁E, etc.) and converted Japanese documentation to English for better maintainability (2025-11-15 23:45)
- `CodeRevision: INC-2025-00012-R1` - Translated all Japanese text in log messages within `GameTurnManagerBase.cpp` to English. Fixed garbled characters in log messages (ↁE, ☁E, ✁E, etc.) and converted Japanese log text to English for consistency (2025-11-15 23:50)
- `CodeRevision: INC-2025-00013-R1` - Translated remaining Japanese comments in `GameTurnManagerBase.cpp` to English. Fixed approximately 200+ Japanese comments throughout the file, including command processing, movement validation, occupancy checks, and helper functions. Work in progress - approximately 157 comments remaining (2025-11-15 23:55)
- `CodeRevision: INC-2025-00014-R1` - Removed ALL Japanese comments from `GameTurnManagerBase.cpp`. Deleted approximately 164 lines containing Japanese characters or garbled text (☁E, ↁE, ✁E, etc.) to clean up the codebase. All Japanese comment lines have been completely removed (2025-11-15 23:58)

## Usage Guidelines

### Format

When making code changes, add a comment near the modified code using the following format:

```
CodeRevision: <ID> (<Description>) (YYYY-MM-DD HH:MM)
```

Example:
```cpp
// CodeRevision: INC-2025-00002-R1 (Added retry logic for pathfinding) (2025-11-10 17:32)
void SomeFunction() {
    // ...
}
```

### Workflow

1. **When making changes**: Create a new `CodeRevision` entry and add it as a comment near the relevant file sections.
2. **Update documentation**: Update this file with the latest `CodeRevision` entry, including the target file, purpose, and timestamp.
3. **Consistency**: Include the same tag in both code comments and this file, so that changes can be verified together with the version and timing.

### For External Agents

When working on this project (including Claude Code and other AI assistants):
- Always check this file before making changes
- Follow the CodeRevision format when modifying code
- Update this log file with your changes
- Maintain consistency between code comments and this documentation

## Recording Guidelines

- New `CodeRevision` entries should follow the format: `CodeRevision: <ID> ? <Description> (YYYY-MM-DD HH:MM)`, and be added as comments near the relevant code.

**Usage Policy**
1. When making changes, create a new `CodeRevision` entry and add it as a comment near the relevant file sections.
2. Update this file with the latest `CodeRevision` and the target file/purpose, so that changes can be tracked across sessions.
3. Include the same tag in both comments and this file, so that changes can be verified together with the version and timing.

## Recent Fixes

- `CodeRevision: INC-2025-00001-R1` - `AGameTurnManagerBase::ExecuteAttacks()` now notifies `Barrier->BeginTurn()` before invoking attacks so GA_AttackBase receives a valid turn ID.
- `CodeRevision: INC-2025-00001-R2` - `AGameTurnManagerBase::OnAttacksFinished()` drops stale turn notifications instead of processing them, preventing outdated attack states from corrupting the turn flow.
- `CodeRevision: INC-2025-00002-R1` - `AUnitBase::StartNextLeg()` now retries `UGridOccupancySubsystem::UpdateActorCell()` on failure (0.1s timer) instead of logging a critical error, and only broadcasts `OnMoveFinished` after the occupancy commit succeeds.
- `CodeRevision: INC-2025-00002-R2` - `UGA_MoveBase::OnMoveFinished()` mirrors the occupancy retry behavior so ability completion waits for a successful grid commit instead of rolling back.
- `CodeRevision: INC-2025-00006-R1` - Removed unused legacy functions from `AGameTurnManagerBase`: `RunTurn()` (hardcoded turn loop replaced by `TurnFlowCoordinator`), `ProcessPlayerCommand()` (private method replaced by `CommandHandler->ProcessPlayerCommand()`), and `StartTurnMoves()` (unused function). These functions were identified as obsolete after the refactoring to use specialized subsystems.

## Comment Policy

- Document any new C++ comments in English only and log the change here (2025-11-15).
