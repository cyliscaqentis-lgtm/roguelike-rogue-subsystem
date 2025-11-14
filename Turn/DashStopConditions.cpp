 
// Copyright Epic Games, Inc. All Rights Reserved.

#include "DashStopConditions.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"

int32 UDashStopEvaluator::CalculateAllowedDashSteps(
    AActor* Actor,
    const FIntPoint& StartCell,
    const FIntPoint& TargetCell,
    int32 ProposedK,
    const FDashStopConfig& Config)
{
    if (!Actor || ProposedK <= 0)
    {
        return 0;
    }

    UWorld* World = Actor->GetWorld();
    if (!World)
    {
        return 0;
    }

    // �����o�H���擾
    TArray<FIntPoint> Path = GetLinePath(StartCell, TargetCell, ProposedK);

    // �e�^�C��Œ�~�������`�F�b�N
    for (int32 t = 0; t < Path.Num(); ++t)
    {
        const FIntPoint& CurrentCell = Path[t];

        // �G�אڃ`�F�b�N
        if (Config.bStopOnEnemyAdjacent && HasAdjacentEnemy(CurrentCell, World))
        {
            UE_LOG(LogTemp, Log, TEXT("[DashStop] Stopped at step %d (EnemyAdjacent)"), t + 1);
            return t + 1;
        }

        // �댯�^�C��`�F�b�N
        if (Config.bStopOnDangerTile && IsDangerTile(CurrentCell, World))
        {
            UE_LOG(LogTemp, Log, TEXT("[DashStop] Stopped at step %d (DangerTile)"), t);
            return FMath::Max(0, t);
        }

        // ��Q���`�F�b�N
        if (Config.bStopOnObstacle && IsObstacle(CurrentCell, World))
        {
            UE_LOG(LogTemp, Log, TEXT("[DashStop] Stopped at step %d (Obstacle)"), t);
            return FMath::Max(0, t);
        }
    }

    // ��~�����ɊY�����Ȃ��ꍇ�͒�Ēʂ�
    return ProposedK;
}

bool UDashStopEvaluator::HasAdjacentEnemy(const FIntPoint& Cell, UWorld* World)
{
    if (!World)
    {
        return false;
    }

    // TODO: Phase 3�㔼�Ŏ���
    // 8�����̗אڃ}�X�ɓG�����邩�`�F�b�N
    return false;
}

bool UDashStopEvaluator::IsDangerTile(const FIntPoint& Cell, UWorld* World)
{
    if (!World)
    {
        return false;
    }

    // TODO: Phase 3�㔼�Ŏ���
    // �댯�^�C��i㩁A�n�ⓙ�j�̔���
    return false;
}

bool UDashStopEvaluator::IsObstacle(const FIntPoint& Cell, UWorld* World)
{
    if (!World)
    {
        return false;
    }

    // TODO: Phase 3�㔼�Ŏ���
    // ��Q���i�ǁA�����j�̔���
    return false;
}

TArray<FIntPoint> UDashStopEvaluator::GetLinePath(
    const FIntPoint& Start,
    const FIntPoint& Target,
    int32 MaxSteps)
{
    TArray<FIntPoint> Path;
    Path.Reserve(MaxSteps);

    FIntPoint Current = Start;
    FIntPoint Delta = Target - Start;

    // ������K���i-1, 0, 1�j
    FIntPoint Direction;
    Direction.X = FMath::Clamp(Delta.X, -1, 1);
    Direction.Y = FMath::Clamp(Delta.Y, -1, 1);

    // �����o�H�𐶐�
    for (int32 i = 0; i < MaxSteps; ++i)
    {
        Current += Direction;
        Path.Add(Current);

        // �^�[�Q�b�g�ɓ��B������I��
        if (Current == Target)
        {
            break;
        }
    }

    return Path;
}
