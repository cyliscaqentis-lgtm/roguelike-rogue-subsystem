// CodeRevision: INC-2025-1206-R1 (Extract turn initialization from GameTurnManagerBase) (2025-11-22 00:11)

#include "Turn/TurnInitializationSubsystem.h"
#include "Grid/GridPathfindingSubsystem.h"
#include "Grid/GridOccupancySubsystem.h"
#include "Turn/DistanceFieldSubsystem.h"
#include "AI/Enemy/EnemyAISubsystem.h"
#include "AI/Enemy/EnemyTurnDataSubsystem.h"
#include "Turn/TurnSystemTypes.h"
#include "Utility/GridUtils.h"
#include "GameFramework/Pawn.h"
#include "Turn/GameTurnManagerBase.h"
#include "Debug/DebugObserverCSV.h"
#include "Grid/URogueDungeonSubsystem.h"
#include "Character/UnitManager.h"
#include "TBSLyraGameMode.h"
#include "Kismet/GameplayStatics.h"
#include "Grid/DungeonFloorGenerator.h"
#include "Grid/AABB.h"
#include "Turn/UnitTurnStateSubsystem.h"
#include "Utility/TurnTagCleanupUtils.h"
#include "Utility/TurnAuthorityUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogTurnInit, Log, All);

void UTurnInitializationSubsystem::OnTurnStarted(AGameTurnManagerBase* Manager, int32 TurnId)
{
	if (!Manager) return;

	UE_LOG(LogTurnInit, Warning, TEXT("[Turn %d] ==== OnTurnStarted (Subsystem) START ===="), TurnId);

	UWorld* World = GetWorld();
	if (!World) return;

	// Get PlayerPawn
	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0);

	// Clear residual tags
	TArray<AActor*> EnemiesForCleanup;
	if (UUnitTurnStateSubsystem* UnitState = World->GetSubsystem<UUnitTurnStateSubsystem>())
	{
		UnitState->CopyEnemiesTo(EnemiesForCleanup);
	}
	TurnTagCleanupUtils::ClearResidualInProgressTags(World, PlayerPawn, EnemiesForCleanup);
	
	UE_LOG(LogTurnInit, Log, TEXT("[Turn %d] ClearResidualInProgressTags called"), TurnId);

	// Refresh enemy roster
	if (UUnitTurnStateSubsystem* UnitState = World->GetSubsystem<UUnitTurnStateSubsystem>())
	{
		const int32 CachedBefore = UnitState->GetCachedEnemyRefs().Num();
		
		TArray<AActor*> CollectedEnemies;
		if (UEnemyAISubsystem* EnemyAI = World->GetSubsystem<UEnemyAISubsystem>())
		{
			EnemyAI->CollectAllEnemies(PlayerPawn, CollectedEnemies);
		}
		else
		{
			UE_LOG(LogTurnInit, Warning, TEXT("[Turn %d] EnemyAISubsystem not available"), TurnId);
		}

		UnitState->UpdateEnemies(CollectedEnemies);
		const int32 AfterCount = UnitState->GetCachedEnemyRefs().Num();

		UE_LOG(LogTurnInit, Log, TEXT("[Turn %d] Enemy roster updated: %d -> %d"), TurnId, CachedBefore, AfterCount);
	}

	// Get updated enemies for initialization
	TArray<AActor*> EnemyActors;
	if (UUnitTurnStateSubsystem* UnitState = World->GetSubsystem<UUnitTurnStateSubsystem>())
	{
		UnitState->CopyEnemiesTo(EnemyActors);
	}

	// Initialize Turn
	InitializeTurn(TurnId, PlayerPawn, EnemyActors);

	// Cache intents to Manager
	if (UEnemyTurnDataSubsystem* EnemyData = World->GetSubsystem<UEnemyTurnDataSubsystem>())
	{
		Manager->CachedEnemyIntents = EnemyData->Intents;
	}

	UE_LOG(LogTurnInit, Warning, TEXT("[Turn %d] ==== OnTurnStarted (Subsystem) END ===="), TurnId);
}

void UTurnInitializationSubsystem::ResolveOrSpawnPathFinder(AGameTurnManagerBase* Manager)
{
	if (!Manager) return;

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTurnInit, Error, TEXT("ResolveOrSpawnPathFinder: World is null"));
		return;
	}

	if (IsValid(Manager->PathFinder.Get()))
	{
		return;
	}

	// Get UGridPathfindingSubsystem (subsystems are automatically created by engine, no spawning needed)
	Manager->PathFinder = World->GetSubsystem<UGridPathfindingSubsystem>();

	if (IsValid(Manager->PathFinder.Get()))
	{
		UE_LOG(LogTurnInit, Log, TEXT("ResolveOrSpawnPathFinder: Retrieved UGridPathfindingSubsystem"));
	}
	else
	{
		UE_LOG(LogTurnInit, Error, TEXT("ResolveOrSpawnPathFinder: Failed to get UGridPathfindingSubsystem!"));
	}
}

void UTurnInitializationSubsystem::ResolveOrSpawnUnitManager(AGameTurnManagerBase* Manager)
{
	if (!Manager) return;

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTurnInit, Error, TEXT("ResolveOrSpawnUnitManager: World is null"));
		return;
	}

	if (IsValid(Manager->UnitMgr))
	{
		return;
	}

	// Try to find existing UnitManager
	TArray<AActor*> Found;
	UGameplayStatics::GetAllActorsOfClass(World, AUnitManager::StaticClass(), Found);
	if (Found.Num() > 0)
	{
		Manager->UnitMgr = Cast<AUnitManager>(Found[0]);
		UE_LOG(LogTurnInit, Log, TEXT("ResolveOrSpawnUnitManager: Found existing UnitManager: %s"), *GetNameSafe(Manager->UnitMgr));
		return;
	}

	// Try to get from GameMode
	if (ATBSLyraGameMode* GM = World->GetAuthGameMode<ATBSLyraGameMode>())
	{
		Manager->UnitMgr = GM->GetUnitManager();
		if (IsValid(Manager->UnitMgr))
		{
			UE_LOG(LogTurnInit, Log, TEXT("ResolveOrSpawnUnitManager: Got UnitManager from GameMode: %s"), *GetNameSafe(Manager->UnitMgr));
			return;
		}
	}

	// Spawn new UnitManager
	UE_LOG(LogTurnInit, Log, TEXT("ResolveOrSpawnUnitManager: Spawning UnitManager"));
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Manager->UnitMgr = World->SpawnActor<AUnitManager>(AUnitManager::StaticClass(), FTransform::Identity, Params);

	if (IsValid(Manager->UnitMgr))
	{
		UE_LOG(LogTurnInit, Log, TEXT("ResolveOrSpawnUnitManager: Spawned UnitManager: %s"), *GetNameSafe(Manager->UnitMgr));
	}
	else
	{
		UE_LOG(LogTurnInit, Error, TEXT("ResolveOrSpawnUnitManager: Failed to spawn UnitManager!"));
	}
}

bool UTurnInitializationSubsystem::CanStartFirstTurn() const
{
	return bPathReady && bUnitsSpawned && bPlayerPossessed && !bFirstTurnStarted;
}

void UTurnInitializationSubsystem::NotifyPlayerPossessed(APawn* NewPawn)
{
	bPlayerPossessed = true;
	UE_LOG(LogTurnInit, Log, TEXT("NotifyPlayerPossessed: %s"), *GetNameSafe(NewPawn));
}

void UTurnInitializationSubsystem::NotifyPathReady()
{
	bPathReady = true;
	UE_LOG(LogTurnInit, Log, TEXT("NotifyPathReady: Pathfinding system is ready"));
}

void UTurnInitializationSubsystem::NotifyUnitsSpawned()
{
	bUnitsSpawned = true;
	UE_LOG(LogTurnInit, Log, TEXT("NotifyUnitsSpawned: Unit spawning complete"));
}

void UTurnInitializationSubsystem::MarkFirstTurnStarted()
{
	bFirstTurnStarted = true;
	UE_LOG(LogTurnInit, Log, TEXT("MarkFirstTurnStarted: First turn has been started"));
}

void UTurnInitializationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogTurnInit, Log, TEXT("TurnInitializationSubsystem initialized"));
}

void UTurnInitializationSubsystem::Deinitialize()
{
	Super::Deinitialize();
	UE_LOG(LogTurnInit, Log, TEXT("TurnInitializationSubsystem deinitialized"));
}

void UTurnInitializationSubsystem::InitializeGameTurnManager(AGameTurnManagerBase* Manager)
{
	if (!Manager) return;

	UE_LOG(LogTurnInit, Warning, TEXT("InitializeGameTurnManager: START..."));

	// CSV Observer
	UDebugObserverCSV* CSVObserver = Manager->FindComponentByClass<UDebugObserverCSV>();
	if (CSVObserver)
	{
		Manager->DebugObservers.Add(CSVObserver);
		Manager->CachedCsvObserver = CSVObserver;
		UE_LOG(LogTurnInit, Log, TEXT("InitializeGameTurnManager: Found and added UDebugObserverCSV to DebugObservers."));

		CSVObserver->MarkSessionStart();
		CSVObserver->SetCurrentTurnForLogging(0);
	}
	else
	{
		UE_LOG(LogTurnInit, Warning, TEXT("InitializeGameTurnManager: UDebugObserverCSV component not found."));
	}

	if (!IsAuthorityLike(GetWorld(), Manager))
	{
		UE_LOG(LogTurnInit, Warning, TEXT("InitializeGameTurnManager: Not authoritative, skipping"));
		return;
	}

	Manager->InitGameplayTags();

	ATBSLyraGameMode* GM = GetWorld()->GetAuthGameMode<ATBSLyraGameMode>();
	if (!ensure(GM))
	{
		UE_LOG(LogTurnInit, Error, TEXT("InitializeGameTurnManager: GameMode not found"));
		return;
	}

	UWorld* World = GetWorld();
	if (!World) return;

	Manager->DungeonSys = World->GetSubsystem<URogueDungeonSubsystem>();

	UE_LOG(LogTurnInit, Warning, TEXT("InitializeGameTurnManager: DungeonSys=%p"), static_cast<void*>(Manager->DungeonSys.Get()));

	if (Manager->DungeonSys)
	{
		Manager->DungeonSys->OnGridReady.AddDynamic(Manager, &AGameTurnManagerBase::HandleDungeonReady);
		UE_LOG(LogTurnInit, Log, TEXT("InitializeGameTurnManager: Subscribed to DungeonSys->OnGridReady"));

		UE_LOG(LogTurnInit, Log, TEXT("InitializeGameTurnManager: Triggering dungeon generation..."));
		Manager->DungeonSys->StartGenerateFromLevel();
	}
	else
	{
		UE_LOG(LogTurnInit, Error, TEXT("InitializeGameTurnManager: DungeonSys is null!"));
	}

	UE_LOG(LogTurnInit, Log, TEXT("InitializeGameTurnManager: Ready for HandleDungeonReady"));
}

void UTurnInitializationSubsystem::HandleDungeonReady(AGameTurnManagerBase* Manager, URogueDungeonSubsystem* DungeonSys)
{
	if (!Manager || !DungeonSys)
	{
		UE_LOG(LogTurnInit, Error, TEXT("HandleDungeonReady: Invalid Manager or DungeonSys"));
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTurnInit, Error, TEXT("HandleDungeonReady: World is null"));
		return;
	}

	UGridPathfindingSubsystem* GridPathfindingSubsystem = World->GetSubsystem<UGridPathfindingSubsystem>();
	if (!GridPathfindingSubsystem)
	{
		UE_LOG(LogTurnInit, Error, TEXT("HandleDungeonReady: GridPathfindingSubsystem not available"));
		return;
	}

	if (!Manager->PathFinder)
	{
		Manager->PathFinder = GridPathfindingSubsystem;
	}

	if (!Manager->UnitMgr)
	{
		UE_LOG(LogTurnInit, Log, TEXT("HandleDungeonReady: Creating UnitManager..."));
		ResolveOrSpawnUnitManager(Manager);
	}

	if (!IsValid(Manager->PathFinder) || !IsValid(Manager->UnitMgr))
	{
		UE_LOG(LogTurnInit, Error, TEXT("HandleDungeonReady: Dependencies not ready"));
		return;
	}

	// Initialize GridPathfindingSubsystem
	if (ADungeonFloorGenerator* Floor = DungeonSys->GetFloorGenerator())
	{
		FGridInitParams InitParams;
		InitParams.GridCostArray = Floor->GridCells;
		InitParams.MapSize = FVector(Floor->GridWidth, Floor->GridHeight, 0.f);
		InitParams.TileSizeCM = Floor->CellSize;
		InitParams.Origin = FVector::ZeroVector;

		GridPathfindingSubsystem->InitializeFromParams(InitParams);

		UE_LOG(LogTurnInit, Warning, TEXT("[PF.Init] Size=%dx%d Cell=%d"), Floor->GridWidth, Floor->GridHeight, Floor->CellSize);

		Manager->bPathReady = true;
	}
	else
	{
		UE_LOG(LogTurnInit, Error, TEXT("HandleDungeonReady: FloorGenerator not available"));
	}

	if (IsValid(Manager->UnitMgr))
	{
		Manager->UnitMgr->PathFinder = Manager->PathFinder;

		if (ADungeonFloorGenerator* Floor = DungeonSys->GetFloorGenerator())
		{
			Manager->UnitMgr->MapSize = FVector(Floor->GridWidth, Floor->GridHeight, 0.f);
		}

		TArray<AAABB*> GeneratedRooms;
		DungeonSys->GetGeneratedRooms(GeneratedRooms);

		Manager->UnitMgr->BuildUnits(GeneratedRooms);
		Manager->bUnitsSpawned = true;
	}

	UE_LOG(LogTurnInit, Log, TEXT("HandleDungeonReady: Completed, initializing turn system..."));

	Manager->InitializeTurnSystem();
	Manager->TryStartFirstTurn();
}

void UTurnInitializationSubsystem::InitializeTurn(int32 TurnId, APawn* PlayerPawn, const TArray<AActor*>& Enemies)
{
	CurrentTurnId = TurnId;

	UE_LOG(LogTurnInit, Warning, TEXT("[Turn %d] ==== TurnInitialization START ===="), TurnId);

	// 1. GridOccupancy initialization
	InitializeGridOccupancy(TurnId, PlayerPawn, Enemies);

	// 2. DistanceField update
	if (PlayerPawn && Enemies.Num() > 0)
	{
		UpdateDistanceField(PlayerPawn, Enemies);
	}
	else
	{
		UE_LOG(LogTurnInit, Warning, TEXT("[Turn %d] Skipping DistanceField update: PlayerPawn=%s, Enemies=%d"),
			TurnId, PlayerPawn ? TEXT("Valid") : TEXT("NULL"), Enemies.Num());
	}

	// 3. Preliminary intent generation
	TArray<FEnemyIntent> PreliminaryIntents;
	if (GeneratePreliminaryIntents(PlayerPawn, Enemies, PreliminaryIntents))
	{
		// Store intents in EnemyTurnDataSubsystem
		if (UEnemyTurnDataSubsystem* EnemyData = GetWorld()->GetSubsystem<UEnemyTurnDataSubsystem>())
		{
			EnemyData->Intents = PreliminaryIntents;
			UE_LOG(LogTurnInit, Log, TEXT("[Turn %d] Stored %d preliminary intents in EnemyTurnDataSubsystem"),
				TurnId, PreliminaryIntents.Num());
		}
	}

	UE_LOG(LogTurnInit, Warning, TEXT("[Turn %d] ==== TurnInitialization END ===="), TurnId);
}

bool UTurnInitializationSubsystem::UpdateDistanceField(APawn* PlayerPawn, const TArray<AActor*>& Enemies)
{
	UWorld* World = GetWorld();
	if (!World || !PlayerPawn)
	{
		UE_LOG(LogTurnInit, Error, TEXT("[Turn %d] UpdateDistanceField: Invalid World or PlayerPawn"), CurrentTurnId);
		return false;
	}

	UGridPathfindingSubsystem* PathFinder = World->GetSubsystem<UGridPathfindingSubsystem>();
	UDistanceFieldSubsystem* DistanceField = World->GetSubsystem<UDistanceFieldSubsystem>();
	UGridOccupancySubsystem* Occupancy = World->GetSubsystem<UGridOccupancySubsystem>();

	if (!PathFinder || !DistanceField || !Occupancy)
	{
		UE_LOG(LogTurnInit, Error, TEXT("[Turn %d] Required subsystems not available: PathFinder=%d, DistanceField=%d, Occupancy=%d"),
			CurrentTurnId, PathFinder != nullptr, DistanceField != nullptr, Occupancy != nullptr);
		return false;
	}

	UE_LOG(LogTurnInit, Warning, TEXT("[Turn %d] Attempting to update DistanceField..."), CurrentTurnId);

	// Get player grid position
	const FIntPoint PlayerGrid = Occupancy->GetCellOfActor(PlayerPawn);

	// Collect enemy positions
	TSet<FIntPoint> EnemyPositions;
	for (AActor* Enemy : Enemies)
	{
		if (IsValid(Enemy))
		{
			FIntPoint EnemyGrid = PathFinder->WorldToGrid(Enemy->GetActorLocation());
			EnemyPositions.Add(EnemyGrid);
		}
	}

	// Calculate margin
	const int32 Margin = CalculateDistanceFieldMargin(PlayerGrid, EnemyPositions);

	UE_LOG(LogTurnInit, Log, TEXT("[Turn %d] DF: Player=(%d,%d) Enemies=%d Margin=%d"),
		CurrentTurnId, PlayerGrid.X, PlayerGrid.Y, EnemyPositions.Num(), Margin);

	// Update distance field
	DistanceField->UpdateDistanceFieldOptimized(PlayerGrid, EnemyPositions, Margin);

	// Validate reachability
	const bool bAllReachable = ValidateDistanceFieldReachability(CurrentTurnId, EnemyPositions, Margin);

	return bAllReachable;
}

bool UTurnInitializationSubsystem::GeneratePreliminaryIntents(
	APawn* PlayerPawn,
	const TArray<AActor*>& Enemies,
	TArray<FEnemyIntent>& OutIntents)
{
	UWorld* World = GetWorld();
	if (!World || !PlayerPawn || Enemies.Num() == 0)
	{
		UE_LOG(LogTurnInit, Warning, TEXT("[Turn %d] Cannot generate intents: World=%s, PlayerPawn=%s, Enemies=%d"),
			CurrentTurnId, World ? TEXT("Valid") : TEXT("NULL"), 
			PlayerPawn ? TEXT("Valid") : TEXT("NULL"), Enemies.Num());
		return false;
	}

	UEnemyAISubsystem* EnemyAISys = World->GetSubsystem<UEnemyAISubsystem>();
	UGridPathfindingSubsystem* PathFinder = World->GetSubsystem<UGridPathfindingSubsystem>();
	UGridOccupancySubsystem* Occupancy = World->GetSubsystem<UGridOccupancySubsystem>();

	if (!EnemyAISys || !PathFinder || !Occupancy)
	{
		UE_LOG(LogTurnInit, Error, TEXT("[Turn %d] Required subsystems not available for intent generation: EnemyAI=%d, PathFinder=%d, Occupancy=%d"),
			CurrentTurnId, EnemyAISys != nullptr, PathFinder != nullptr, Occupancy != nullptr);
		return false;
	}

	UE_LOG(LogTurnInit, Warning, TEXT("[Turn %d] Generating preliminary enemy intents..."), CurrentTurnId);

	// Get player grid coordinates
	const FIntPoint PlayerGrid = Occupancy->GetCellOfActor(PlayerPawn);

	// Build observations
	TArray<FEnemyObservation> PreliminaryObs;
	EnemyAISys->BuildObservations(Enemies, PlayerGrid, PathFinder, PreliminaryObs);

	// Collect intents
	EnemyAISys->CollectIntents(PreliminaryObs, Enemies, OutIntents);

	UE_LOG(LogTurnInit, Warning,
		TEXT("[Turn %d] Preliminary intents generated: %d intents from %d enemies (will be updated after player move)"),
		CurrentTurnId, OutIntents.Num(), Enemies.Num());

	return OutIntents.Num() > 0;
}

void UTurnInitializationSubsystem::InitializeGridOccupancy(int32 TurnId, APawn* PlayerPawn, const TArray<AActor*>& Enemies)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTurnInit, Error, TEXT("[Turn %d] InitializeGridOccupancy: Invalid World"), TurnId);
		return;
	}

	UGridOccupancySubsystem* GridOccupancy = World->GetSubsystem<UGridOccupancySubsystem>();
	if (!GridOccupancy)
	{
		UE_LOG(LogTurnInit, Error, TEXT("[Turn %d] GridOccupancySubsystem not available"), TurnId);
		return;
	}

	// Set turn ID
	GridOccupancy->SetCurrentTurnId(TurnId);

	// Purge outdated reservations
	GridOccupancy->PurgeOutdatedReservations(TurnId);
	UE_LOG(LogTurnInit, Log,
		TEXT("[Turn %d] PurgeOutdatedReservations called at turn start (before player input)"),
		TurnId);

	// Collect all units
	TArray<AActor*> AllUnits;
	if (PlayerPawn)
	{
		AllUnits.Add(PlayerPawn);
	}
	AllUnits.Append(Enemies);

	if (AllUnits.Num() > 0)
	{
		// Note: RebuildFromWorldPositions is destructive and causes state loss
		// We rely on incremental updates instead
		UE_LOG(LogTurnInit, Log, TEXT("[Turn %d] GridOccupancy initialized with %d units"), TurnId, AllUnits.Num());
	}
	else
	{
		GridOccupancy->EnforceUniqueOccupancy();
		UE_LOG(LogTurnInit, Warning,
			TEXT("[Turn %d] EnforceUniqueOccupancy called (fallback - no units cached yet)"),
			TurnId);
	}
}

int32 UTurnInitializationSubsystem::CalculateDistanceFieldMargin(const FIntPoint& PlayerGrid, const TSet<FIntPoint>& EnemyPositions) const
{
	auto Manhattan = [](const FIntPoint& A, const FIntPoint& B) -> int32
	{
		return FGridUtils::ManhattanDistance(A, B);
	};

	int32 MaxD = 0;
	for (const FIntPoint& C : EnemyPositions)
	{
		int32 Dist = Manhattan(C, PlayerGrid);
		if (Dist > MaxD)
		{
			MaxD = Dist;
		}
	}

	const int32 Margin = FMath::Clamp(MaxD + 4, 8, 64);
	return Margin;
}

bool UTurnInitializationSubsystem::ValidateDistanceFieldReachability(int32 TurnId, const TSet<FIntPoint>& EnemyPositions, int32 Margin) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	UDistanceFieldSubsystem* DistanceField = World->GetSubsystem<UDistanceFieldSubsystem>();
	if (!DistanceField)
	{
		return false;
	}

	bool bAnyUnreached = false;
	for (const FIntPoint& C : EnemyPositions)
	{
		const int32 D = DistanceField->GetDistance(C);
		if (D < 0)
		{
			bAnyUnreached = true;
			UE_LOG(LogTurnInit, Warning, TEXT("[Turn %d] Enemy at (%d,%d) unreached! Dist=%d"),
				TurnId, C.X, C.Y, D);
		}
	}

	if (bAnyUnreached)
	{
		UE_LOG(LogTurnInit, Error, TEXT("[Turn %d] ❌DistanceField has unreachable enemies with Margin=%d"),
			TurnId, Margin);
		return false;
	}
	else
	{
		UE_LOG(LogTurnInit, Log, TEXT("[Turn %d] ✅All enemies reachable with Margin=%d"),
			TurnId, Margin);
		return true;
	}
}
