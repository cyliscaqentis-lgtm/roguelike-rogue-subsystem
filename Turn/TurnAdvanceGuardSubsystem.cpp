#include "Turn/TurnAdvanceGuardSubsystem.h"

#include "AbilitySystemComponent.h"
#include "TimerManager.h"
#include "../Utility/ProjectDiagnostics.h"
#include "Turn/GameTurnManagerBase.h"
#include "Turn/TurnActionBarrierSubsystem.h"
#include "Utility/RogueGameplayTags.h"

DEFINE_LOG_CATEGORY(LogTurnAdvanceGuard);

bool UTurnAdvanceGuardSubsystem::CanAdvanceTurn(const AGameTurnManagerBase* TurnManager, int32 TurnId, bool* OutBarrierQuiet, int32* OutInProgressCount) const
{
    if (!TurnManager)
    {
        return false;
    }

    bool bBarrierQuiet = false;
    int32 PendingCount = 0;
    if (!EvaluateBarrierQuiet(TurnId, bBarrierQuiet, PendingCount))
    {
        if (OutBarrierQuiet)
        {
            *OutBarrierQuiet = false;
        }
        if (OutInProgressCount)
        {
            *OutInProgressCount = 0;
        }
        return false;
    }

    if (!bBarrierQuiet)
    {
        UE_LOG(LogTurnAdvanceGuard, Warning,
            TEXT("[CanAdvanceTurn] Barrier NOT quiescent: Turn=%d PendingActions=%d"),
            TurnId, PendingCount);
    }

    bool bAscValid = false;
    const int32 InProgressCount = CollectInProgressCount(TurnManager, bAscValid);
    if (OutInProgressCount)
    {
        *OutInProgressCount = InProgressCount;
    }

    if (!bAscValid)
    {
        return false;
    }

    const bool bNoInProgressTags = (InProgressCount == 0);
    if (!bNoInProgressTags)
    {
        UE_LOG(LogTurnAdvanceGuard, Warning,
            TEXT("[CanAdvanceTurn] InProgress tags still active: Count=%d"),
            InProgressCount);
    }

    if (OutBarrierQuiet)
    {
        *OutBarrierQuiet = bBarrierQuiet;
    }

    const bool bCanAdvance = bBarrierQuiet && bNoInProgressTags;
    if (bCanAdvance)
    {
        UE_LOG(LogTurnAdvanceGuard, Log,
            TEXT("[CanAdvanceTurn] OK: Turn=%d (Barrier=Quiet, InProgress=0)"),
            TurnId);
    }
    else
    {
        UE_LOG(LogTurnAdvanceGuard, Warning,
            TEXT("[CanAdvanceTurn] ❌BLOCKED: Turn=%d (Barrier=%s, InProgress=%d)"),
            TurnId,
            bBarrierQuiet ? TEXT("Quiet") : TEXT("Busy"),
            InProgressCount);
    }

    return bCanAdvance;
}

void UTurnAdvanceGuardSubsystem::HandleEndEnemyTurn(AGameTurnManagerBase* TurnManager)
{
    if (!TurnManager)
    {
        UE_LOG(LogTurnAdvanceGuard, Error, TEXT("[EndEnemyTurn] TurnManager is null"));
        return;
    }

    const int32 TurnId = TurnManager->GetCurrentTurnId();
    bool bBarrierQuiet = false;
    int32 InProgressCount = 0;

    if (!CanAdvanceTurn(TurnManager, TurnId, &bBarrierQuiet, &InProgressCount))
    {
        DumpBarrierState(TurnId);
        TurnManager->ClearResidualInProgressTags();

        if (bBarrierQuiet && InProgressCount > 0)
        {
            if (CanAdvanceTurn(TurnManager, TurnId))
            {
                UE_LOG(LogTurnAdvanceGuard, Warning,
                    TEXT("[EndEnemyTurn] Residual InProgress tags cleared, advancing turn without retry"));
                TurnManager->AdvanceTurnAndRestart();
                return;
            }
        }

        ScheduleRetry(TurnManager);
        return;
    }

    TurnManager->ClearResidualInProgressTags();
    TurnManager->AdvanceTurnAndRestart();
}

bool UTurnAdvanceGuardSubsystem::EvaluateBarrierQuiet(int32 TurnId, bool& OutBarrierQuiet, int32& OutPendingActions) const
{
    OutPendingActions = 0;
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTurnAdvanceGuard, Error, TEXT("[CanAdvanceTurn] World not available"));
        OutBarrierQuiet = false;
        return false;
    }

    if (UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>())
    {
        OutBarrierQuiet = Barrier->IsQuiescent(TurnId);
        if (!OutBarrierQuiet)
        {
            OutPendingActions = Barrier->GetPendingActionCount(TurnId);
        }
        return true;
    }

    UE_LOG(LogTurnAdvanceGuard, Error,
        TEXT("[CanAdvanceTurn] Barrier subsystem not found"));
    OutBarrierQuiet = false;
    return false;
}

int32 UTurnAdvanceGuardSubsystem::CollectInProgressCount(const AGameTurnManagerBase* TurnManager, bool& bOutAscValid) const
{
    bOutAscValid = false;
    if (!TurnManager)
    {
        return 0;
    }

    if (UAbilitySystemComponent* ASC = TurnManager->GetPlayerASC())
    {
        bOutAscValid = true;
        return ASC->GetTagCount(RogueGameplayTags::State_Action_InProgress);
    }

    UE_LOG(LogTurnAdvanceGuard, Error,
        TEXT("[CanAdvanceTurn] Player ASC not found"));
    return 0;
}

void UTurnAdvanceGuardSubsystem::DumpBarrierState(int32 TurnId) const
{
    if (UWorld* World = GetWorld())
    {
        if (UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>())
        {
            Barrier->DumpTurnState(TurnId);
        }
    }
}

void UTurnAdvanceGuardSubsystem::ScheduleRetry(AGameTurnManagerBase* TurnManager)
{
    if (!TurnManager)
    {
        return;
    }

    if (!TurnManager->bEndTurnPosted)
    {
        TurnManager->bEndTurnPosted = true;

        if (UWorld* World = GetWorld())
        {
            TWeakObjectPtr<AGameTurnManagerBase> WeakManager = TurnManager;
            FTimerDelegate RetryDelegate = FTimerDelegate::CreateLambda([WeakManager]()
            {
                if (AGameTurnManagerBase* StrongManager = WeakManager.Get())
                {
                    StrongManager->EndEnemyTurn();
                }
            });

            World->GetTimerManager().SetTimer(
                TurnManager->EndEnemyTurnDelayHandle,
                RetryDelegate,
                0.5f,
                false);

            UE_LOG(LogTurnAdvanceGuard, Warning,
                TEXT("[EndEnemyTurn] Retry scheduled in 0.5s"));
        }
    }
    else
    {
        UE_LOG(LogTurnAdvanceGuard, Log,
            TEXT("[EndEnemyTurn] Retry suppressed (already posted)"));
    }
}
