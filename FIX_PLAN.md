# Refactoring Plan v5: Prioritizing Mover Intent Generation

## 1. Goal

The current "Two-Pass" AI with scoring (v4) correctly avoids collisions and makes better tactical choices, but a new issue has been observed in narrow corridors: the "wrong" enemy moves first. This plan details the implementation of a priority system for generating mover intents to ensure more natural queueing and line-forming behavior.

## 2. Problem Summary

-   **Symptom**: In a narrow corridor, when the player moves forward, an enemy unit to the side of the corridor is moving into the vacated space before the unit directly behind the player can. This breaks the illusion of a cohesive squad queueing to advance.
-   **Root Cause**: The `CollectIntents` function's "Pass 2" (for movers) processes enemies in their default array order (i.e., spawn order). When multiple enemies identify the same vacated cell as their optimal move target, the one that happens to be first in the array "claims" the spot via `ClaimedMoveTargets`. The tactically more appropriate unit (e.g., the one directly behind the player) is then forced to find a suboptimal alternate path because it is processed later.
-   **Conclusion**: The order of intent generation for movers is currently arbitrary, not tactical.

## 3. Proposed Architecture: Sorting Mover Candidates

To fix this, we will introduce an explicit sorting step into "Pass 2" of the `CollectIntents` function. Instead of processing movers in their default array order, we will sort them based on who has the "right of way".

### 3.1. New `CollectIntents` Pass 2 Logic

1.  **Gather Mover Candidates**: After Pass 1 (Attackers) is complete, iterate through all enemies and create a list of indices for units that still need an intent (i.e., they are not attackers).
2.  **Sort Mover Candidates**: Sort this list of candidate indices based on the following criteria, in order:
    a. **Primary Sort (Chebyshev Distance)**: Ascending. Units closer to the player get to decide their move first.
    b. **Secondary Sort (Manhattan Distance)**: Ascending. As a tie-breaker for units at the same Chebyshev distance, this prioritizes units in a straight line (cardinal directions) over units at a diagonal.
    c. **Tertiary Sort (Index)**: Ascending. A final tie-breaker to ensure sort stability.
3.  **Process Sorted Movers**: Iterate through the **sorted** list of indices. For each unit, call `ComputeMoveOrWaitIntent` and add the resulting intent to the output. Because the highest-priority units go first, they will claim the most desirable cells, forcing lower-priority units to pathfind around them.

## 4. Affected Files & Signatures

-   **`AI/Enemy/EnemyAISubsystem.cpp`**:
    -   The implementation of the `CollectIntents` function will be significantly modified to include the new candidate gathering and sorting logic in Pass 2.
    -   No other function signatures need to change. The intelligence is being added to the orchestration logic, not the individual computation functions.
-   **`AI/Enemy/EnemyAISubsystem.h`**:
    -   No changes are required.

## 5. Testing and Verification Plan

1.  **Compilation**: The project must compile successfully.
2.  **Corridor Scenario Re-Testing**:
    -   **Scenario**: Re-run the narrow corridor test case where the player moves forward one tile, vacating a space.
    -   **Expected Outcome**: The enemy unit positioned directly behind the player should now be prioritized by the sorting logic. It should move into the vacated space. The enemy unit to the side should be processed second and be forced to find an alternate move (e.g., moving in behind the first enemy).
3.  **Log Verification**:
    -   Add logging to show the sorted order of mover candidates before Pass 2 processing begins.
    -   Verify that the `RESERVE DEST` logs in `GridOccupancy` now show the correct, higher-priority unit claiming the contested cell first.