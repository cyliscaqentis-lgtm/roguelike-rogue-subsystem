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

ATBSLyraGameMode::ATBSLyraGameMode()
{
	PrimaryActorTick.bCanEverTick = false;
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

	if (Dgn)
	{
		URogueFloorConfigData* Config = LoadObject<URogueFloorConfigData>(nullptr, TEXT("/Game/Rogue/Systems/URogueFloorConfigData.URogueFloorConfigData"));
		if (Config)
		{
			Dgn->RegisterFloorConfig(1, Config);
		}
		Dgn->OnGridReady.AddDynamic(this, &ATBSLyraGameMode::HandleGridComplete);
	}

	BuildDungeonAsync(); // 非同期生成 → 完了時に HandleGridComplete()
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

	DIAG_LOG(Log, TEXT("[TBSLyraGameMode] SpawnBootstrapActors: Dgn=%p (PFL/UM are managed by TurnManager)"),
		static_cast<void*>(Dgn.Get()));
}

void ATBSLyraGameMode::BuildDungeonAsync()
{
	// 既存のダンジョン生成フローにフックしてください
	// 完了時に HandleGridComplete() を呼ぶ
	// 例：URogueDungeonSubsystem の FOnDungeonGenerated デリゲートにバインド
	if (Dgn)
	{
		DIAG_LOG(Log, TEXT("[TBSLyraGameMode] BuildDungeonAsync: Dungeon subsystem ready"));
		// 既存の生成フローが完了したら HandleGridComplete() を呼ぶ
		// ここでは既存のGridComplete()からの呼び出しに依存
		Dgn->TransitionToFloor(1);
	}
}

void ATBSLyraGameMode::GridComplete(const TArray<int32>& Grid, const TArray<AAABB*>& Rooms)
{
	// PathFinderとUnitManagerはGameTurnManagerが所有するため、ここでは初期化しない
	// OnDungeonReadyを発火して、TurnManagerに初期化を委譲
	DIAG_LOG(Log, TEXT("[TBSLyraGameMode] GridComplete: Broadcasting OnDungeonReady (TurnManager will initialize PFL/UM)"));
	
	// 準備完了イベントを発火
	HandleGridComplete();
}

void ATBSLyraGameMode::HandleGridComplete()
{
	// これが唯一の"準備完了"シグナル
	DIAG_LOG(Log, TEXT("[TBSLyraGameMode] HandleGridComplete: Broadcasting OnDungeonReady"));
	OnDungeonReady.Broadcast();
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
