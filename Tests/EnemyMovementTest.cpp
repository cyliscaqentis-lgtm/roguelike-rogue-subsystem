#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Turn/DistanceFieldSubsystem.h"
#include "Grid/GridPathfindingSubsystem.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyMovementTest, "Rogue.EnemyMovement.GetNextStep", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyMovementTest::RunTest(const FString& Parameters)
{
    // Create a temporary world
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    if (!World)
    {
        AddError(TEXT("Failed to create world"));
        return false;
    }

    // Initialize subsystems
    UGridPathfindingSubsystem* GridPathfinding = World->GetSubsystem<UGridPathfindingSubsystem>();
    UDistanceFieldSubsystem* DistanceField = World->GetSubsystem<UDistanceFieldSubsystem>();

    if (!GridPathfinding || !DistanceField)
    {
        AddError(TEXT("Failed to get subsystems"));
        return false;
    }

    // Setup Grid
    // 100x100 grid
    FVector MapSize(100, 100, 0);
    TArray<int32> GridCosts;
    GridCosts.Init(0, 100 * 100); // All floor (cost 0)
    GridPathfinding->InitializeGrid(GridCosts, MapSize, 100);

    // Setup Scenario
    // Player at (32, 31)
    // Enemy at (31, 28)
    FIntPoint PlayerPos(32, 31);
    FIntPoint EnemyPos(31, 28);

    // Update DistanceField
    DistanceField->UpdateDistanceField(PlayerPos);

    // Check DistanceField values
    int32 DistAtEnemy = DistanceField->GetDistance(EnemyPos);
    AddInfo(FString::Printf(TEXT("Distance at Enemy(%d,%d): %d"), EnemyPos.X, EnemyPos.Y, DistAtEnemy));

    // Expected neighbors
    // (32, 29) -> Cost 20. Align 2.
    // (30, 29) -> Cost 28. Align 0.
    
    int32 Dist32_29 = DistanceField->GetDistance(FIntPoint(32, 29));
    AddInfo(FString::Printf(TEXT("Distance at (32,29): %d"), Dist32_29));

    int32 Dist30_29 = DistanceField->GetDistance(FIntPoint(30, 29));
    AddInfo(FString::Printf(TEXT("Distance at (30,29): %d"), Dist30_29));

    // Calculate Next Step
    FIntPoint NextStep = DistanceField->GetNextStepTowardsPlayer(EnemyPos, nullptr);
    
    AddInfo(FString::Printf(TEXT("NextStep: (%d,%d)"), NextStep.X, NextStep.Y));

    // Validation
    if (NextStep == FIntPoint(32, 29))
    {
        AddInfo(TEXT("Success: Chosen (32, 29)"));
    }
    else if (NextStep == FIntPoint(30, 29))
    {
        AddError(TEXT("Failure: Chosen (30, 29) - Suboptimal!"));
    }
    else
    {
        AddError(FString::Printf(TEXT("Failure: Chosen (%d, %d) - Unexpected"), NextStep.X, NextStep.Y));
    }

    return true;
}
