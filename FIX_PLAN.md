# GameTurnManagerBase Refactoring Plan

## 1. Objective

The primary objective of this refactoring is to reduce the size and complexity of the `GameTurnManagerBase` class, which currently exceeds 4000 lines of code. We will achieve this by delegating its core responsibilities to a set of dedicated `UWorldSubsystem`s. The `GameTurnManagerBase` will evolve into a high-level coordinator, orchestrating the turn-based flow by invoking these specialized subsystems.

## 2. Pre-existing Subsystems to Integrate

Our analysis has identified several existing subsystems that already encapsulate specific functionalities. The first step is to ensure `GameTurnManagerBase` fully utilizes them, removing any redundant logic from its own implementation.

- **`UTurnFlowCoordinator`**: Manages turn progression, Action Points (AP), and Turn/Input IDs.
- **`UTurnCommandHandler`**: Manages player command validation and processing.
- **`UAttackPhaseExecutorSubsystem`**: Manages the sequential execution of attack abilities.
- **`UTurnDebugSubsystem`**: Centralizes all debugging output (logs, on-screen display, CSV).
- **`UTurnActionBarrierSubsystem`**: Manages the synchronization of turn actions (Barrier), ensuring all actions complete before the turn proceeds.
- **`UGridPathfindingSubsystem`**: Handles all grid-based pathfinding and coordinate conversions.
- **`UGridOccupancySubsystem`**: Manages grid cell occupancy.
- **`UEnemyAISubsystem`**: Handles enemy unit collection, observation building, and intent generation.

## 3. Utility Extraction

To further declutter `GameTurnManagerBase.cpp`, generic helper functions that do not depend on the manager's state should be moved to the `Utility` folder.

**Target**: `Utility/RogueActorUtils.h`
**Functions to Extract**:
- `TryGetASC(const AActor* Actor)`: Retrieves the AbilitySystemComponent from an Actor or its Controller.
- `GetTeamIdOf(const AActor* Actor)`: Retrieves the Team ID from an Actor or its Controller via `IGenericTeamAgentInterface`.

## 4. New Subsystem Proposal: `UUnitTurnStateSubsystem`

A significant portion of `GameTurnManagerBase`'s current logic involves tracking the state of individual units within a turn (e.g., has the unit acted, can it act, is it ready?). This responsibility will be extracted into a new subsystem.

**Name**: `UUnitTurnStateSubsystem`
**Responsibilities**:
- Maintain a list of all active units (Player + Enemy + Ally) participating in the current turn.
- Track the turn-specific state for each unit (e.g., `bHasActed`, `bHasMoved`).
- Provide functions to query for ready units (e.g., `GetNextUnitToAct`).
- Provide functions to update a unit's state (e.g., `MarkUnitAsActed`).
- Manage the turn order/initiative of units.

## 5. Refactoring Steps

- [x] **Step 3: Refactor `GameTurnManagerBase.cpp` - Command Handling**
    - [x] Delegate `OnPlayerCommandAccepted_Implementation` to `UTurnCommandHandler`.
    - [x] Remove `ExecutePlayerMove` and delegate to `UTurnCommandHandler`.

- [x] **Step 4: Refactor `GameTurnManagerBase.cpp` - Attack Phase**
    - [x] Delegate `ExecuteAttacks` to `UAttackPhaseExecutorSubsystem`.
    - [x] Ensure `ExecuteSequentialPhase` calls the delegated `ExecuteAttacks`.

- [x] **Step 5: Refactor `GameTurnManagerBase.cpp` - Move Phase**
    - [x] Extract fallback logic from `ExecuteMovePhase` to `EnsureEnemyIntents`.
    - [x] Ensure `ExecuteMovePhase` uses `EnsureEnemyIntents`.

- [x] **Step 6: Implement `UUnitTurnStateSubsystem`**
    - [x] Create `UnitTurnStateSubsystem` class.
    - [x] Integrate into `GameTurnManagerBase` (used in `OnTurnStartedHandler`).

- [x] **Step 7: Refactor `OnTurnStartedHandler` - Turn Initialization** (2025-11-22)
    - [x] Create `UTurnInitializationSubsystem` to handle turn initialization logic.
    - [x] Extract DistanceField update logic to `UpdateDistanceField()`.
    - [x] Extract GridOccupancy initialization to `InitializeGridOccupancy()`.
    - [x] Extract preliminary intent generation to `GeneratePreliminaryIntents()`.
    - [x] Simplify `OnTurnStartedHandler` from 210 lines to ~60 lines.
    - [x] **Result**: Reduced `GameTurnManagerBase.cpp` from 3110 lines to 2512 lines (~598 lines, 19% reduction).
    - [x] Compile and Test: ✅ Build successful.

- [x] **Step 8: Refactor `OnPlayerMoveCompleted` - Player Move Handler** (2025-11-22)
    - [x] Create `UPlayerMoveHandlerSubsystem` to handle post-move logic.
    - [x] Extract turn notification validation logic.
    - [x] Extract enemy intent regeneration logic.
    - [x] Extract DistanceField update after player move.
    - [x] Simplify `OnPlayerMoveCompleted` from 134 lines to 110 lines.
    - [x] **Result**: Reduced `GameTurnManagerBase.cpp` from 2512 lines to 2481 lines (~31 lines, 1.2% reduction).
    - [x] **Cumulative**: Reduced from 3110 lines to 2481 lines (~629 lines, 20.2% reduction).
    - [x] Compile and Test: ✅ Build successful.

- [ ] **Step 9: Final Cleanup and Verification**
    - [ ] Verify all subsystems are correctly initialized and used.
    - [ ] Check for any remaining large functions in `GameTurnManagerBase` to refactor.
    - [ ] Compile and Test.
- **Search**: Find all instances of `UE_LOG(LogGameTurn, ...)` and other direct debug outputs (e.g., `GEngine->AddOnScreenDebugMessage`) within `GameTurnManagerBase.cpp`.
- **Replace**: Replace these direct calls with function calls to the `UTurnDebugSubsystem`.
    - **Example (Diff-like):**
      ```diff
      - UE_LOG(LogGameTurn, Log, TEXT("Phase changing from %s to %s"), *OldPhase.ToString(), *NewPhase.ToString());
      + if (UTurnDebugSubsystem* DebugSys = GetWorld()->GetSubsystem<UTurnDebugSubsystem>())
      + {
      +     DebugSys->LogPhaseTransition(OldPhase, NewPhase);
      + }
      ```

### Step 4: Integrate `TurnFlowCoordinator`
- Remove member variables like `CurrentTurnId`, `CurrentTurnIndex`, `PlayerAP` from `GameTurnManagerBase`.
- Replace direct access to these variables with calls to the `UTurnFlowCoordinator`.
    - **Example (Diff-like):**
      ```diff
      - CurrentTurnId++;
      + if (UTurnFlowCoordinator* FlowCoord = GetWorld()->GetSubsystem<UTurnFlowCoordinator>())
      + {
      +     FlowCoord->AdvanceTurn();
      + }

      - if (PlayerAP >= Cost)
      + if (FlowCoord && FlowCoord->HasSufficientAP(Cost))
      ```

### Step 5: Integrate `TurnCommandHandler`
- Remove all command processing logic from `GameTurnManagerBase`, especially functions that handle `FPlayerCommand`.
- Delegate the command directly to `UTurnCommandHandler`.
    - **Example (Diff-like):**
      ```diff
      // In GameTurnManagerBase, when a command is received from the player controller:
      - if (ValidateCommand(PlayerCommand))
      - {
      -     // ... extensive validation and processing logic ...
      - }
      + if (UTurnCommandHandler* CommandHandler = GetWorld()->GetSubsystem<UTurnCommandHandler>())
      + {
      +     CommandHandler->ProcessPlayerCommand(PlayerCommand);
      + }
      ```
- The functions for opening and closing the input window should also be delegated.

### Step 6: Integrate `AttackPhaseExecutorSubsystem` & `TurnActionBarrierSubsystem`
- Remove logic from `GameTurnManagerBase` that manually loops through attacks or waits for abilities.
- Use `UAttackPhaseExecutorSubsystem` to handle the attack sequence.
- Use `UTurnActionBarrierSubsystem` to wait for all actions (Move/Attack/Wait) to complete.
    - **Example (Diff-like):**
      ```diff
      - // Old logic with manual queue and waiting
      - ProcessNextAttackInQueue(); 
      
      + // New logic
      + UAttackPhaseExecutorSubsystem* AttackExecutor = GetWorld()->GetSubsystem<UAttackPhaseExecutorSubsystem>();
      + if (AttackExecutor)
      + {
      +     AttackExecutor->OnFinished.AddDynamic(this, &AGameTurnManagerBase::OnAttackPhaseCompleted);
      +     AttackExecutor->BeginSequentialAttacks(ResolvedAttacks, CurrentTurnId);
      + }
      ```

### Step 7: Integrate `UUnitTurnStateSubsystem` & `UEnemyAISubsystem`
- Remove unit arrays and state-tracking variables (e.g., `ActiveUnits`, `UnitReadyList`) from `GameTurnManagerBase`.
- Delegate enemy collection and intent gathering to `UEnemyAISubsystem`.
- Delegate unit state tracking to `UUnitTurnStateSubsystem`.
    - **Example (Diff-like):**
      ```diff
      - CollectEnemies(); // Internal function
      - BuildAllObservations(); // Internal function
      
      + UEnemyAISubsystem* EnemyAI = GetWorld()->GetSubsystem<UEnemyAISubsystem>();
      + if (EnemyAI)
      + {
      +     EnemyAI->CollectAllEnemies(PlayerPawn, OutEnemies);
      +     EnemyAI->BuildObservations(PlayerGrid);
      +     EnemyAI->CollectIntents(Observations, Enemies);
      + }
      ```

### Step 8: Integrate Grid Subsystems
- Remove any legacy grid logic or direct access to `AGridPathfindingLibrary` (if any remains).
- Ensure all grid queries go through `UGridPathfindingSubsystem` and `UGridOccupancySubsystem`.

## 6. Expected Outcome

After these changes, `GameTurnManagerBase.cpp` should be significantly shorter and consist primarily of high-level state machine logic (`SetPhase`, `OnPhaseChanged`). The complex implementation details will be properly encapsulated within their respective subsystems, improving modularity, testability, and maintainability.