// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/Enemy/EnemyAISubsystem.h"
#include "AI/Enemy/EnemyThinkerBase.h"
#include "Grid/GridPathfindingLibrary.h"
#include "Grid/GridOccupancySubsystem.h"
#include "Turn/TurnCorePhaseManager.h"
#include "Kismet/GameplayStatics.h"  // ★★★ PathFinder検索用 ★★★
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

    // ★★★ 重要：DistanceFieldを更新 ★★★
    // これがないとGetNextStepTowardsPlayerが常に現在セルを返してしまう
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

        // ★★★ 重要：Enemyの有効性チェック ★★★
        if (!IsValid(Enemy))
        {
            UE_LOG(LogEnemyAI, Log, TEXT("[BuildObservations] Enemy[%d] is invalid, skipping"), i);
            ++InvalidEnemies;
            continue;
        }

        FEnemyObservation Obs;
        Obs.GridPosition = PathFinder->WorldToGrid(Enemy->GetActorLocation());
        Obs.PlayerGridPosition = PlayerGrid;
        Obs.DistanceInTiles = FMath::Abs(Obs.GridPosition.X - PlayerGrid.X) +
            FMath::Abs(Obs.GridPosition.Y - PlayerGrid.Y);

        OutObs.Add(Obs);
        ++ValidEnemies;

        // ★★★ デバッグ：最初の3体のみログ出力 ★★★
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

    // ★ 診断：BuildObservations後のGridOccupancy状態をサンプリング
    // ★★★ 引数で渡されたPathFinderを使用（自前取得を削除） ★★★
    if (UWorld* World = GetWorld())
    {
        if (UGridOccupancySubsystem* Occupancy = World->GetSubsystem<UGridOccupancySubsystem>())
        {
            // プレイヤー周辺のグリッド状態をサンプリング
            int32 SampleRadius = 3;
            int32 BlockedCount = 0;
            int32 TotalCells = (SampleRadius * 2 + 1) * (SampleRadius * 2 + 1);
            
            for (int32 dx = -SampleRadius; dx <= SampleRadius; ++dx)
            {
                for (int32 dy = -SampleRadius; dy <= SampleRadius; ++dy)
                {
                    FIntPoint SampleCell = PlayerGrid + FIntPoint(dx, dy);
                    // ★★★ 引数のPathFinderの統合APIを使用 ★★★
                    if (PathFinder && !PathFinder->IsCellWalkable(SampleCell))
                    {
                        BlockedCount++;
                    }
                }
            }
            
            UE_LOG(LogEnemyAI, Verbose,
                TEXT("[BuildObservations] GridOccupancy SAMPLE: %d/%d cells blocked around player (%d,%d)"),
                BlockedCount, TotalCells, PlayerGrid.X, PlayerGrid.Y);

            // 敵の位置が占有されているか確認
            for (int32 i = 0; i < FMath::Min(3, OutObs.Num()); ++i)
            {
                const FEnemyObservation& Obs = OutObs[i];
                // ★★★ 引数のPathFinderの統合APIを使用 ★★★
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

    // ★★★ 重要：サイズ一致チェック ★★★
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

        // ★★★ Enemyの有効性チェック ★★★
        if (!IsValid(Enemy))
        {
            UE_LOG(LogEnemyAI, Warning, TEXT("[CollectIntents] Enemy[%d] is invalid, skipping"), i);
            continue;
        }

        FEnemyIntent Intent = ComputeIntent(Enemy, Obs[i]);
        OutIntents.Add(Intent);

        // ★★★ Intent統計 ★★★
        if (Intent.AbilityTag.MatchesTag(FGameplayTag::RequestGameplayTag(TEXT("AI.Intent.Wait"))))
        {
            ++WaitIntents;
        }
        else
        {
            ++ValidIntents;
        }

        // ★★★ デバッグ：最初の3体のみログ出力 ★★★
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

    // ★★★ EnemyThinkerBase に委譲 ★★★
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
        // ★★★ デフォルト：待機 ★★★
        Intent.AbilityTag = FGameplayTag::RequestGameplayTag(TEXT("AI.Intent.Wait"));

        UE_LOG(LogEnemyAI, Warning,
            TEXT("[ComputeIntent] Enemy=%s has NO Thinker component, defaulting to Wait"),
            *Enemy->GetName());
    }

    return Intent;
}

//------------------------------------------------------------------------------
// ★★★ Phase 4: Enemy収集（2025-11-09） ★★★
//------------------------------------------------------------------------------

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

    // ★★★ APawnで検索（includeが不要） ★★★
    TArray<AActor*> Found;
    UGameplayStatics::GetAllActorsOfClass(World, APawn::StaticClass(), Found);

    UE_LOG(LogEnemyAI, Log, TEXT("[CollectAllEnemies] GetAllActorsOfClass found %d Pawns"), Found.Num());

    static const FName ActorTagEnemy(TEXT("Enemy"));
    static const FGameplayTag GT_Enemy = FGameplayTag::RequestGameplayTag(TEXT("Faction.Enemy"));

    int32 NumByTag = 0, NumByTeam = 0, NumByActorTag = 0;

    OutEnemies.Reserve(Found.Num());

    for (AActor* A : Found)
    {
        // ★★★ Nullチェック ★★★
        if (!IsValid(A))
        {
            continue;
        }

        // ★★★ プレイヤーを除外 ★★★
        if (A == PlayerPawn)
        {
            UE_LOG(LogEnemyAI, Verbose, TEXT("[CollectAllEnemies] Skipping PlayerPawn: %s"), *A->GetName());
            continue;
        }

        // ★★★ TeamID判定 ★★★
        int32 TeamId = 255; // Default: NoTeam
        if (const IGenericTeamAgentInterface* TeamAgent = Cast<IGenericTeamAgentInterface>(A))
        {
            TeamId = TeamAgent->GetGenericTeamId().GetId();
        }
        else if (const IGenericTeamAgentInterface* Controller = Cast<IGenericTeamAgentInterface>(Cast<APawn>(A)->GetController()))
        {
            TeamId = Controller->GetGenericTeamId().GetId();
        }

        const bool bByTeam = (TeamId == 2 || TeamId == 255); // 敵チーム or NoTeam

        // ★★★ GameplayTag判定 ★★★
        UAbilitySystemComponent* ASC = nullptr;
        if (const IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(A))
        {
            ASC = ASI->GetAbilitySystemComponent();
        }
        const bool bByGTag = (ASC && ASC->HasMatchingGameplayTag(GT_Enemy));

        // ★★★ ActorTag判定 ★★★
        const bool bByActorTag = A->Tags.Contains(ActorTagEnemy);

        // ★★★ いずれかの条件を満たせば敵として認識 ★★★
        if (bByGTag || bByTeam || bByActorTag)
        {
            OutEnemies.Add(A);

            if (bByGTag) ++NumByTag;
            if (bByTeam) ++NumByTeam;
            if (bByActorTag) ++NumByActorTag;

            // ★★★ デバッグ：最初の3体のみログ ★★★
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

    // ★★★ エラー検出：敵が1体も見つからない場合 ★★★
    if (OutEnemies.Num() == 0 && Found.Num() > 1)
    {
        UE_LOG(LogEnemyAI, Warning,
            TEXT("[CollectAllEnemies] No enemies collected from %d Pawns! Check TeamID/Tags."),
            Found.Num());
    }
}
