# FIX PLAN: Player Attack Ability Not Activating

## 1. Target Feature / Files

-   **Feature**: Player Command Processing for Attack Actions.
-   **Primary File to Modify**: `Turn/GameTurnManagerBase.cpp`
-   **Function to Modify**: `AGameTurnManagerBase::OnPlayerCommandAccepted_Implementation`

## 2. Root Cause Hypothesis and Rationale

-   **Symptom**: The player's attack ability does not activate when an attack command is issued.
-   **Hypothesis**: The `OnPlayerCommandAccepted_Implementation` function in `AGameTurnManagerBase` lacks a specific logic branch to handle player commands tagged with `InputTag_Attack`. The existing implementation only checks for `InputTag_Move` and `InputTag_Turn`.
-   **Rationale**: Any command that is not a "Move" or "Turn" command falls into a final `else` block that logs an error and takes no further action. This means the player's attack command is received by the turn manager but is never processed to trigger the corresponding gameplay ability.

## 3. Proposed Solution (Adopted)

-   An `else if` block will be added to the `OnPlayerCommandAccepted_Implementation` function to specifically handle the `InputTag_Attack`.
-   This new block will be responsible for identifying the target from the command, creating the appropriate `GameplayEvent`, and sending it to the player's Ability System Component (ASC) to trigger the attack ability.

## 4. Implementation Details

In `Turn/GameTurnManagerBase.cpp`, inside `OnPlayerCommandAccepted_Implementation`:

1.  Locate the `if/else if` structure that checks `Command.CommandTag`.
2.  Add a new `else if` block before the final `else`.

    **New Code Block:**
    ```cpp
    else if (Command.CommandTag.MatchesTag(RogueGameplayTags::InputTag_Attack))
    {
        UE_LOG(LogTurnManager, Log, TEXT("[Turn %d] Player attack command accepted."), CurrentTurnId);

        UWorld* World = GetWorld();
        APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0);
        UAbilitySystemComponent* ASC = GetPlayerASC();
        UGridOccupancySubsystem* Occupancy = World ? World->GetSubsystem<UGridOccupancySubsystem>() : nullptr;

        if (!PlayerPawn || !ASC || !Occupancy)
        {
            UE_LOG(LogTurnManager, Error, TEXT("Attack command aborted: Missing critical components (PlayerPawn, ASC, or Occupancy)."));
            return;
        }

        AActor* TargetActor = Occupancy->GetActorAtCell(Command.TargetCell);
        if (!TargetActor)
        {
            UE_LOG(LogTurnManager, Warning, TEXT("Attack command aborted: No target actor found at cell (%d, %d)."), Command.TargetCell.X, Command.TargetCell.Y);
            // Optionally, notify client of rejection
            if (APlayerControllerBase* TPCB = Cast<APlayerControllerBase>(PlayerPawn->GetController()))
            {
                TPCB->Client_NotifyMoveRejected();
            }
            return;
        }

        // Close input window now that the command is validated and accepted
        if (PlayerInputProcessor)
        {
            PlayerInputProcessor->CloseInputWindow();
        }

        FGameplayEventData EventData;
        EventData.EventTag = RogueGameplayTags::GameplayEvent_Intent_Attack;
        EventData.Instigator = PlayerPawn;
        EventData.Target = TargetActor;
        
        UE_LOG(LogTurnManager, Log, TEXT("Sending GameplayEvent %s to trigger attack on %s."), *EventData.EventTag.ToString(), *TargetActor->GetName());

        ASC->HandleGameplayEvent(EventData.EventTag, &EventData);

        // Since this consumes a turn, we can likely call a function to advance the turn state,
        // similar to how the move command works. For now, we trigger the event.
        // The ability itself should notify the turn manager of completion.
    }
    ```

## 5. Impact Scope

-   This change enables the player to use attack abilities.
-   It correctly integrates player attacks into the turn-based command processing flow.
-   The change is localized to the turn manager and does not affect other systems directly.

## 6. Testing Plan

-   **Test Case**: Have the player issue an attack command on an adjacent enemy.
-   **Expected Outcome**: The player's attack ability should activate, targeting the selected enemy. The game should correctly process the turn consumption.
-   **Test Case 2**: Have the player target an empty cell or an invalid target.
-   **Expected Outcome**: The attack should not activate, and the player should be able to issue another command (i.e., the turn is not consumed).
