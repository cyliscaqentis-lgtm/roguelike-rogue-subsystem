# Rogue CodeRevision Log & Usage Guidelines

## Overview

This document tracks all code revisions in the Rogue project. Each revision includes a unique ID, description, affected files, and timestamp. This helps external agents like Claude Code understand the change history.

**Refactoring Summary (2025-11-22):** GameTurnManagerBase.cpp reduced from 2671 to 2058 lines (-613, -23%) through systematic extraction to utility classes and subsystems.

---

## Usage Guidelines

### Format

When making code changes, add a comment near the modified code using the following format:

```cpp
// CodeRevision: <ID> (<Description>) (YYYY-MM-DD HH:MM)
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

### Comment Policy

- Document any new C++ comments in English only and log the change here (2025-11-15).

---

## Change History

### 2025-12-13

- `INC-2025-1134-R1` - Routed player attack commands through `TurnCommandHandler` (new command tag, controller attack input, and ability dispatch) so GameTurnManager only logs the result (`Turn/TurnCommandHandler.cpp`) (2025-12-13 09:30)

### 2025-11-27

- `INC-2025-1131-R1` - Replace the alternate-move selection with tactics-aware scoring so candidates are filtered by distance and chosen based on weighted distance/lane alignment (2025-11-27 17:00)
- `INC-2025-1130-R1` - Two-pass enemy intent generation that marks attackers as hard-blocked and lets movers claim alternative targets or wait rather than colliding (2025-11-27 16:30)
- `INC-2025-1129-R1` - Cap `UUnitMovementComponent`'s grid update retries and force movement completion after the limit to prevent hang states when `UpdateActorCell` freezes (2025-11-27 16:00)
- `INC-2025-1128-R1` - Tightened PlayerController input latch resets to only trigger on legitimate window id changes or explicit rejection, preventing duplicate SubmitCommand RPCs (2025-11-27 15:00)
- `INC-2025-1127-R1` - Align sequential move filtering with `CoreResolvePhase`, log why moves drop out of the dispatch set, and ensure the legacy sequential entry path still sets move-phase flags (2025-11-27 14:15)

### 2025-11-26

- `INC-2025-1126-R1` - Derives sequential/simultaneous flow from cached EnemyIntents via `DoesAnyIntentHaveAttack()` so ATTACK intents never slip through per FIX_PLAN_26.md (2025-11-26 10:00)

### 2025-11-25

- `INC-2025-1125-R1` - Blocks move intents from attack tiles and derives bHasAttack from cached intents per FIX_PLAN_25.md (2025-11-25 12:00)

### 2025-11-24

- `INC-2025-1124-R1` - Simplifies `ComputeIntent` move handling so CoreResolvePhase arbitrates congested moves per `FIX_PLAN_24.md` (2025-11-24 09:30)

### 2025-11-23

- `INC-2025-1123-R1` - Rejects DistanceField suggestions that do not reduce the distance to the player as outlined in `FIX_PLAN_23.md` (2025-11-23 12:00)

### 2025-11-22

#### Performance
- `INC-2025-1122-PERF-R5` - Enhanced input buffering for seamless continuous movement: now buffers input even after command is sent (for next turn), and processes buffer on window change in addition to gate opening; enables uninterrupted walking rhythm when holding direction (`Player/PlayerControllerBase.cpp`) (2025-11-23 01:30)
- `INC-2025-1122-PERF-R4` - Implemented input buffering to eliminate gate replication delay: when player inputs while gate is closed (waiting for replication), the input is buffered and processed immediately when gate opens in Tick, reducing perceived latency by ~200-300ms (`Player/PlayerControllerBase.h`, `Player/PlayerControllerBase.cpp`) (2025-11-23 01:00)
- `INC-2025-1122-PERF-R3` - Changed `UpdateActorCell` retry from timed delay (0.02s) to `SetTimerForNextTick` for instant retry on next frame; eliminates retry latency entirely when Two-Phase Commit requires waiting for previous occupant (`Character/UnitMovementComponent.cpp`) (2025-11-23 00:45)
- `INC-2025-1122-PERF-R2` - Reduced `UpdateActorCell` retry delay from 0.1s to 0.02s to minimize freeze perception when grid occupancy conflicts occur; reduces worst-case retry delay from 0.5s to 0.1s (`Character/UnitMovementComponent.cpp`) (2025-11-22 22:00)
- `INC-2025-1122-PERF-R1` - Fixed performance issue causing frame hitches on every player step: removed redundant AI calculation in `OnPlayerMoveCompleted` when enemy phase was already executed (simultaneous movement case), reducing AI calculations from 3x to 2x per turn (`Turn/GameTurnManagerBase.cpp`) (2025-11-22 20:45)

#### Bug Fixes
- `INC-2025-1122-ADJ-ATTACK-R3` - [FIX] Upgrade enemies to ATTACK only if adjacent to BOTH player's current AND target positions: prevents both (1) enemies attacking thin air (R1 bug) and (2) enemies not attacking when they should (R2 issue). Modified `UpgradeIntentsForAdjacency` to take both positions. (`Turn/GameTurnManagerBase.cpp`, `AI/Enemy/EnemyTurnDataSubsystem.h`, `AI/Enemy/EnemyTurnDataSubsystem.cpp`) (2025-11-22 21:05)
- `INC-2025-1122-TARGET-R1` - Fixed enemy not finding target during simultaneous movement: `FindAdjacentTarget` now checks player's reserved cell if player not found at current position, enabling attacks against moving targets (`Abilities/GA_MeleeAttack.cpp`) (2025-11-22 21:25)
- `INC-2025-1122-FACING-R1` - Fixed enemy facing wrong direction during attack: use target's reserved cell position for facing calculation even while target is still moving, so attackers face the target's destination rather than their mid-movement position (`Abilities/GA_MeleeAttack.cpp`) (2025-11-22 20:15)
- `INC-2025-1122-ATTACK-SEQ-R1` - Fixed simultaneous player/enemy attacks: removed immediate `ExecuteEnemyPhase()` call from attack command processing in `TurnCommandHandler`, so player attack completes before enemy phase begins (`Turn/TurnCommandHandler.cpp`) (2025-11-22 19:57)
- `INC-2025-1122-ATTACK-SEQ-R2` - Added enemy phase trigger in `OnPlayerMoveCompleted` after player action completes, so enemy phase starts after player attack finishes rather than simultaneously (`Turn/GameTurnManagerBase.cpp`) (2025-11-22 19:57)

#### Refactoring
- `INC-2025-1208-R4` - Introduced `UTurnAdvanceGuardSubsystem` to encapsulate the `EndEnemyTurn`/`CanAdvanceTurn` safety checks, moving barrier inspections, residual tag cleanup, and retry scheduling out of `AGameTurnManagerBase` so the manager now delegates to a reusable subsystem wrapper (`Turn/GameTurnManagerBase.h`, `Turn/GameTurnManagerBase.cpp`, `Turn/TurnAdvanceGuardSubsystem.h`, `Turn/TurnAdvanceGuardSubsystem.cpp`) (2025-11-22 02:10)
- `INC-2025-1208-R3` - Extracted `AdvanceTurnAndRestart`'s player-position logging and core turn-advance sequence into dedicated helpers so turn completion remains behaviorally identical while the main method focuses on safety checks (`Turn/GameTurnManagerBase.h`, `Turn/GameTurnManagerBase.cpp`) (2025-11-22 01:45)
- `INC-2025-1208-R2` - Refactored `ExecuteMovePhase` and `ExecuteSimultaneousPhase` in `AGameTurnManagerBase` to share dependency resolution and player-intent plumbing via new helpers, reducing duplication while preserving existing logging behavior (`Turn/GameTurnManagerBase.h`, `Turn/GameTurnManagerBase.cpp`) (2025-11-22 01:30)
- `INC-2025-1208-R1` - Split `AGameTurnManagerBase::DispatchResolvedMove` into focused helpers for wait actions, player moves, and enemy moves, centralized enemy roster fallback collection into dedicated functions, and introduced shared helpers for loose gameplay-tag removal and blocking-tag cleanup before triggering player move abilities (`Turn/GameTurnManagerBase.h`, `Turn/GameTurnManagerBase.cpp`) (2025-11-22 01:10)
- `INC-2025-1207-R1` - Extracted the heavy initialization logic from `AGameTurnManagerBase::InitializeTurnSystem` into a dedicated `UTurnSystemInitializer` world subsystem so `GameTurnManagerBase` now delegates turn-system wiring (subsystem resolution, barrier/attack executor delegate binding, PathFinder/enemy bootstrap, and TurnAbilityCompleted event hook-up) to a helper (files: `Turn/TurnSystemInitializer.h`, `Turn/TurnSystemInitializer.cpp`, `Turn/GameTurnManagerBase.h`, `Turn/GameTurnManagerBase.cpp`) (2025-11-22 00:40)
- `INC-2025-1122-SIMUL-R5` - Extracted intent regeneration logic from `ExecuteEnemyPhase` to `UEnemyTurnDataSubsystem::RegenerateIntentsForPlayerPosition`, reducing `GameTurnManagerBase.cpp` code complexity (`Turn/GameTurnManagerBase.cpp`, `AI/Enemy/EnemyTurnDataSubsystem.h`, `AI/Enemy/EnemyTurnDataSubsystem.cpp`) (2025-11-22)
- `INC-2025-1122-SIMUL-R3` - Register player move reservation via `TurnManager->RegisterResolvedMove` before `ExecuteEnemyPhase` so that `GetReservedCellForActor` returns the correct target cell for intent generation (`Turn/TurnCommandHandler.cpp`) (2025-11-22)
- `INC-2025-1122-SIMUL-R2` - Use existing `GridOccupancySubsystem::GetReservedCellForActor` to get player's predicted position for enemy intent generation during simultaneous movement, instead of adding new variables (`Turn/GameTurnManagerBase.cpp`, `Turn/TurnCommandHandler.cpp`) (2025-11-22)
- `INC-2025-1122-SIMUL-R1` - Implemented simultaneous movement: moved `ExecuteEnemyPhase()` to public section and call it from `TurnCommandHandler::TryExecuteMoveCommand` immediately after player command is accepted, removed call from `OnPlayerMoveCompleted` so enemies start moving at the same time as the player (`Turn/GameTurnManagerBase.h`, `Turn/GameTurnManagerBase.cpp`, `Turn/TurnCommandHandler.cpp`) (2025-11-22)
- `INC-2025-1122-R3` - Extracted `EnsureEnemyIntents` fallback logic into `UEnemyTurnDataSubsystem::EnsureIntentsFallback` so `GameTurnManagerBase::EnsureEnemyIntents` is now a thin wrapper that delegates intent generation to the subsystem (`AI/Enemy/EnemyTurnDataSubsystem.h`, `AI/Enemy/EnemyTurnDataSubsystem.cpp`, `Turn/GameTurnManagerBase.cpp`) (2025-11-22)
- `INC-2025-1122-R2` - Moved move reservation and dispatch logic into `UMoveReservationSubsystem` including `TriggerPlayerMoveAbility` so `GameTurnManagerBase` delegates movement mechanics to the subsystem (`Turn/MoveReservationSubsystem.h`, `Turn/MoveReservationSubsystem.cpp`, `Turn/GameTurnManagerBase.cpp`) (2025-11-22)
- `INC-2025-1122-R1` - Created `TurnTagCleanupUtils` namespace with static helper functions for tag cleanup (`ClearResidualInProgressTags`, `RemoveGameplayTagLoose`, `CleanseBlockingTags`) extracted from `GameTurnManagerBase` (`Utility/TurnTagCleanupUtils.h`, `Utility/TurnTagCleanupUtils.cpp`, `Turn/GameTurnManagerBase.cpp`) (2025-11-22)

### 2025-11-21

- `INC-2025-1205-R1` - Added `Utility/RogueActorUtils` so actor/team helpers can be reused and introduced `UUnitTurnStateSubsystem` to cache per-turn enemy rosters; `AGameTurnManagerBase` now refreshes enemy lists through the new subsystem for occupancy, distance-field prep, and fallback intent generation flows (`Utility/RogueActorUtils.h`, `Turn/UnitTurnStateSubsystem.h`, `Turn/UnitTurnStateSubsystem.cpp`, `Turn/GameTurnManagerBase.h`, `Turn/GameTurnManagerBase.cpp`) (2025-11-21 23:08)
- `INC-2025-1204-R1` - Added `ts.Enemy.RegenIntentsOnMove` console variable (default 0) to gate the heavy DistanceField + enemy intent regeneration path in `AGameTurnManagerBase::OnPlayerCommandAccepted_Implementation`, so by default player MOVE commands no longer rebuild the distance field and recompute intents synchronously on every step (keeping performance predictable) while still allowing developers to re-enable the predictive behavior when investigating AI issues (2025-11-21 16:30)
- `INC-2025-1201-R1` - Extended `Tools/Log/log_summarizer.py` with an AI-focused `--mode player_actions` preset and optional JSON output so assistants can consume concise, structured player command summaries instead of raw logs (2025-11-21 02:30)
- `INC-2025-1200-R1` - Updated `Tools/Log/log_summarizer.py` to robustly decode UTF-8/UTF-16 CSV logs and correctly parse TurnID for the default "last 3 turns" summarization behavior across environments (2025-11-21 02:00)

### 2025-11-20

- `INC-2025-1157-R3` - Added a Chebyshev distance=1 guard to FOLLOW-UP so only adjacent units can step into the origin cell (including player origin) while still allowing player-path chasing; prevents long-range leapfrogs that caused stacking/overlap (2025-11-20 21:00)
- `INC-2025-1157-R2` - Relaxed FOLLOW-UP blocking so enemies can also chain into the player's previous cell (team mismatch allowed if origin is player), rejecting only true team mismatches; this prevents the pursuit chain from breaking and keeps enemies stacked correctly behind the lead chaser (2025-11-20 20:40)
- `INC-2025-1157-R1` - Restricted GridOccupancySubsystem FOLLOW-UP so only same-team units (and never the player pawn) can override an OriginHold, keeping enemy chains behind the lead pursuer instead of backfilling the player's old cell (2025-11-20 20:00)
- `INC-2025-1148-R2` - Block swap moves in UConflictResolverSubsystem::ResolveAllConflicts even when a cell has multiple contenders by downgrading swap participants to WAIT with swap-specific reasons, preventing GridOccupancy deadlocks from A<->B swaps slipping through the conflict path (2025-11-20 19:05)
- `INC-2025-1154-R1` - Applied the same corner-cutting rule used by the distance field and LOS checks to `UEnemyAISubsystem::FindAlternateMoveCells`, so diagonal move candidates are rejected whenever either orthogonal shoulder is blocked, preventing enemies from slipping diagonally around wall corners during alternate move selection (2025-11-20 18:30)
- `INC-2025-1153-R1` - Updated `UDistanceFieldSubsystem::GetNextStepTowardsPlayer` to prefer a straight step along the X/Y axis when the enemy is exactly aligned with the player and the tile is walkable, so "north of player" enemies advance straight south instead of taking diagonals when a direct path exists (2025-11-20 18:00)
- `INC-2025-1152-R1` - Adjusted enemy move ordering and scoring so backline units are processed first in `UEnemyAISubsystem::CollectIntents` and orthogonal steps receive a small score bonus in `ScoreMoveCandidate`, making conflicts over approach tiles resolve in favor of further units with straight-line moves while closer or diagonal-only units are more likely to WAIT (2025-11-20 17:30)
- `INC-2025-1151-R1` - Tightened enemy move intent generation in `UEnemyAISubsystem` so both primary and alternate move candidates must strictly reduce Chebyshev distance to the player, preventing side-to-side oscillation when an enemy cannot actually get closer (2025-11-20 17:00)
- `INC-2025-1150-R1` - Updated `UEnemyAISubsystem::ComputeMoveOrWaitIntent` so primary move candidates treat attacker origin cells (hard-blocked tiles) as blocked alongside already-claimed move targets, ensuring movers pick destinations after attackers have fixed their tiles instead of relying on the conflict resolver to downgrade those moves to WAIT (2025-11-20 16:30)
- `INC-2025-1149-R1` - Added `LastAcceptedCommandTag` to `AGameTurnManagerBase` and updated `OnPlayerMoveCompleted` to gate its fallback enemy-phase dispatch based on that tag instead of the cached move command, so attack completions reliably trigger enemy actions even after prior move commands in the same or previous turns (2025-11-20 15:45)
- `INC-2025-1148-R1` - Relaxed the "block moves into attack tiles" behavior in `UConflictResolverSubsystem::ResolveAllConflicts` so MOVE actions whose target cell is occupied by a stationary unit (attack tile or WAIT) first try to reroute to an adjacent cell that still reduces the distance field before being downgraded to WAIT, preventing enemies that could safely step closer to the player from stalling in place (2025-11-20 15:30)
- `INC-2025-1147-R1` - Rotated melee attackers toward their target in `UGA_MeleeAttack::ActivateAbility` using the cached front-cell location from `ComputeTargetFacingInfo`, so both enemies and the player visually face the struck unit when playing the melee montage (2025-11-20 15:10)
- `INC-2025-1146-R1` - Updated `UTurnCommandHandler::ProcessPlayerCommand` and `AGameTurnManagerBase::OnPlayerMoveCompleted` so validated `InputTag_Move` commands still allow downstream MovePrecheck/ACK to run, and attack/turn completions re-derive sequential vs simultaneous enemy flow from cached intents even when no move was accepted, ensuring enemies act after a player attack and the first move of Turn 0 is no longer dropped (2025-11-20 15:00)
- `INC-2025-1145-R1` - Prevent stale attack completions from calling `TurnActionBarrierSubsystem::CompleteAction` on the wrong `TurnId` by checking the barrier's current turn inside `UGA_AttackBase::EndAbility`, avoiding cross-turn pollution of the barrier state (2025-11-20 14:00)
- `INC-2025-1144-R1` - Added an idempotency guard inside `UGA_AttackBase::EndAbility` so duplicate completion calls are ignored before any barrier updates, per `FIX_PLAN.md` (2025-11-20 13:30)
- `INC-2025-1142-R1` - Ensure `GA_TurnActionBase` and its attack/wait derivatives send the current `TurnId` in `Gameplay.Event.Turn.Ability.Completed` so downstream turn logic stops processing stale payloads per `FIX_PLAN.md` (2025-11-20 12:30)
- `INC-2025-1141-R1` - Guard `UGA_MeleeAttack::OnMontageCompleted` against duplicate invocations so a single attack ability cannot end twice, per `FIX_PLAN.md` (2025-11-20 12:00)

### 2025-11-19

- `INC-2025-1135-R1` - Fixed encoding issues by correcting garbled comments (7 locations: ・・lueprintNativeEvent・・ → BlueprintNativeEvent) and translating all Japanese comments to English in `GameTurnManagerBase.h` (2 locations), `GameTurnManagerBase.cpp` (16 locations), and `TurnCommandEncoding.h` (2 locations) to prevent SHIFT-JIS misinterpretation by AI tools (2025-11-19 17:25)
- `INC-2025-1133-R1` - Added player attack command handling inside `AGameTurnManagerBase::OnPlayerCommandAccepted_Implementation` so `InputTag_Attack` fires a GameplayEvent toward the target actor (2025-11-19 01:10)
- `INC-2025-1132-R1` - Added an anti-teleport guard in the CoreResolvePhase so movement intents that differ from live occupancy by more than one Chebyshev step become WAITs and emit a desync warning (2025-11-19 00:57)

### 2025-11-18

- `INC-2025-1118-R1` - Implements Priority 2.1 three-pass revalidation so stationary blockers lock out conflicting moves instead of triggering `GridOccupancy` rejection (2025-11-18 13:10)

### 2025-11-17

- `INC-2025-00030-R8` - Refactor `GA_MoveBase` so it only executes the ConflictResolver-provided `TargetCell`, and update `GameTurnManagerBase` to trigger the ability with that encoded cell while `OnPlayerCommandAccepted` now only acknowledges the command. Files modified: `Abilities/GA_MoveBase.cpp`, `Turn/GameTurnManagerBase.cpp`, `Documentation/CODE_REVISION.md` (2025-11-17 01:50)
- `INC-2025-00030-R7` - Keep resolved wait actions flowing through `TurnCorePhaseManager::CoreExecutePhase` so they reach `DispatchResolvedMove`, ensuring the barrier registration/completion logic runs before Wait intents hit GAS. Files modified: `Turn/TurnCorePhaseManager.cpp`, `Documentation/CODE_REVISION.md` (2025-11-17 01:50)
- `INC-2025-00030-R6` - Delay `EndEnemyTurn()` after barrier completion using `SetTimer` to avoid racing with `GA_MoveBase::EndAbility` tag cleanup so `CanAdvanceTurn()` sees clean state. Files modified: `Turn/GameTurnManagerBase.cpp`, `Turn/GameTurnManagerBase.h`, `Documentation/CODE_REVISION.md` (2025-11-17 02:55)
- `INC-2025-00030-R5` - Split attack and move completion callbacks in `GameTurnManagerBase` so `AttackPhaseExecutorSubsystem::OnFinished` triggers the sequential move phase while the barrier callback only ends the turn. Files modified: `Turn/GameTurnManagerBase.h`, `Turn/GameTurnManagerBase.cpp`, `Documentation/CODE_REVISION.md` (2025-11-17 02:00)
- `INC-2025-00030-R4` - Registered wait actions with TurnActionBarrierSubsystem and marked them complete immediately so idle moves no longer leave the barrier stuck in-progress. Files modified: `Turn/GameTurnManagerBase.cpp`, `Documentation/CODE_REVISION.md` (2025-11-17 01:00)
- `INC-2025-00030-R3` - Ensured GA_MoveBase completes its registered TurnActionBarrierSubsystem action when the ability ends so player moves release the barrier and the turn can advance. Files modified: `Abilities/GA_MoveBase.cpp`, `Documentation/CODE_REVISION.md` (2025-11-17 01:00)
- `INC-2025-00030-R1` - Completed migration from `AGridPathfindingLibrary` to `UGridPathfindingSubsystem` in movement components and abilities. Updated `UUnitMovementComponent::MoveUnitAlongGridPath()` to remove `AGridPathfindingLibrary*` parameter and use subsystem internally. Updated `UUnitMovementComponent::FinishMovement()` to use `UGridPathfindingSubsystem` for grid operations. Migrated all `GetPathFinder()` calls in `GA_MoveBase` to `GetGridPathfindingSubsystem()` (10+ locations). Updated function signatures: `ComputeFixedZ()` now accepts `const UGridPathfindingSubsystem*` instead of `const AGridPathfindingLibrary*`. Removed `CachedPathFinder` member variable from `GA_MoveBase`. Updated forward declarations in headers. All movement validation and grid operations now use the new subsystem architecture. Deprecated `GetPathFinder()` function kept for backward compatibility during transition. Files modified: `Character/UnitMovementComponent.h`, `Character/UnitMovementComponent.cpp`, `Abilities/GA_MoveBase.h`, `Abilities/GA_MoveBase.cpp`, `Documentation/CODE_REVISION.md` (2025-11-16 23:55)
- `INC-2025-00029-R1` - Removed `CachedPlayerPawn` member variable from `AGameTurnManagerBase` as part of architecture improvement plan Phase 3.2. Replaced all 55 usages with local `APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0)` calls in each function that needs player pawn access. Updated functions: `InitializeTurnSystem`, `CollectEnemies` (3 instances), `GetPlayerASC`, `OnTurnStartedHandler`, `ContinueTurnAfterInput`, `ExecuteMovePhase`, `NotifyPlayerPossessed`. Removed caching assignments and member variable declaration. All player pawn accesses now use direct `GetPlayerPawn()` calls instead of cached reference. Files modified: `Turn/GameTurnManagerBase.h`, `Turn/GameTurnManagerBase.cpp`, `Documentation/CODE_REVISION.md` (2025-11-16 00:00)
- `INC-2025-00028-R1` - Replaced `CurrentTurnIndex` with `CurrentTurnId` in `AGameTurnManagerBase` as part of architecture improvement plan Phase 3.1. Replaced all log messages and critical logic to use `CurrentTurnId` from `UTurnFlowCoordinator` instead of local `CurrentTurnIndex`. `CurrentTurnIndex` is now synchronized with `TurnFlowCoordinator->GetCurrentTurnIndex()` for backward compatibility but no longer used in core logic. Replication and synchronization logic for `CurrentTurnIndex` kept for transition period. Updated approximately 40+ log messages and function calls. Files modified: `Turn/GameTurnManagerBase.cpp` (2025-11-16 00:00)
- `INC-2025-00027-R1` - Migrated grid pathfinding functionality from `AGridPathfindingLibrary` Actor to `UGridPathfindingSubsystem` as part of architecture improvement plan Phase 2. Created new `UGridPathfindingSubsystem` class inheriting from `UWorldSubsystem` with all pathfinding functionality. Updated `GameTurnManagerBase::HandleDungeonReady()` to initialize Subsystem. Added `GetGridPathfindingSubsystem()` accessor methods to `GameTurnManagerBase` and `GA_MoveBase`. Removed PathFinder Actor spawning from `TBSLyraGameMode::SpawnBootstrapActors()`. Legacy `GetCachedPathFinder()` methods kept for backward compatibility during migration. Files modified: `Grid/GridPathfindingSubsystem.h` (new), `Grid/GridPathfindingSubsystem.cpp` (new), `Turn/GameTurnManagerBase.h`, `Turn/GameTurnManagerBase.cpp`, `Abilities/GA_MoveBase.h`, `Abilities/GA_MoveBase.cpp`, `Turn/TBSLyraGameMode.cpp` (2025-11-16 00:00)
- `INC-2025-00026-R1` - Fixed garbled Japanese comments (mojibake) in `GA_MoveBase.h` as part of architecture improvement plan Phase 1. Converted file encoding to UTF-8 (BOM-less) and corrected all garbled Japanese comments throughout the file. Fixed approximately 20+ comment lines including class description, function comments, and section headers. All comments are now readable and properly encoded. Files modified: `Abilities/GA_MoveBase.h` (2025-11-16 00:00)
- `INC-2025-00025-R1` - Converted attack intents to wait before dispatching the sequential move-only phase so those intents no longer trigger attacks again and all move/wait actions are properly tracked by the barrier (2025-11-17 19:25)
- `INC-2025-00024-R1` - Registered enemy move actions with `TurnActionBarrierSubsystem` and completed them when the manual `OnMoveFinished` delegate fires so the barrier waits for all units before ending the turn (2025-11-17 19:20)
- `INC-2025-00023-R1` - Trigger sequential move-only intent execution after the attack phase completes so non-attacking enemies move before the enemy turn ends (2025-11-17 19:15)
- `INC-2025-00022-R1` - Added rotation handling in `UGA_MoveBase::OnMoveFinished` so actors face their movement direction and corrected `GA_MeleeAttack` to derive rotation from the attack direction (2025-11-17 19:00)
- `INC-2025-00021-R1` - Separated responsibilities of `AGridPathfindingLibrary` as part of refactoring plan section 7. Phase 1: Removed unused `IsCellWalkableAtWorldPosition()` wrapper function. Phase 2: Replaced all `IsCellWalkable()` calls (5 locations) with `IsCellWalkableIgnoringActor()` in `DistanceFieldSubsystem.cpp`, `EnemyThinkerBase.cpp`, `EnemyAISubsystem.cpp`, and `GA_MoveBase.cpp`, then removed `IsCellWalkable()` and `DoesCellContainBlockingActor()` helper function. Phase 3: Replaced `GetActorsAtGridPosition()` calls (3 locations) with direct `UGridOccupancySubsystem` access in `DetectInExpandingVision()`, `DetectInRadius()`, and `SearchAdjacentTiles()`, removed `GetActorAtPosition()` and `GetActorsAtGridPosition()` functions, removed `ReturnGridStatusIgnoringSelf()` function. Phase 4: Removed static distance functions (`GetChebyshevDistance()`, `GetManhattanDistanceGrid()`, `GetEuclideanDistanceGrid()`) - use `FGridUtils` directly. All fallback logic using `TActorIterator` removed. `AGridPathfindingLibrary` now focused solely on pathfinding and terrain management, with clear separation from `UGridOccupancySubsystem`. Files modified: `Grid/GridPathfindingLibrary.h`, `Grid/GridPathfindingLibrary.cpp`, `Turn/DistanceFieldSubsystem.cpp`, `AI/Enemy/EnemyThinkerBase.cpp`, `AI/Enemy/EnemyAISubsystem.cpp`, `Abilities/GA_MoveBase.cpp` (2025-11-17 15:40)
- `INC-2025-00020-R1` - Declared `TryGetASC`/`GetTeamIdOf` before their use and localized enemy subsystem references while building observations so `Turn/GameTurnManagerBase.cpp` compiles without duplicate definitions after the movement refactor (2025-11-17 14:00)
- `INC-2025-00019-R1` - Removed 16 wrapper functions from `AGameTurnManagerBase` as part of refactoring plan section 6. Phase 1: Removed 8 unused functions (`BuildAllObservations`, `SendGameplayEventWithResult`, `SendGameplayEvent`, `GetPlayerController_TBS`, `GetPlayerPawnCachedOrFetch`, `EnsureFloorGenerated`, `NextFloor`, `WarpPlayerToStairUp`). Phase 2: Replaced internal calls to `GetPlayerPawn()` (6 locations), `CollectEnemies()` (3 locations), `CollectIntents()` (1 location), and `HasAnyAttackIntent()` (3 locations) with direct subsystem calls. Phase 3: Removed function declarations and implementations for replaced functions. Phase 4: Removed `GetDungeonSystem()` and `GetFloorGenerator()` wrappers. All wrapper functions now replaced with direct subsystem access (`UGameplayStatics`, `EnemyAISubsystem`, `EnemyTurnDataSubsystem`, `DungeonSystem` member). Files modified: `Turn/GameTurnManagerBase.h`, `Turn/GameTurnManagerBase.cpp` (2025-11-16 15:30)
- `INC-2025-00018-R1` - Rebound `UGA_MoveBase` to the new `UUnitMovementComponent::OnMoveFinished` delegate, ensuring `TurnBarrier` receives valid actor and action context after the movement refactor (2025-11-17 13:15)
- `INC-2025-00017-R1` - Fixed swap move blocking in `UConflictResolverSubsystem`. Reversed swap detection logic to ALLOW swap moves instead of blocking them. When Actor A moves to cell B and Actor B moves to cell A (swap pattern detected), both moves are now permitted instead of forcing both actors to Wait. Updated resolution reason from "Blocked by swap detection" to "Swap move allowed". This fixes the issue where player moves were incorrectly canceled when swapping positions with enemies. Files modified: `Turn/ConflictResolverSubsystem.cpp` (2025-11-16 14:10)
- `INC-2025-00016-R2` - Added occupancy retry logic in AUnitBase::StartNextLeg() and UGA_MoveBase::OnMoveFinished() to retry UpdateActorCell() before broadcasting completion.
- `INC-2025-00016-R1` - Integrated `IsMoveValid()` API into AI decision-making pipeline. Replaced manual Chebyshev distance calculations with `FGridUtils::ChebyshevDistance()` in `UEnemyAISubsystem::BuildObservations()` and `UEnemyThinkerBase::ComputeIntent_Implementation()`. Added IsMoveValid validation in `ComputeIntent_Implementation()` to verify AI movement intentions before committing, preventing invalid moves (corner-cutting, terrain blocking, occupancy conflicts). AI now falls back to Wait intent when proposed move is rejected. Files modified: `AI/Enemy/EnemyAISubsystem.cpp`, `AI/Enemy/EnemyThinkerBase.cpp` (2025-11-16 14:00)
- `INC-2025-00015-R1` - Created unified movement validation API `IsMoveValid()` in `AGridPathfindingLibrary` consolidating Chebyshev distance (8-directional), terrain walkability, corner-cutting prevention, and occupancy checks. Refactored `AGameTurnManagerBase::OnPlayerCommandAccepted_Implementation()` to use new API (removed ~70 lines of inline validation logic). Simplified `AUnitBase::MoveUnit()` to delegate to `UUnitMovementComponent`, removed obsolete `StartNextLeg()` and `UpdateMove()` implementations (171 lines removed). Translated all Japanese comments in `GridPathfindingLibrary.cpp` to English (44 comment lines fixed). Files modified: `Grid/GridPathfindingLibrary.h`, `Grid/GridPathfindingLibrary.cpp`, `Turn/GameTurnManagerBase.cpp`, `Character/UnitBase.h`, `Character/UnitBase.cpp` (2025-11-16 13:55)
- `INC-2025-1117G-R1` - Implement sequential enemy turn flow and caching per `FIX_PLAN_7.md` (2025-11-17 19:30)
- `INC-2025-1117H-R1` - Generalize `ExecuteAttacks` and add shared move dispatching per `FIX_PLAN_8.md` (2025-11-17 19:10)
- `INC-2025-1117DIAG-R1` - Added diagnostic ASC tag logging before sequential attacks to aid FIX_PLAN_10 investigation (2025-11-17 19:30)
- `INC-2025-1117-FINAL-FIX-R3` - Added `State.Turn.Active` tag handling around the attack executor flow per `FIX_PLAN_13.md` (2025-11-17 20:00)
- `INC-2025-1117-FINAL-FIX-R2` - Added `AI_Intent_Attack` asset tags so `GA_AttackBase` triggers now match the resolved intent tags (2025-11-17 19:50)
- `INC-2025-1117-FINAL-FIX-R1` - Fixed `GA_AttackBase` trigger tags to use `AI_Intent_Attack` so sequential attacks fire the right ability (2025-11-17 19:40)
- `INC-2025-1117-ROLLBACK-R1` - Reverted attack tag adjustments to match asset expectations per `FIX_PLAN_15.md` (2025-11-17 20:20)

### 2025-11-15

- `INC-2025-00014-R1` - Removed ALL Japanese comments from `GameTurnManagerBase.cpp`. Deleted approximately 164 lines containing Japanese characters or garbled text (☁E, ↁE, ✁E, etc.) to clean up the codebase. All Japanese comment lines have been completely removed (2025-11-15 23:58)
- `INC-2025-00013-R1` - Translated remaining Japanese comments in `GameTurnManagerBase.cpp` to English. Fixed approximately 200+ Japanese comments throughout the file, including command processing, movement validation, occupancy checks, and helper functions. Work in progress - approximately 157 comments remaining (2025-11-15 23:55)
- `INC-2025-00012-R1` - Translated all Japanese text in log messages within `GameTurnManagerBase.cpp` to English. Fixed garbled characters in log messages (ↁE, ☁E, ✁E, etc.) and converted Japanese log text to English for consistency (2025-11-15 23:50)
- `INC-2025-00011-R1` - Fixed garbled and Japanese comments in `GameTurnManagerBase.h` and `GameTurnManagerBase.cpp`, translated all comments to English. Fixed encoding issues including garbled characters (繁E 縺, ☁E, etc.) and converted Japanese documentation to English for better maintainability (2025-11-15 23:45)
- `INC-2025-00010-R1` - Removed redundant wrapper function `ExecuteSimultaneousMoves()` from `AGameTurnManagerBase`. This function was a simple wrapper that only called `ExecuteSimultaneousPhase()` and was unused (no C++ call sites, not exposed to Blueprint). Code should call `ExecuteSimultaneousPhase()` directly instead (2025-11-15 22:45)
- `INC-2025-00009-R1` - Removed redundant wrapper functions `TM_ReturnGridStatus()` and `TM_GridChangeVector()` from `AGameTurnManagerBase`. These functions were simple wrappers around `ADungeonFloorGenerator` methods and created confusion with duplicate functionality in `AGridPathfindingLibrary`. Blueprint code should use `GetFloorGenerator()->ReturnGridStatus()` or `GetFloorGenerator()->GridChangeVector()` directly instead (2025-11-15 22:30)
- `INC-2025-00008-R1` - Removed Blueprint-exposed wall manipulation functions (`SetWallCell()`, `SetWallAtWorldPosition()`, `SetWallRect()`) from `AGameTurnManagerBase`. Removed duplicate input window functions (`OpenInputWindowForPlayer()`, `CloseInputWindowForPlayer()`) and unified input window management through `UPlayerInputProcessor` API (`OpenInputWindow()`, `CloseInputWindow()`). Updated call sites in `NotifyPlayerPossessed()` and `OnPlayerCommandAccepted_Implementation()` to use the unified API (2025-11-15 22:00)
- `INC-2025-00007-R1` - Removed obsolete ability tracking functions (`NotifyAbilityStarted()`, `OnAbilityCompleted()`, `WaitForAbilityCompletion_Implementation()`) and AI pipeline functions (`BuildObservations_Implementation()`, `ComputeEnemyIntent_Implementation()`) replaced by `UTurnActionBarrierSubsystem` and `EnemyAISubsystem`. Updated `GA_WaitBase` to use Barrier subsystem exclusively (2025-11-15 21:00)
- `INC-2025-00006-R1` - Removed unused legacy functions from `AGameTurnManagerBase`: `RunTurn()`, `ProcessPlayerCommand()`, `StartTurnMoves()` (2025-11-15 20:30)

### 2025-11-14

- `INC-2025-00005-R1` - Updated `TurnManager` debug messages to ASCII, updated `DungeonFloorGenerator::BSP_Split` to use latest `stack.Pop()` API (2025-11-14 23:00)
- `INC-2025-00004-R1` - Fixed GridOccupancy corruption by dropping stale turn notifications in `OnAttacksFinished` (2025-11-14 22:50)
- `INC-2025-00003-R1` - Updated `response_note.md` and this file to include "CodeRevision" usage policy (2025-11-14 22:24)
- `INC-2025-00002-R1` - Added detailed pathfinding retry logic to `DistanceFieldSubsystem::GetNextStepTowardsPlayer()`.
- `INC-2025-00001-R2` - Added `WindowId` ACK handling in `APlayerControllerBase::Client_ConfirmCommandAccepted_Implementation`.
- `INC-2025-00001-R1` - Added barrier notification before `AGameTurnManagerBase::EndEnemyTurn()` (barrier synchronization fix).

### Legacy / Unknown Dates

- `INC-2025-00035-R1` - Added `UTBSDamageExecution` custom damage calculation class for TBS. Implements `Execute_Implementation()` to calculate damage using Attack, Defense, Crit (CritChance/CritMultiplier), and damage modifiers (DamageDealtRate/DamageTakenRate) from `TBSAttributeSet_Combat`. Includes `FTBSDamageStatics` structure for attribute capture definitions. Supports SetByCaller.Damage tag for skill power specification. Files modified: `Character/UTBSDamageExecution.h` (new), `Character/UTBSDamageExecution.cpp` (new), `Documentation/CODE_REVISION.md` (2025-01-XX XX:XX)
- `INC-2025-00034-R1` - Added `PreAttributeChange()` and `PostGameplayEffectExecute()` override functions to `UTBSAttributeSet_Combat`. `PreAttributeChange()` clamps Health attribute between 0 and MaxHealth. `PostGameplayEffectExecute()` checks for death condition when Health reaches 0 or below (includes TODO for death tag/event handling). Files modified: `Character/TBSAttributeSet_Combat.h`, `Character/TBSAttributeSet_Combat.cpp`, `Documentation/CODE_REVISION.md` (2025-01-XX XX:XX)
- `INC-2025-00033-R1` - Added `UTBSAttributeSet_Combat` AttributeSet class for TBS combat attributes. Provides common combat attributes: HP (Health/MaxHealth), Attack, Defense, Speed, Accuracy, Evasion, Crit (CritChance/CritMultiplier), and Damage modifiers (DamageDealtRate/DamageTakenRate). Follows Lyra GAS patterns with `ATTRIBUTE_ACCESSORS` macro and proper replication. Initialized with `SetBaseValue()` in constructor. Files modified: `Character/TBSAttributeSet_Combat.h` (new), `Character/TBSAttributeSet_Combat.cpp` (new), `Documentation/CODE_REVISION.md` (2025-01-XX XX:XX)
- `INC-2025-00032-R1` - Removed duplicate and legacy PathFinder accessor functions. Replaced all `GetCachedPathFinder()` calls with `GetGridPathfindingSubsystem()` (2 locations: `GameTurnManagerBase.cpp`, `PlayerControllerBase.cpp`). Removed `GetCachedPathFinder()` function declaration and implementation. Removed `CachedPathFinder` member variable from `GameTurnManagerBase` (kept `PathFinder` only). Removed legacy PathFinder Actor initialization code from `HandleDungeonReady()` (PathFinder is now Subsystem, not Actor). Cleaned up `ResolveOrSpawnPathFinder()` function with clarifying comments. Removed `FindGridLibrary()` helper function from `GA_MeleeAttack.cpp` (replaced with direct `GetSubsystem()` calls). Replaced `GetCurrentTurnIndex()` usage in `PlayerControllerBase.cpp` with `TurnFlowCoordinator->GetCurrentTurnIndex()`. Removed `TBSLyraGameMode::GetPathFinder()` function (no call sites, PathFinder is now Subsystem). Files modified: `Turn/GameTurnManagerBase.h`, `Turn/GameTurnManagerBase.cpp`, `Player/PlayerControllerBase.cpp`, `Abilities/GA_MeleeAttack.cpp`, `Turn/TBSLyraGameMode.h`, `Documentation/CODE_REVISION.md` (2025-01-XX XX:XX)
- `INC-2025-00031-R1` - Cleaned up duplicate and deprecated movement-related functions. Removed unused `CalculateNextTilePosition()` function from `GA_MoveBase` (no call sites found). Removed deprecated `GA_MoveBase::GetPathFinder()` function (replaced by `GetGridPathfindingSubsystem()`). Replaced legacy `GridChangeVector()` call with `SetGridCostAtWorldPosition()` in `GA_MoveBase::UpdateGridState()`. Added usage comments to `SnapToCellCenter()` and `SnapToCellCenterFixedZ()` functions to clarify they return coordinates only and do not move actors (use `GridUtils::SnapActorToGridCell()` for actor movement). Files modified: `Abilities/GA_MoveBase.h`, `Abilities/GA_MoveBase.cpp`, `Documentation/CODE_REVISION.md` (2025-01-XX XX:XX)

---

## Superseded Revisions

The following revisions have been superseded by later fixes and are kept for historical reference:

- `INC-2025-1122-ADJ-ATTACK-R2` - [SUPERSEDED by R3] Removed UpgradeIntentsForAdjacency call that was incorrectly causing distant enemies to attack (`Turn/GameTurnManagerBase.cpp`) (2025-11-22 20:55)
- `INC-2025-1122-ADJ-ATTACK-R1` - [SUPERSEDED by R3] Fixed adjacent enemies not attacking when player moves away (`Turn/GameTurnManagerBase.cpp`) (2025-11-22 20:30)
- `INC-2025-1122-SIMUL-R4` - [SUPERSEDED by INC-2025-1122-ATTACK-SEQ-R1] Trigger `ExecuteEnemyPhase` for attack commands too, ensuring enemy phase runs for all player actions (`Turn/TurnCommandHandler.cpp`) (2025-11-22)
