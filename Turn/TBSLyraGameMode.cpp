#include "TBSLyraGameMode.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"  // TActorIterator用
#include "../ProjectDiagnostics.h"
#include "Grid/DungeonFloorGenerator.h"
#include "Grid/GridPathfindingLibrary.h"
#include "Character/UnitManager.h"
#include "Character/UnitBase.h"
#include "Grid/AABB.h"
#include "Grid/URogueDungeonSubsystem.h"
#include "Grid/GridOccupancySubsystem.h"
#include "Rogue/Data/RogueFloorConfigData.h"

namespace TBSLyraGameMode_Private
{
	template<typename TActor>
	TActor* EnsureSingletonActor(UWorld* World, const TCHAR* DebugLabel)
	{
		if (!World)
		{
			return nullptr;
		}

		for (TActorIterator<TActor> It(World); It; ++It)
		{
			if (IsValid(*It))
			{
				DIAG_LOG(Log, TEXT("[TBSLyraGameMode] %s already present: %s"),
					DebugLabel, *GetNameSafe(*It));
				return *It;
			}
		}

		FActorSpawnParameters Params = ATBSLyraGameMode::MakeAlwaysSpawnParams();
		TActor* Spawned = World->SpawnActor<TActor>(TActor::StaticClass(), FTransform::Identity, Params);

		if (IsValid(Spawned))
		{
			DIAG_LOG(Log, TEXT("[TBSLyraGameMode] Spawned %s singleton: %s"),
				DebugLabel, *GetNameSafe(Spawned));
		}
		else
		{
			DIAG_LOG(Error, TEXT("[TBSLyraGameMode] Failed to spawn %s singleton"), DebugLabel);
		}

		return Spawned;
	}
}

ATBSLyraGameMode::ATBSLyraGameMode()
{
	PrimaryActorTick.bCanEverTick = false;
	bStartPlayersAsSpectators = true;
}

FActorSpawnParameters ATBSLyraGameMode::MakeAdjustDontSpawnParams()
{
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
	return Params;
}

FActorSpawnParameters ATBSLyraGameMode::MakeAlwaysSpawnParams()
{
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return Params;
}

void ATBSLyraGameMode::BeginPlay()
{
	Super::BeginPlay();
	
	SpawnBootstrapActors();

	// All dungeon generation logic is now handled by GameTurnManager and URogueDungeonSubsystem
	// The trigger is centralized in GameTurnManager::OnExperienceLoaded.
}

// ======= Trace: GetWorldVariables（BP の pure 関数相当）=======
FWorldVariables ATBSLyraGameMode::GetWorldVariables() const
{
	FWorldVariables Out;
	
	// デフォルト値
	Out.ChanceToDropQuad = 0.f;
	Out.CorridorWidth = 1;
	Out.MapDimensions = FVector(64, 64, 0);
	Out.RoomMinSize = 2;
	Out.TileSize = 100;
	Out.WallThickness = 1;

	// ADungeonFloorGeneratorからオーバーライド
	UWorld* World = GetWorld();
	if (World)
	{
		for (TActorIterator<ADungeonFloorGenerator> It(World); It; ++It)
		{
			ADungeonFloorGenerator* FloorGenerator = *It;
			Out.MapDimensions = FVector(FloorGenerator->GridWidth, FloorGenerator->GridHeight, 0);
			Out.TileSize = FloorGenerator->CellSize;
			Out.RoomMinSize = FloorGenerator->GenParams.MinRoomSize;
			break;
		}
	}

	return Out;
}

void ATBSLyraGameMode::SpawnBootstrapActors()
{
	UWorld* World = GetWorld();
	check(World);

	// PathFinderとUnitManagerはGameTurnManagerが所有・生成するため、ここでは生成しない
	// DungeonSubsystemのみ取得（SubsystemはWorldが所有）
	if (!Dgn)
	{
		Dgn = World->GetSubsystem<URogueDungeonSubsystem>();
		ensure(Dgn);
	}

	TBSLyraGameMode_Private::EnsureSingletonActor<AGridPathfindingLibrary>(World, TEXT("PathFinder"));
	TBSLyraGameMode_Private::EnsureSingletonActor<AUnitManager>(World, TEXT("UnitManager"));

	DIAG_LOG(Log, TEXT("[TBSLyraGameMode] SpawnBootstrapActors: Dgn=%p (PathFinder/UnitManager ensured)"),
		static_cast<void*>(Dgn.Get()));
}

void ATBSLyraGameMode::TeamTurnReset(int32 Team)
{
	// UnitManagerはGameTurnManagerが所有するため、ここからは直接アクセスしない
	// 必要に応じてGameTurnManager経由でアクセスするか、この機能をGameTurnManagerに移す
	DIAG_LOG(Warning, TEXT("[TBSLyraGameMode] TeamTurnReset: UnitManager is now managed by TurnManager. Use TurnManager's API instead."));
	// TODO: GameTurnManager経由でアクセスするか、この機能をGameTurnManagerに移す
}

void ATBSLyraGameMode::UnitDestroyed(FVector Location, AUnitBase* UnitToDestroy)
{
	if (!UnitToDestroy)
	{
		DIAG_LOG(Warning, TEXT("[TBSLyraGameMode] UnitDestroyed: UnitToDestroy is null"));
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// PathFinderとUnitManagerはGameTurnManagerが所有するため、ここからは直接アクセスしない
	// 必要に応じてGameTurnManager経由でアクセスするか、この機能をGameTurnManagerに移す
	DIAG_LOG(Warning, TEXT("[TBSLyraGameMode] UnitDestroyed: PathFinder/UnitManager are now managed by TurnManager. Use TurnManager's API instead."));
	
	// GridOccupancySubsystemから削除
	if (UGridOccupancySubsystem* Occupancy = World->GetSubsystem<UGridOccupancySubsystem>())
	{
		Occupancy->UnregisterActor(UnitToDestroy);
	}

	// アクターを破壊
	if (UnitToDestroy && IsValid(UnitToDestroy))
	{
		UnitToDestroy->Destroy();
		DIAG_LOG(Log, TEXT("[TBSLyraGameMode] UnitDestroyed: Actor destroyed"));
	}
}
