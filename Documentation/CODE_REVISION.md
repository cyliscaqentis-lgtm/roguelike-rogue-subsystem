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
