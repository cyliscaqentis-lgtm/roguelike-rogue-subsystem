#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TurnAdvanceGuardSubsystem.generated.h"

class AGameTurnManagerBase;

/**
 * Handles the guard conditions for ending the enemy turn and advancing to the next turn.
 * Encapsulates barrier checks, residual tag validation, and retry scheduling.
 *
 * CodeRevision: INC-2025-1208-R4 (Extract EndEnemyTurn/CanAdvanceTurn guards into subsystem) (2025-11-22 02:10)
 */
UCLASS()
class LYRAGAME_API UTurnAdvanceGuardSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    /** Evaluate whether the turn can safely advance (barrier quiet + no InProgress tags). */
    bool CanAdvanceTurn(const AGameTurnManagerBase* TurnManager, int32 TurnId, bool* OutBarrierQuiet = nullptr, int32* OutInProgressCount = nullptr) const;

    /** Execute the end-of-enemy-turn guard logic (dump barrier, cleanup tags, schedule retry, advance turn). */
    void HandleEndEnemyTurn(AGameTurnManagerBase* TurnManager);

private:
    bool EvaluateBarrierQuiet(int32 TurnId, bool& OutBarrierQuiet, int32& OutPendingActions) const;
    int32 CollectInProgressCount(const AGameTurnManagerBase* TurnManager, bool& bOutAscValid) const;
    void DumpBarrierState(int32 TurnId) const;
    void ScheduleRetry(AGameTurnManagerBase* TurnManager);
};
