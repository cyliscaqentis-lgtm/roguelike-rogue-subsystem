// Copyright Epic Games, Inc. All Rights Reserved.

// CodeRevision: INC-2025-00030-R2 (Migrate to UGridPathfindingSubsystem) (2025-11-17 00:40)
// CodeRevision: INC-2025-00030-R3 (Ensure Obs/Enemies array alignment) (2025-11-18 10:00)
// EnemyAISubsystem.cpp
#include "AI/Enemy/EnemyAISubsystem.h"
#include "AI/Enemy/EnemyThinkerBase.h"
#include "Grid/GridPathfindingSubsystem.h"
#include "Grid/GridOccupancySubsystem.h"
#include "Turn/TurnCorePhaseManager.h"
#include "Turn/DistanceFieldSubsystem.h"
#include "Utility/GridUtils.h"  // CodeRevision: INC-2025-00016-R1 (2025-11-16 14:00)
#include "Utility/RogueGameplayTags.h"
#include "Kismet/GameplayStatics.h"
#include "AbilitySystemInterface.h"
#include "GenericTeamAgentInterface.h"
#include "../../Utility/ProjectDiagnostics.h"

// CodeRevision: INC-2025-00030-R2 (Migrate to UGridPathfindingSubsystem) (2025-11-17 00:40)
void UEnemyAISubsystem::BuildObservations(
    const TArray<AActor*>& Enemies,
    const FIntPoint& PlayerGrid,
    UGridPathfindingSubsystem* PathFinder,
    TArray<FEnemyObservation>& OutObs)
{
    OutObs.Empty(Enemies.Num());

    UE_LOG(LogEnemyAI, Verbose,
        TEXT("[BuildObservations] ==== START ==== Enemies=%d, PlayerGrid=(%d,%d), PathFinder=%s"),
        Enemies.Num(),
        PlayerGrid.X, PlayerGrid.Y,
        PathFinder ? TEXT("Valid") : TEXT("NULL"));

    if (!PathFinder)
    {
        UE_LOG(LogEnemyAI, Error, TEXT("[BuildObservations] PathFinder is null!"));
        return;
    }

    UE_LOG(LogEnemyAI, Log, TEXT("[BuildObservations] PlayerGrid=(%d, %d)"), PlayerGrid.X, PlayerGrid.Y);

    if (UWorld* World = GetWorld())
    {
        if (UTurnCorePhaseManager* TurnCore = World->GetSubsystem<UTurnCorePhaseManager>())
        {
            TurnCore->CoreObservationPhase(PlayerGrid);
            UE_LOG(LogEnemyAI, Log, TEXT("[BuildObservations] DistanceField updated with PlayerGrid=(%d,%d)"), PlayerGrid.X, PlayerGrid.Y);
        }
        else
        {
            UE_LOG(LogEnemyAI, Error, TEXT("[BuildObservations] TurnCorePhaseManager not found!"));
        }
    }
    else
    {
        UE_LOG(LogEnemyAI, Error, TEXT("[BuildObservations] World not found!"));
    }

    int32 ValidEnemies = 0;
    int32 InvalidEnemies = 0;

    // CodeRevision: INC-2025-00030-R3
    // Enemies 配列と OutObs 配列のインデックス整合性を保証するため、
    // 無効な Enemy に対してもダミー Observation を挿入する。
    for (int32 i = 0; i < Enemies.Num(); ++i)
    {
        AActor* Enemy = Enemies[i];

        FEnemyObservation Obs;
        Obs.PlayerGridPosition = PlayerGrid;

        if (!IsValid(Enemy))
        {
            UE_LOG(LogEnemyAI, Log,
                TEXT("[BuildObservations] Enemy[%d] is invalid, inserting dummy observation"), i);

            Obs.GridPosition = FIntPoint::ZeroValue;
            Obs.DistanceInTiles = -1;
            ++InvalidEnemies;
        }
        else
        {
            // CodeRevision: INC-2025-1156-R1 (Fix Intent/Live desync: Use GridOccupancy as source of truth for enemy position) (2025-11-20 20:00)
            // CodeRevision: INC-2025-1156-R1 (Fix Intent/Live desync: Use GridOccupancy as source of truth for enemy position) (2025-11-20 20:00)
            if (UGridOccupancySubsystem* Occupancy = GetWorld()->GetSubsystem<UGridOccupancySubsystem>())
            {
                Obs.GridPosition = Occupancy->GetCellOfActor(Enemy);
            }

            // Fallback to physical location if not in occupancy (or occupancy missing)
            if (Obs.GridPosition == FIntPoint::ZeroValue)
            {
                Obs.GridPosition = PathFinder->WorldToGrid(Enemy->GetActorLocation());
            }
            // CodeRevision: INC-2025-00016-R1 (Use FGridUtils::ChebyshevDistance) (2025-11-16 14:00)
            Obs.DistanceInTiles = FGridUtils::ChebyshevDistance(Obs.GridPosition, PlayerGrid);

            ++ValidEnemies;

            if (i < 3)
            {
                UE_LOG(LogEnemyAI, Log,
                    TEXT("[BuildObservations] Enemy[%d]: %s, Grid=(%d, %d), DistToPlayer=%d"),
                    i, *Enemy->GetName(), Obs.GridPosition.X, Obs.GridPosition.Y, Obs.DistanceInTiles);
            }
        }

        OutObs.Add(Obs);
    }

    UE_LOG(LogEnemyAI, Log,
        TEXT("[BuildObservations] ==== RESULT ==== Generated %d observations (Valid=%d, Invalid=%d)"),
        OutObs.Num(), ValidEnemies, InvalidEnemies);

    if (UWorld* World = GetWorld())
    {
        if (UGridOccupancySubsystem* Occupancy = World->GetSubsystem<UGridOccupancySubsystem>())
        {
            /*
            int32 SampleRadius = 3;
            int32 BlockedCount = 0;
            int32 TotalCells = (SampleRadius * 2 + 1) * (SampleRadius * 2 + 1);
            
            for (int32 dx = -SampleRadius; dx <= SampleRadius; ++dx)
            {
                for (int32 dy = -SampleRadius; dy <= SampleRadius; ++dy)
                {
                    FIntPoint SampleCell = PlayerGrid + FIntPoint(dx, dy);
                    // CodeRevision: INC-2025-00021-R1 (Replace IsCellWalkable with IsCellWalkableIgnoringActor - Phase 2.3) (2025-11-17 15:05)
                    // Debug code: only terrain check needed
                    if (PathFinder && !PathFinder->IsCellWalkableIgnoringActor(SampleCell, nullptr))
                    {
                        BlockedCount++;
                    }
                }
            }
            
            UE_LOG(LogEnemyAI, Verbose,
                TEXT("[BuildObservations] GridOccupancy SAMPLE: %d/%d cells blocked around player (%d,%d)"),
                BlockedCount, TotalCells, PlayerGrid.X, PlayerGrid.Y);
            */

            for (int32 DebugIdx = 0; DebugIdx < FMath::Min(3, OutObs.Num()); ++DebugIdx)
            {
                const FEnemyObservation& Obs = OutObs[DebugIdx];
                // CodeRevision: INC-2025-00021-R1 (Replace IsCellWalkable with IsCellWalkableIgnoringActor - Phase 2.3) (2025-11-17 15:05)
                // Debug code: only terrain check needed
                bool bEnemyOccupied = PathFinder && !PathFinder->IsCellWalkableIgnoringActor(Obs.GridPosition, nullptr);
                UE_LOG(LogEnemyAI, Verbose,
                    TEXT("[BuildObservations] Enemy[%d] at (%d,%d): SelfOccupied=%d"),
                    DebugIdx, Obs.GridPosition.X, Obs.GridPosition.Y, bEnemyOccupied ? 1 : 0);
            }
        }
        else
        {
            UE_LOG(LogEnemyAI, Error, TEXT("[BuildObservations] GridOccupancySubsystem not found for diagnostics!"));
        }
    }
}

void UEnemyAISubsystem::CollectIntents(
    const TArray<FEnemyObservation>& Obs,
    const TArray<AActor*>& Enemies,
    TArray<FEnemyIntent>& OutIntents)
{
// CodeRevision: INC-2025-1130-R1 (Two-pass enemy intent generation to avoid attacker blocking) (2025-11-27 16:30)
    OutIntents.Empty(Obs.Num());

    UE_LOG(LogEnemyAI, Warning,
        TEXT("[CollectIntents] ==== START ==== Observations=%d, Enemies=%d"),
        Obs.Num(), Enemies.Num());

    if (Obs.Num() != Enemies.Num())
    {
        UE_LOG(LogEnemyAI, Error,
            TEXT("[CollectIntents] Size mismatch! Obs=%d Enemies=%d - Cannot generate intents"),
            Obs.Num(), Enemies.Num());
        return;
    }

    // INC-2025-0002: ログ強化 - Attack/Move/Waitの件数を個別にカウント
    int32 AttackIntents = 0;
    int32 MoveIntents = 0;
    int32 WaitIntents = 0;

    const FGameplayTag AttackTag = RogueGameplayTags::AI_Intent_Attack;
    const FGameplayTag MoveTag = RogueGameplayTags::AI_Intent_Move;
    const FGameplayTag WaitTag = RogueGameplayTags::AI_Intent_Wait;

    TSet<FIntPoint> HardBlockedCells;
    TSet<FIntPoint> ClaimedMoveTargets;
    TSet<TWeakObjectPtr<AActor>> ActorsWithIntent;

    // ---- Pass 1: Determine attackers and hard-block their cells ----
    UE_LOG(LogEnemyAI, Warning, TEXT("[CollectIntents] === PASS 1: Attack Intent Generation ==="));
    for (int32 i = 0; i < Obs.Num(); ++i)
    {
        AActor* Enemy = Enemies[i];

        if (!IsValid(Enemy))
        {
            UE_LOG(LogEnemyAI, Warning, TEXT("[CollectIntents] Enemy[%d] is invalid, skipping"), i);
            continue;
        }

        FEnemyIntent Intent = ComputeIntent(Enemy, Obs[i]);

        if (Intent.AbilityTag.MatchesTag(AttackTag))
        {
            Intent.Actor = Enemy;
            Intent.Owner = Enemy;
            Intent.CurrentCell = Obs[i].GridPosition;
            OutIntents.Add(Intent);
            ActorsWithIntent.Add(Enemy);
            HardBlockedCells.Add(Obs[i].GridPosition);
            ++AttackIntents;

            UE_LOG(LogEnemyAI, Warning,
                TEXT("[CollectIntents] Pass1[%d]: %s -> ATTACK at (%d,%d), HardBlocked"),
                i, *GetNameSafe(Enemy),
                Obs[i].GridPosition.X, Obs[i].GridPosition.Y);
        }
    }

    UE_LOG(LogEnemyAI, Warning,
        TEXT("[CollectIntents] Pass1 Complete: AttackIntents=%d, HardBlockedCells=%d"),
        AttackIntents, HardBlockedCells.Num());

    // ---- Pass 2: Handle movers / waits while respecting claimed cells ----
    UE_LOG(LogEnemyAI, Warning, TEXT("[CollectIntents] === PASS 2: Move/Wait Intent Generation ==="));
    TArray<int32> MoveCandidateIndices;
    MoveCandidateIndices.Reserve(Obs.Num());

    for (int32 i = 0; i < Obs.Num(); ++i)
    {
        AActor* Enemy = Enemies[i];
        if (IsValid(Enemy) && !ActorsWithIntent.Contains(Enemy))
        {
            MoveCandidateIndices.Add(i);
        }
    }

    // CodeRevision: INC-2025-1152-R1 (Process backline movers first so outer ring units claim approach tiles before frontliners) (2025-11-20 17:30)
    MoveCandidateIndices.Sort([&Obs](int32 A, int32 B)
    {
        const FEnemyObservation& ObsA = Obs[A];
        const FEnemyObservation& ObsB = Obs[B];

        // Prefer units that are currently farther from the player.
        if (ObsA.DistanceInTiles != ObsB.DistanceInTiles)
        {
            return ObsA.DistanceInTiles > ObsB.DistanceInTiles;
        }

        const int32 ManA =
            FMath::Abs(ObsA.GridPosition.X - ObsA.PlayerGridPosition.X) +
            FMath::Abs(ObsA.GridPosition.Y - ObsA.PlayerGridPosition.Y);

        const int32 ManB =
            FMath::Abs(ObsB.GridPosition.X - ObsB.PlayerGridPosition.X) +
            FMath::Abs(ObsB.GridPosition.Y - ObsB.PlayerGridPosition.Y);

        // For equal Chebyshev distance, prefer units that are aligned (lower Manhattan)
        // so they claim straight paths first, preventing diagonal units from cutting across.
        if (ManA != ManB)
        {
            return ManA < ManB;
        }

        return A < B;
    });

    for (const int32 i : MoveCandidateIndices)
    {
        AActor* Enemy = Enemies[i];
        const FEnemyObservation& Observation = Obs[i];

        FEnemyIntent Intent = ComputeMoveOrWaitIntent(
            Enemy,
            Observation,
            HardBlockedCells,
            ClaimedMoveTargets);

        Intent.Actor = Enemy;
        Intent.Owner = Enemy;
        Intent.CurrentCell = Observation.GridPosition;

        if (Intent.AbilityTag.MatchesTag(MoveTag))
        {
            ClaimedMoveTargets.Add(Intent.NextCell);
            ++MoveIntents;

            UE_LOG(LogEnemyAI, VeryVerbose,
                TEXT("[CollectIntents] Pass2[%d]: %s -> MOVE (%d,%d) -> (%d,%d)"),
                i, *GetNameSafe(Enemy),
                Observation.GridPosition.X, Observation.GridPosition.Y,
                Intent.NextCell.X, Intent.NextCell.Y);
        }
        else if (Intent.AbilityTag.MatchesTag(WaitTag))
        {
            ++WaitIntents;

            UE_LOG(LogEnemyAI, VeryVerbose,
                TEXT("[CollectIntents] Pass2[%d]: %s -> WAIT at (%d,%d)"),
                i, *GetNameSafe(Enemy),
                Observation.GridPosition.X, Observation.GridPosition.Y);
        }
        else
        {
            // Unknown tag
            UE_LOG(LogEnemyAI, Warning,
                TEXT("[CollectIntents] Pass2[%d]: %s -> UNKNOWN tag %s"),
                i, *GetNameSafe(Enemy), *Intent.AbilityTag.ToString());
        }

        OutIntents.Add(Intent);
        ActorsWithIntent.Add(Enemy);
    }

    // INC-2025-0002: 詳細なサマリログ
    UE_LOG(LogEnemyAI, Warning,
        TEXT("[CollectIntents] ==== RESULT ==== Generated %d intents (Attack=%d, Move=%d, Wait=%d)"),
        OutIntents.Num(), AttackIntents, MoveIntents, WaitIntents);
}

// CodeRevision: INC-2025-1130-R1 (Two-pass enemy intent generation to avoid attacker blocking) (2025-11-27 16:30)
FEnemyIntent UEnemyAISubsystem::ComputeMoveOrWaitIntent(
    AActor* EnemyActor,
    const FEnemyObservation& Obs,
    const TSet<FIntPoint>& HardBlockedCells,
    const TSet<FIntPoint>& ClaimedMoveTargets) const
{
    FEnemyIntent Intent;
    Intent.CurrentCell = Obs.GridPosition;
    Intent.NextCell = Obs.GridPosition;

    if (!IsValid(EnemyActor))
    {
        Intent.AbilityTag = RogueGameplayTags::AI_Intent_Wait;
        return Intent;
    }

    const UDistanceFieldSubsystem* DistanceField = GetWorld() ? GetWorld()->GetSubsystem<UDistanceFieldSubsystem>() : nullptr;
    FIntPoint PrimaryCell = Obs.GridPosition;

    if (DistanceField)
    {
        PrimaryCell = DistanceField->GetNextStepTowardsPlayer(Obs.GridPosition, EnemyActor);
    }

    const int32 CurrentDist = Obs.DistanceInTiles;
    const int32 PrimaryDist = FGridUtils::ChebyshevDistance(PrimaryCell, Obs.PlayerGridPosition);

    const bool bPrimaryWalkable = IsCellWalkable(EnemyActor, PrimaryCell);

    // Optimistic Pathing: 攻撃中の味方マス(HardBlockedCells)はここではブロックしない。
    // 実際に動けるかどうかは ConflictResolver に任せる。
    // これにより、後列ユニットが前列の攻撃ユニットの背後で待機する挙動（隊列維持）が実現される。
    // CodeRevision: INC-2025-1150-R1 (Treat attacker origin cells as hard-blocked when picking primary move targets) (2025-11-20 16:30)
    const bool bPrimaryBlocked =
        HardBlockedCells.Contains(PrimaryCell) ||
        ClaimedMoveTargets.Contains(PrimaryCell);

    // CodeRevision: INC-2025-1157-R1 (Re-introduce distance check: Allow maintain/reduce, reject increase) (2025-11-20 11:35)
    // We allow PrimaryDist == CurrentDist to enable corner navigation (sideways moves),
    // but we strictly reject PrimaryDist > CurrentDist to prevent moving away.
    const bool bPrimaryCloserOrEqual = (PrimaryDist <= CurrentDist);

    if (PrimaryCell != Obs.GridPosition &&
        bPrimaryWalkable &&
        bPrimaryCloserOrEqual &&
        !bPrimaryBlocked)
    {
        Intent.AbilityTag = RogueGameplayTags::AI_Intent_Move;
        Intent.NextCell = PrimaryCell;
        UE_LOG(LogEnemyAI, Verbose,
            TEXT("[ComputeMoveOrWaitIntent] %s: Using primary cell (%d,%d) -> Dist %d -> %d"),
            *GetNameSafe(EnemyActor),
            PrimaryCell.X, PrimaryCell.Y,
            CurrentDist, PrimaryDist);
        return Intent;
    }

    TArray<FIntPoint> Candidates;
    FindAlternateMoveCells(
        Obs.GridPosition,
        Obs.PlayerGridPosition,
        CurrentDist,
        EnemyActor,
        HardBlockedCells,
        ClaimedMoveTargets,
        Candidates);

    if (Candidates.Num() > 0)
    {
        const FIntPoint ChosenCell = SelectBestAlternateCell(
            Candidates,
            Obs.GridPosition,
            Obs.PlayerGridPosition);
        const int32 ChosenDist = FGridUtils::ChebyshevDistance(ChosenCell, Obs.PlayerGridPosition);

        // CodeRevision: INC-2025-1158-R1 (Prevent unnecessary shuffling behind friends) (2025-11-20 11:40)
        // If the primary path was blocked by a unit (dynamic obstacle) but was otherwise walkable (not a wall),
        // and our best alternate move is only "Equal Distance" (not strictly closer),
        // and we are not already at the optimal distance (e.g. range 1 for melee),
        // then we should WAIT instead of taking a sideways step.
        const bool bBlockedByUnit = bPrimaryBlocked && bPrimaryWalkable;
        const bool bOnlyEqualDist = (ChosenDist == CurrentDist);

        if (bBlockedByUnit && bOnlyEqualDist)
        {
            Intent.AbilityTag = RogueGameplayTags::AI_Intent_Wait;
            UE_LOG(LogEnemyAI, Verbose,
                TEXT("[ComputeMoveOrWaitIntent] %s: Blocked by unit at (%d,%d). Best alt (%d,%d) is equal dist (%d). WAITING."),
                *GetNameSafe(EnemyActor),
                PrimaryCell.X, PrimaryCell.Y,
                ChosenCell.X, ChosenCell.Y,
                ChosenDist);
            return Intent;
        }

        Intent.AbilityTag = RogueGameplayTags::AI_Intent_Move;
        Intent.NextCell = ChosenCell;
        UE_LOG(LogEnemyAI, Verbose,
            TEXT("[ComputeMoveOrWaitIntent] %s: Using alternate cell (%d,%d) -> Dist %d -> %d (Candidates=%d)"),
            *GetNameSafe(EnemyActor),
            ChosenCell.X, ChosenCell.Y,
            CurrentDist, ChosenDist,
            Candidates.Num());
        return Intent;
    }

    Intent.AbilityTag = RogueGameplayTags::AI_Intent_Wait;
    Intent.NextCell = Obs.GridPosition;
    return Intent;
}

void UEnemyAISubsystem::FindAlternateMoveCells(
    const FIntPoint& SelfCell,
    const FIntPoint& PlayerGrid,
    int32 CurrentDistanceInTiles,
    AActor* EnemyActor,
    const TSet<FIntPoint>& HardBlockedCells,
    const TSet<FIntPoint>& ClaimedMoveTargets,
    TArray<FIntPoint>& OutCandidates) const
{
    OutCandidates.Reset();

    static const FIntPoint Directions[8] = {
        { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
        { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 }
    };

	for (const FIntPoint& Dir : Directions)
	{
		const FIntPoint Candidate = SelfCell + Dir;

		if (!IsCellWalkable(EnemyActor, Candidate))
        {
            continue;
        }

        if (HardBlockedCells.Contains(Candidate) || ClaimedMoveTargets.Contains(Candidate))
        {
            continue;
        }

		const int32 NewDist = FGridUtils::ChebyshevDistance(Candidate, PlayerGrid);
		// CodeRevision: INC-2025-1155-R1 (Fix inconsistent distance comparison: Use Chebyshev vs Chebyshev, and allow equal distance for sideways moves) (2025-11-20 19:30)
		const int32 CurrentChebyshev = FGridUtils::ChebyshevDistance(SelfCell, PlayerGrid);
		if (NewDist > CurrentChebyshev)
		{
			continue;
		}

		// CodeRevision: INC-2025-1154-R1 (Apply corner-cutting prevention to alternate diagonal moves so they respect LOS shoulders) (2025-11-20 18:30)
		if (Dir.X != 0 && Dir.Y != 0)
		{
			if (UWorld* World = GetWorld())
			{
				if (const UGridPathfindingSubsystem* Pathfinding = World->GetSubsystem<UGridPathfindingSubsystem>())
				{
					const FIntPoint Shoulder1 = SelfCell + FIntPoint(Dir.X, 0);
					const FIntPoint Shoulder2 = SelfCell + FIntPoint(0, Dir.Y);

					const bool bShoulder1Walkable = Pathfinding->IsCellWalkableIgnoringActor(Shoulder1, nullptr);
					const bool bShoulder2Walkable = Pathfinding->IsCellWalkableIgnoringActor(Shoulder2, nullptr);

					if (!bShoulder1Walkable || !bShoulder2Walkable)
					{
						UE_LOG(LogEnemyAI, Verbose,
							TEXT("[FindAlternateMoveCells] Reject diagonal (%d,%d)->(%d,%d) due to corner (Shoulder1=%d Shoulder2=%d)"),
							SelfCell.X, SelfCell.Y,
							Candidate.X, Candidate.Y,
							bShoulder1Walkable ? 1 : 0,
							bShoulder2Walkable ? 1 : 0);
						continue;
					}
				}
			}
		}

        OutCandidates.Add(Candidate);
    }

    UE_LOG(LogEnemyAI, Verbose,
        TEXT("[FindAlternateMoveCells] Self=(%d,%d) Dist=%d, CandidateCount=%d"),
        SelfCell.X, SelfCell.Y, CurrentDistanceInTiles, OutCandidates.Num());
}

FIntPoint UEnemyAISubsystem::SelectBestAlternateCell(
    const TArray<FIntPoint>& Candidates,
    const FIntPoint& SelfCell,
    const FIntPoint& PlayerGrid) const
{
    if (Candidates.Num() == 0)
    {
        return FIntPoint::ZeroValue;
    }

    FIntPoint BestCell = Candidates[0];
    float BestScore = ScoreMoveCandidate(SelfCell, BestCell, PlayerGrid);

    for (int32 Idx = 1; Idx < Candidates.Num(); ++Idx)
    {
        const FIntPoint& Candidate = Candidates[Idx];
        const float Score = ScoreMoveCandidate(SelfCell, Candidate, PlayerGrid);

        if (Score > BestScore)
        {
            BestScore = Score;
            BestCell = Candidate;
        }
    }

    UE_LOG(LogEnemyAI, Verbose,
        TEXT("[SelectBestAlternateCell] Self=(%d,%d) Player=(%d,%d) Best=(%d,%d) Score=%.2f"),
        SelfCell.X, SelfCell.Y,
        PlayerGrid.X, PlayerGrid.Y,
        BestCell.X, BestCell.Y,
        BestScore);

    return BestCell;
}

float UEnemyAISubsystem::ScoreMoveCandidate(
	const FIntPoint& SelfCell,
	const FIntPoint& Candidate,
	const FIntPoint& PlayerGrid) const
{
    const int32 CurDist = FGridUtils::ChebyshevDistance(SelfCell, PlayerGrid);
    const int32 NewDist = FGridUtils::ChebyshevDistance(Candidate, PlayerGrid);
    const int32 dRow = FMath::Abs(Candidate.Y - PlayerGrid.Y);
    const int32 dCol = FMath::Abs(Candidate.X - PlayerGrid.X);

	const float Wdist = 10.0f;
	const float Wrow = 2.0f;
	const float Wcol = 1.0f;
	const float Wlane = 1.5f;
	const float Wortho = 0.75f; // CodeRevision: INC-2025-1152-R1 (Prefer orthogonal steps over diagonals when reducing distance)

	float Score = 0.0f;
	Score += Wdist * float(CurDist - NewDist);
	Score -= Wrow * float(dRow);
	Score -= Wcol * float(dCol);

	const FVector2D ToPlayer(float(PlayerGrid.X - SelfCell.X), float(PlayerGrid.Y - SelfCell.Y));
	const FVector2D Step(float(Candidate.X - SelfCell.X), float(Candidate.Y - SelfCell.Y));

	float DotContribution = 0.0f;
	if (!ToPlayer.IsNearlyZero() && !Step.IsNearlyZero())
    {
		const float Dot = FVector2D::DotProduct(ToPlayer.GetSafeNormal(), Step.GetSafeNormal());
		DotContribution = Wlane * Dot;
		Score += DotContribution;
	}

	// Prefer cardinal (N/E/S/W) steps when multiple candidates offer similar distance gains,
	// to better match typical roguelike enemy movement where orthogonal approaches feel "smarter".
	const int32 StepDx = Candidate.X - SelfCell.X;
	const int32 StepDy = Candidate.Y - SelfCell.Y;
	const bool bIsOrthogonal = (StepDx == 0 || StepDy == 0);
	if (bIsOrthogonal)
	{
		Score += Wortho;
	}

    UE_LOG(LogEnemyAI, VeryVerbose,
        TEXT("[ScoreMoveCandidate] Self=(%d,%d) Candidate=(%d,%d) ΔDist=%d Wdist*Δ=%.2f Wrow*%d=%.2f Wcol*%d=%.2f Wlane*dot=%.2f Total=%.2f"),
        SelfCell.X, SelfCell.Y,
        Candidate.X, Candidate.Y,
        CurDist - NewDist,
        Wdist * float(CurDist - NewDist),
        dRow,
        Wrow * float(dRow),
        dCol,
        Wcol * float(dCol),
        DotContribution,
        Score);

    return Score;
}

bool UEnemyAISubsystem::IsCellWalkable(AActor* EnemyActor, const FIntPoint& Cell) const
{
    if (!EnemyActor)
    {
        return false;
    }

    if (UWorld* World = GetWorld())
    {
        if (UGridPathfindingSubsystem* PathFinder = World->GetSubsystem<UGridPathfindingSubsystem>())
        {
            return PathFinder->IsCellWalkableIgnoringActor(Cell, EnemyActor);
        }
    }

    return false;
}

FEnemyIntent UEnemyAISubsystem::ComputeIntent(
    AActor* Enemy,
    const FEnemyObservation& Observation)
{
    FEnemyIntent Intent;
    Intent.Actor = Enemy;

    if (!IsValid(Enemy))
    {
        UE_LOG(LogEnemyAI, Warning, TEXT("[ComputeIntent] Enemy is invalid, returning Wait"));
        Intent.AbilityTag = FGameplayTag::RequestGameplayTag(TEXT("AI.Intent.Wait"));
        return Intent;
    }

    UEnemyThinkerBase* Thinker = Enemy->FindComponentByClass<UEnemyThinkerBase>();

    if (Thinker)
    {
        Intent = Thinker->ComputeIntent(Observation);
        Intent.Actor = Enemy;

        UE_LOG(LogEnemyAI, Log,
            TEXT("[ComputeIntent] Enemy=%s, Thinker found, Intent=%s"),
            *Enemy->GetName(), *Intent.AbilityTag.ToString());
    }
    else
    {
        Intent.AbilityTag = FGameplayTag::RequestGameplayTag(TEXT("AI.Intent.Wait"));

        UE_LOG(LogEnemyAI, Warning,
            TEXT("[ComputeIntent] Enemy=%s has NO Thinker component, defaulting to Wait"),
            *Enemy->GetName());
    }

    return Intent;
}

void UEnemyAISubsystem::CollectAllEnemies(
    AActor* PlayerPawn,
    TArray<AActor*>& OutEnemies)
{
    OutEnemies.Empty();

    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogEnemyAI, Error, TEXT("[CollectAllEnemies] World is null"));
        return;
    }

    UE_LOG(LogEnemyAI, Log, TEXT("[CollectAllEnemies] ==== START ===="));

    TArray<AActor*> Found;
    UGameplayStatics::GetAllActorsOfClass(World, APawn::StaticClass(), Found);

    UE_LOG(LogEnemyAI, Log, TEXT("[CollectAllEnemies] GetAllActorsOfClass found %d Pawns"), Found.Num());

    static const FName ActorTagEnemy(TEXT("Enemy"));
    static const FGameplayTag GT_Enemy = FGameplayTag::RequestGameplayTag(TEXT("Faction.Enemy"));

    int32 NumByTag = 0, NumByTeam = 0, NumByActorTag = 0;

    OutEnemies.Reserve(Found.Num());

    for (AActor* A : Found)
    {
        if (!IsValid(A))
        {
            continue;
        }

        if (A == PlayerPawn)
        {
            UE_LOG(LogEnemyAI, Verbose, TEXT("[CollectAllEnemies] Skipping PlayerPawn: %s"), *A->GetName());
            continue;
        }

        int32 TeamId = 255;
        if (const IGenericTeamAgentInterface* TeamAgent = Cast<IGenericTeamAgentInterface>(A))
        {
            TeamId = TeamAgent->GetGenericTeamId().GetId();
        }
        else if (const IGenericTeamAgentInterface* Controller = Cast<IGenericTeamAgentInterface>(Cast<APawn>(A)->GetController()))
        {
            TeamId = Controller->GetGenericTeamId().GetId();
        }

        const bool bByTeam = (TeamId == 2 || TeamId == 255);

        UAbilitySystemComponent* ASC = nullptr;
        if (const IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(A))
        {
            ASC = ASI->GetAbilitySystemComponent();
        }
        const bool bByGTag = (ASC && ASC->HasMatchingGameplayTag(GT_Enemy));

        const bool bByActorTag = A->Tags.Contains(ActorTagEnemy);

        if (bByGTag || bByTeam || bByActorTag)
        {
            OutEnemies.Add(A);

            if (bByGTag) ++NumByTag;
            if (bByTeam) ++NumByTeam;
            if (bByActorTag) ++NumByActorTag;

            const int32 Index = OutEnemies.Num() - 1;
            if (Index < 3)
            {
                UE_LOG(LogEnemyAI, Log,
                    TEXT("[CollectAllEnemies] Added[%d]: %s (TeamId=%d, GTag=%d, ByTeam=%d, ActorTag=%d)"),
                    Index, *A->GetName(), TeamId, bByGTag, bByTeam, bByActorTag);
            }
        }
    }

    UE_LOG(LogEnemyAI, Log,
        TEXT("[CollectAllEnemies] ==== RESULT ==== found=%d  collected=%d  byGTag=%d  byTeam=%d  byActorTag=%d"),
        Found.Num(), OutEnemies.Num(), NumByTag, NumByTeam, NumByActorTag);

    if (OutEnemies.Num() == 0 && Found.Num() > 1)
    {
        UE_LOG(LogEnemyAI, Warning,
            TEXT("[CollectAllEnemies] No enemies collected from %d Pawns! Check TeamID/Tags."),
            Found.Num());
    }
}
