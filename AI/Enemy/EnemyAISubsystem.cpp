// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/Enemy/EnemyAISubsystem.h"
#include "AI/Enemy/EnemyThinkerBase.h"
#include "Grid/GridPathfindingLibrary.h"
#include "Grid/GridOccupancySubsystem.h"
#include "Turn/TurnCorePhaseManager.h"
#include "Kismet/GameplayStatics.h"
#include "AbilitySystemInterface.h"
#include "GenericTeamAgentInterface.h"
#include "../../ProjectDiagnostics.h"

void UEnemyAISubsystem::BuildObservations(
    const TArray<AActor*>& Enemies,
    AActor* Player,
    AGridPathfindingLibrary* PathFinder,
    TArray<FEnemyObservation>& OutObs)
{
    OutObs.Empty(Enemies.Num());

    UE_LOG(LogEnemyAI, Verbose,
        TEXT("[BuildObservations] ==== START ==== Enemies=%d, Player=%s, PathFinder=%s"),
        Enemies.Num(),
        Player ? *Player->GetName() : TEXT("NULL"),
        PathFinder ? TEXT("Valid") : TEXT("NULL"));

    if (!Player || !PathFinder)
    {
        UE_LOG(LogEnemyAI, Error, TEXT("[BuildObservations] Player or PathFinder is null!"));
        return;
    }

    const FIntPoint PlayerGrid = PathFinder->WorldToGrid(Player->GetActorLocation());
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

    for (int32 i = 0; i < Enemies.Num(); ++i)
    {
        AActor* Enemy = Enemies[i];

        if (!IsValid(Enemy))
        {
            UE_LOG(LogEnemyAI, Log, TEXT("[BuildObservations] Enemy[%d] is invalid, skipping"), i);
            ++InvalidEnemies;
            continue;
        }

        FEnemyObservation Obs;
        Obs.GridPosition = PathFinder->WorldToGrid(Enemy->GetActorLocation());
        Obs.PlayerGridPosition = PlayerGrid;
        const int32 DeltaX = FMath::Abs(Obs.GridPosition.X - PlayerGrid.X);
        const int32 DeltaY = FMath::Abs(Obs.GridPosition.Y - PlayerGrid.Y);
        Obs.DistanceInTiles = FMath::Max(DeltaX, DeltaY);

        OutObs.Add(Obs);
        ++ValidEnemies;

        if (i < 3)
        {
            UE_LOG(LogEnemyAI, Log,
                TEXT("[BuildObservations] Enemy[%d]: %s, Grid=(%d, %d), DistToPlayer=%d"),
                i, *Enemy->GetName(), Obs.GridPosition.X, Obs.GridPosition.Y, Obs.DistanceInTiles);
        }
    }

    UE_LOG(LogEnemyAI, Log,
        TEXT("[BuildObservations] ==== RESULT ==== Generated %d observations (Valid=%d, Invalid=%d)"),
        OutObs.Num(), ValidEnemies, InvalidEnemies);

    if (UWorld* World = GetWorld())
    {
        if (UGridOccupancySubsystem* Occupancy = World->GetSubsystem<UGridOccupancySubsystem>())
        {
            int32 SampleRadius = 3;
            int32 BlockedCount = 0;
            int32 TotalCells = (SampleRadius * 2 + 1) * (SampleRadius * 2 + 1);
            
            for (int32 dx = -SampleRadius; dx <= SampleRadius; ++dx)
            {
                for (int32 dy = -SampleRadius; dy <= SampleRadius; ++dy)
                {
                    FIntPoint SampleCell = PlayerGrid + FIntPoint(dx, dy);
                    if (PathFinder && !PathFinder->IsCellWalkable(SampleCell))
                    {
                        BlockedCount++;
                    }
                }
            }
            
            UE_LOG(LogEnemyAI, Verbose,
                TEXT("[BuildObservations] GridOccupancy SAMPLE: %d/%d cells blocked around player (%d,%d)"),
                BlockedCount, TotalCells, PlayerGrid.X, PlayerGrid.Y);

            for (int32 i = 0; i < FMath::Min(3, OutObs.Num()); ++i)
            {
                const FEnemyObservation& Obs = OutObs[i];
                bool bEnemyOccupied = PathFinder && !PathFinder->IsCellWalkable(Obs.GridPosition);
                UE_LOG(LogEnemyAI, Verbose,
                    TEXT("[BuildObservations] Enemy[%d] at (%d,%d): SelfOccupied=%d"),
                    i, Obs.GridPosition.X, Obs.GridPosition.Y, bEnemyOccupied ? 1 : 0);
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

    int32 ValidIntents = 0;
    int32 WaitIntents = 0;

    for (int32 i = 0; i < Obs.Num(); ++i)
    {
        AActor* Enemy = Enemies[i];

        if (!IsValid(Enemy))
        {
            UE_LOG(LogEnemyAI, Warning, TEXT("[CollectIntents] Enemy[%d] is invalid, skipping"), i);
            continue;
        }

        FEnemyIntent Intent = ComputeIntent(Enemy, Obs[i]);
        OutIntents.Add(Intent);

        if (Intent.AbilityTag.MatchesTag(FGameplayTag::RequestGameplayTag(TEXT("AI.Intent.Wait"))))
        {
            ++WaitIntents;
        }
        else
        {
            ++ValidIntents;
        }

        if (i < 3)
        {
            UE_LOG(LogEnemyAI, Log,
                TEXT("[CollectIntents] Enemy[%d]: %s, Intent=%s"),
                i, *Enemy->GetName(), *Intent.AbilityTag.ToString());
        }
    }

    UE_LOG(LogEnemyAI, Warning,
        TEXT("[CollectIntents] ==== RESULT ==== Generated %d intents (Valid=%d, Wait=%d)"),
        OutIntents.Num(), ValidIntents, WaitIntents);
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
