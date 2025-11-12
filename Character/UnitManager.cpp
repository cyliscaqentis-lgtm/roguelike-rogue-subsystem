#include "UnitManager.h"

#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "UObject/ConstructorHelpers.h"
#include "Components/CapsuleComponent.h"

// ===== あなたのプロジェクト固有のヘッダ =====
#include "Grid/AABB.h"
#include "Character/UnitBase.h"
#include "Character/EnemyUnitBase.h"
#include "Grid/GridPathfindingLibrary.h"
#include "Grid/GridOccupancySubsystem.h"
#include "Player/PlayerControllerBase.h"
#include "Debug/DebugObserverCSV.h"
#include "UObject/UObjectGlobals.h"
#include "Character/LyraPawnData.h"

namespace UnitManager_Private
{
	static void OccupyInitialCell(UWorld* World, AGridPathfindingLibrary* PathFinder, AActor* Actor)
	{
		if (!World || !PathFinder || !Actor)
		{
			return;
		}

		if (UGridOccupancySubsystem* Occupancy = World->GetSubsystem<UGridOccupancySubsystem>())
		{
			const FIntPoint Cell = PathFinder->WorldToGrid(Actor->GetActorLocation());
			Occupancy->OccupyCell(Cell, Actor);
		}
	}
}

AUnitManager::AUnitManager()
{
	PrimaryActorTick.bCanEverTick = false;

	// TeamSize を 2 要素にしておく（[0]=プレイヤー, [1]=敵）
	EnsureTeamSize2();

	// C++で BP_EnemyUnitBase を直接参照して設定
	static ConstructorHelpers::FClassFinder<AEnemyUnitBase> BP_EnemyUnitFinder(TEXT("/Game/Rogue/Characters/BP_EnemyUnitBase"));
	if (BP_EnemyUnitFinder.Succeeded())
	{
		EnemyUnitClass = BP_EnemyUnitFinder.Class;
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[UnitManager] Failed to find BP_EnemyUnitBase. Check the path in the constructor. Spawning will fail."));
	}

	if (!DefaultEnemyPawnData)
	{
		DefaultEnemyPawnData = LoadObject<ULyraPawnData>(nullptr, TEXT("/Game/Characters/Heroes/SimplePawnData/TBS_EnemyPawnData.TBS_EnemyPawnData"));
		if (!DefaultEnemyPawnData)
		{
			UE_LOG(LogTemp, Warning, TEXT("[UnitManager] Failed to load default enemy PawnData asset."));
		}
	}
}

void AUnitManager::EnsureTeamSize2()
{
	if (TeamSize.Num() < 2)
	{
		TeamSize.SetNum(2);
	}

	if (TeamSize[0] <= 0)
	{
		TeamSize[0] = 1;
	}
}

void AUnitManager::BeginPlay()
{
	Super::BeginPlay();
	// Trace: Event BeginPlay（BP では何もしていない）
}

// ========================= Trace: InitUnitArrays =========================
void AUnitManager::InitUnitArrays()
{
	EnsureTeamSize2();

	const int32 DesiredEnemies = FMath::Max(InitialEnemyCount, 1);
	TeamSize[1] = DesiredEnemies;

	// StatBlock を (TeamSize[1] + TeamSize[0]) にリサイズし、DefaultStatBlock を詰める
	const int32 Total = TeamSize[1] + TeamSize[0];

	TArray<FUnitStatBlock> Empty;
	StatBlock = MoveTemp(Empty);
	StatBlock.SetNum(Total);

	for (int32 i = 0; i < Total; ++i)
	{
		StatBlock[i] = DefaultStatBlock;
	}
}

// ========================= Trace: GenerateBasicEnemyTeam =========================
void AUnitManager::GenerateBasicEnemyTeam()
{
	EnsureTeamSize2();
	const int32 Start = TeamSize[0];
	const int32 End   = TeamSize[1] + (TeamSize[0] - 1);

	for (int32 idx = Start; idx <= End && idx < StatBlock.Num(); ++idx)
	{
		GetUnitStatBlock(idx);

		for (int32 p = 1; p <= DefaultPoints; ++p)
		{
			switch (FMath::RandRange(0, 3))
			{
			case 0: Strength     += 1; break;
			case 1: Intelligence += 1; break;
			case 2: Dexterity    += 1; break;
			case 3: Constitution += 1; break;
			default: Strength    += 1; break;
			}
		}

		CalculateDerivedValues();
		SetStartingHealthAndPower();
		SetUnitStatBlock(idx);
	}
}

int32 AUnitManager::SpawnEnemyUnits(int32 DesiredEnemyCount)
{
	EnsureTeamSize2();

	UE_LOG(LogTemp, Warning, TEXT("[UnitManager::SpawnEnemyUnits] START - DesiredCount=%d, bEnemiesSpawned=%d"),
		DesiredEnemyCount, bEnemiesSpawned);

	if (!PathFinder)
	{
		UE_LOG(LogTemp, Error, TEXT("UnitManager::SpawnEnemyUnits: PathFinder not set"));
		return 0;
	}

	if (bEnemiesSpawned)
	{
		UE_LOG(LogTemp, Warning, TEXT("UnitManager::SpawnEnemyUnits: Already spawned enemies, skipping (bEnemiesSpawned=%d)"), bEnemiesSpawned);
		return 0;
	}

	if (!PlayerStartRoom)
	{
		UE_LOG(LogTemp, Error, TEXT("UnitManager::SpawnEnemyUnits: PlayerStartRoom not set"));
		return 0;
	}

	const int32 TargetEnemyCount = (DesiredEnemyCount >= 0)
		? DesiredEnemyCount
		: TeamSize[1];

	if (TargetEnemyCount <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("UnitManager::SpawnEnemyUnits: TargetEnemyCount <= 0"));
		return 0;
	}

	if (!bEnemyStatsInitialized)
	{
		GenerateBasicEnemyTeam();
		bEnemyStatsInitialized = true;
	}

	// ★★★ 変更：敵をプレイヤーと同じ部屋（PlayerStartRoom）にスポーン ★★★
	UE_LOG(LogTemp, Log, TEXT("[UnitManager::SpawnEnemyUnits] Spawning %d enemies in PlayerStartRoom at %s"),
		TargetEnemyCount, *PlayerStartRoom->GetActorLocation().ToString());

	int32 SpawnedEnemies = 0;
	bool bLoggedMissingEnemyClass = false;

	// すべての敵をPlayerStartRoomにスポーン
	const TArray<FVector> EnemySpawns = SpawnLocations(PlayerStartRoom, TargetEnemyCount);

	UE_LOG(LogTemp, Log, TEXT("[UnitManager::SpawnEnemyUnits] Found %d spawn locations in PlayerStartRoom"),
		EnemySpawns.Num());

	for (const FVector& Loc : EnemySpawns)
	{
		if (!EnemyUnitClass)
		{
			if (!bLoggedMissingEnemyClass)
			{
				UE_LOG(LogTemp, Error, TEXT("UnitManager::SpawnEnemyUnits: EnemyUnitClass is not set."));
				bLoggedMissingEnemyClass = true;
			}
			continue;
		}

		UWorld* World = GetWorld();
		if (!World)
		{
			UE_LOG(LogTemp, Error, TEXT("UnitManager::SpawnEnemyUnits: World is null"));
			return SpawnedEnemies;
		}

		// ★★★ CDOからカプセル半高を取得して床から正しい高さでスポーン ★★★
		float CapsuleHalfHeight = 0.f;
		if (EnemyUnitClass)
		{
			const AEnemyUnitBase* CDO = EnemyUnitClass->GetDefaultObject<AEnemyUnitBase>();
			if (CDO && CDO->GetCapsuleComponent())
			{
				CapsuleHalfHeight = CDO->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			}
		}

		// 安全マージン（床めり込み回避）
		constexpr float ZEpsilon = 0.5f;

		// 床(Z=0) + カプセル半高で配置 → BeginPlayの平面拘束でこの高さに固定される
		FVector SpawnLoc = Loc;
		SpawnLoc.Z = Loc.Z + CapsuleHalfHeight + ZEpsilon;

		UE_LOG(LogTemp, Log, TEXT("[UnitManager::SpawnEnemyUnits] Spawning enemy at: %s (Z=%.2f, HalfHeight=%.2f)"),
			*SpawnLoc.ToString(), SpawnLoc.Z, CapsuleHalfHeight);

		FTransform SpawnTM(FRotator::ZeroRotator, SpawnLoc, FVector::OneVector);
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

		AEnemyUnitBase* Enemy = World->SpawnActor<AEnemyUnitBase>(EnemyUnitClass, SpawnTM, P);
		if (!Enemy)
		{
			UE_LOG(LogTemp, Warning, TEXT("[UnitManager::SpawnEnemyUnits] Failed to spawn enemy at %s"),
				*SpawnLoc.ToString());
			continue;
		}

		if (Enemy->HasAuthority())
		{
			if (AEnemyUnitBase* EnemyUnit = Cast<AEnemyUnitBase>(Enemy))
			{
				ULyraPawnData* PawnDataToApply = DefaultEnemyPawnData;

				if (!PawnDataToApply)
				{
					if (EnemyUnitClass)
					{
						if (const AEnemyUnitBase* EnemyCDO = Cast<AEnemyUnitBase>(EnemyUnitClass->GetDefaultObject()))
						{
							PawnDataToApply = EnemyCDO->GetEnemyPawnData();
						}
					}
				}

				// ★★★ 修正: PawnData設定後にコントローラーをスポーン ★★★
				if (PawnDataToApply)
				{
					EnemyUnit->SetEnemyPawnData(PawnDataToApply);

					// PawnData設定後、コントローラーをスポーン（bDeferredControllerSpawn=trueのため）
					if (!EnemyUnit->GetController())
					{
						EnemyUnit->SpawnDefaultController();
						UE_LOG(LogTemp, Log, TEXT("[UnitManager] Spawned controller for %s after PawnData setup"),
							*EnemyUnit->GetName());
					}
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("[UnitManager] No PawnData available for %s (DefaultEnemyPawnData unset, class CDO empty)"),
						*EnemyUnit->GetName());
				}
			}

			Enemy->StatBlock = DefaultStatBlock;
			Enemy->SetStatVars();
			Enemy->SetActorHiddenInGame(false);
			Enemy->Team = 1;
			AllUnits.Add(Enemy);

			UnitManager_Private::OccupyInitialCell(World, PathFinder, Enemy);
			++SpawnedEnemies;

			UE_LOG(LogTemp, Log, TEXT("[UnitManager::SpawnEnemyUnits] Successfully spawned enemy %d/%d: %s"),
				SpawnedEnemies, TargetEnemyCount, *Enemy->GetName());
		}
	}

	if (SpawnedEnemies >= TargetEnemyCount)
	{
		UE_LOG(LogTemp, Log, TEXT("UnitManager::SpawnEnemyUnits: Spawned %d enemies"), SpawnedEnemies);
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("UnitManager::SpawnEnemyUnits: Spawned %d/%d enemies (not enough rooms?)"),
			SpawnedEnemies, TargetEnemyCount);
	}

	bEnemiesSpawned = SpawnedEnemies > 0;
	return SpawnedEnemies;
}

// ========================= Trace: GetUnitStatBlock =========================
void AUnitManager::GetUnitStatBlock(int32 Index)
{
	if (!StatBlock.IsValidIndex(Index)) return;
	const FUnitStatBlock& B = StatBlock[Index];

	Strength=B.Strength; Intelligence=B.Intelligence; Dexterity=B.Dexterity; Constitution=B.Constitution;
	Health=B.MaxHealth; CurrentHealth=B.CurrentHealth; Power=B.MaxPower; CurrentPower=B.CurrentPower;
	Speed=B.MaxSpeed; CurrentSpeed=B.CurrentSpeed;
	MaxSightRange=B.MaxSightRange; CurrentSightRange=B.CurrentSightRange;

	bCanAct=B.bCanAct; bHasActed=B.bHasActed; bIsDisabled=B.bIsDisabled; bCanMove=B.bCanMove; bHasMoved=B.bHasMoved; bIsRooted=B.bIsRooted;

	Team=B.Team; MovementRange=B.RangePerMove; NumberOfMoves=B.NumberOfMoves; CurrentMovementRange=B.CurrentTotalMovementRange;

	MeleeBaseAttack=B.MeleeBaseAttack; RangedBaseAttack=B.RangedBaseAttack; MagicBaseAttack=B.MagicBaseAttack;
	MeleeBaseDamage=B.MeleeBaseDamage; RangedBaseDamage=B.RangedBaseDamage; MagicBaseDamage=B.MagicBaseDamage;

	BaseDamageAbsorb=B.BaseDamageAbsorb; BaseDamageAvoidance=B.BaseDamageAvoidance; BaseMagicResist=B.BaseMagicResist; BaseMagicPenetration=B.BaseMagicPenetration;
	CurrentDamageAbsorb=B.CurrentDamageAbsorb; CurrentDamageAvoidance=B.CurrentDamageAvoidance; CurrentMagicResist=B.CurrentMagicResist; CurrentMagicPenetration=B.CurrentMagicPenetration;
}

// ========================= Trace: SetUnitStatBlock =========================
void AUnitManager::SetUnitStatBlock(int32 Index)
{
	if (!StatBlock.IsValidIndex(Index)) return;

	FUnitStatBlock Tmp = StatBlock[Index];

	Tmp.Strength=Strength; Tmp.Intelligence=Intelligence; Tmp.Dexterity=Dexterity; Tmp.Constitution=Constitution;

	Tmp.MaxHealth=Health; Tmp.CurrentHealth=CurrentHealth;
	Tmp.MaxPower=Power;   Tmp.CurrentPower=CurrentPower;

	Tmp.MaxSpeed=Speed;   Tmp.CurrentSpeed=CurrentSpeed;

	Tmp.MaxSightRange=MaxSightRange; Tmp.CurrentSightRange=CurrentSightRange;

	Tmp.bCanAct=bCanAct; Tmp.bHasActed=bHasActed; Tmp.bIsDisabled=bIsDisabled; Tmp.bCanMove=bCanMove; Tmp.bHasMoved=bHasMoved; Tmp.bIsRooted=bIsRooted;

	Tmp.Team=Team; Tmp.RangePerMove=MovementRange; Tmp.NumberOfMoves=NumberOfMoves; Tmp.CurrentTotalMovementRange=CurrentMovementRange;

	Tmp.MeleeBaseAttack=MeleeBaseAttack; Tmp.RangedBaseAttack=RangedBaseAttack; Tmp.MagicBaseAttack=MagicBaseAttack;

	Tmp.MeleeBaseDamage=MeleeBaseDamage; Tmp.RangedBaseDamage=RangedBaseDamage; Tmp.MagicBaseDamage=MagicBaseDamage;

	Tmp.BaseDamageAbsorb=BaseDamageAbsorb; Tmp.BaseDamageAvoidance=BaseDamageAvoidance; Tmp.BaseMagicResist=BaseMagicResist; Tmp.BaseMagicPenetration=BaseMagicPenetration;

	Tmp.CurrentDamageAbsorb=CurrentDamageAbsorb; Tmp.CurrentDamageAvoidance=CurrentDamageAvoidance; Tmp.CurrentMagicResist=CurrentMagicResist; Tmp.CurrentMagicPenetration=CurrentMagicPenetration;

	StatBlock[Index] = Tmp;
}

// ========================= Trace: CalculateDerivedValues =========================
void AUnitManager::CalculateDerivedValues()
{
	const int32 SumSD = Strength + Dexterity;
	if (SumSD >= 10 && SumSD <= 17)      Speed = 600.f;
	else if (SumSD > 17)                 Speed = 750.f;
	else                                 Speed = 450.f;

	Health = ((Constitution * 1.75f) + (Strength * 0.65f)) * 5.f;

	const int32 Major = (Strength >= Intelligence) ? Strength : Intelligence;
	const int32 Max3  = (Major >= Dexterity) ? Major : Dexterity;
	Power = Max3 * 5.f;

    NumberOfMoves = FMath::Max(1, Constitution / 4);
    MovementRange = FMath::Max(1, Dexterity / 4);
    CurrentMovementRange = FMath::Max(1, MovementRange * NumberOfMoves);
    UE_LOG(LogTemp, Log,
        TEXT("[UnitManager] Calculated move stats: Dex=%d Con=%d -> MovementRange=%d, NumberOfMoves=%d, CurrentMovement=%d"),
        Dexterity, Constitution, MovementRange, NumberOfMoves, CurrentMovementRange);

	CurrentMagicResist      = (Intelligence * 1.25f) + BaseMagicResist;
	CurrentDamageAvoidance  = (Dexterity    * 1.25f) + BaseDamageAvoidance;
	CurrentMagicPenetration = (Intelligence * 1.0f ) + BaseMagicPenetration;
	CurrentDamageAbsorb     = (Constitution * 0.65f) + (Strength * 0.65f) + BaseDamageAbsorb;

	MagicBaseAttack  = FMath::FloorToInt(Intelligence * 2.5f);
	RangedBaseAttack = FMath::FloorToInt(Dexterity    * 2.5f);
	MeleeBaseAttack  = FMath::FloorToInt(Strength     * 2.5f);

	MagicBaseDamage  = (Intelligence + 25) * 1.65f;
	RangedBaseDamage = (Dexterity    + 25) * 1.65f;
	MeleeBaseDamage  = (Strength     + 25) * 1.65f;

	CurrentSpeed = Speed;
}

// ========================= Trace: SetStartingHealthAndPower =========================
void AUnitManager::SetStartingHealthAndPower()
{
	CurrentHealth = Health;
	CurrentPower  = Power;
}

// ========================= Trace: BuildUnits(RoomsIn) =========================
void AUnitManager::BuildUnits(const TArray<AAABB*>& RoomsIn)
{
	InitUnitArrays();

	Rooms = RoomsIn;
	BigEnoughRooms.Reset();
	PlayerStartRooms.Reset();
	PlayerStartRoom = nullptr;

	// ★★★ 敵スポーンフラグをリセット（2025-11-09）
	// 新しいフロアの生成時に敵を再スポーン可能にする
	bEnemiesSpawned = false;
	AllUnits.Reset();  // 既存のユニット配列もクリア

	UE_LOG(LogTemp, Warning, TEXT("BuildUnits: Received %d rooms. (bEnemiesSpawned reset to false)"), Rooms.Num());

	// BigEnoughRooms: RoomArea > TeamSize[1] * 1
	for (AAABB* R : Rooms)
	{
		if (!R) continue;
		if (RoomArea(R) > TeamSize[1] * 1)
		{
			BigEnoughRooms.Add(R);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("BuildUnits: Found %d BigEnoughRooms (needs to be > 0)."), BigEnoughRooms.Num());

	// PlayerStartRooms: RoomArea > TeamSize[0] * 2
	for (AAABB* R : BigEnoughRooms)
	{
		if (RoomArea(R) > TeamSize[0] * 2)
		{
			PlayerStartRooms.Add(R);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("BuildUnits: Found %d PlayerStartRooms (needs to be > 0)."), PlayerStartRooms.Num());

	// ランダムでプレイヤー開始部屋を決定
	if (PlayerStartRooms.Num() == 0)
	{
		const FString Message = TEXT("UnitManager: No PlayerStartRooms found.");
		UE_LOG(LogTemp, Warning, TEXT("%s"), *Message);
		LogToCSV(TEXT("UnitManager"), Message);
		return;
	}

	const int32 PlayerRoomStartIndex = FMath::RandRange(0, PlayerStartRooms.Num() - 1);
	PlayerStartRoom = PlayerStartRooms[PlayerRoomStartIndex];

	// ★★★ 追加：ログ ★★★
	if (PlayerStartRoom)
	{
		const FString Message = FString::Printf(TEXT("BuildUnits - Selected PlayerStartRoom at Location: %s"), *PlayerStartRoom->GetActorLocation().ToString());
		UE_LOG(LogTemp, Log, TEXT("UnitManager::%s"), *Message);
		LogToCSV(TEXT("UnitManager"), Message);
	}
	else
	{
		const FString Message = TEXT("BuildUnits - Failed to select a PlayerStartRoom!");
		UE_LOG(LogTemp, Warning, TEXT("UnitManager::%s"), *Message);
		LogToCSV(TEXT("UnitManager"), Message);
	}

	// BigEnoughRooms からプレイヤー部屋を除外
	BigEnoughRooms.Remove(PlayerStartRoom);
}

// ========================= Trace: OnTBSCharacterPossessed_Event =========================
void AUnitManager::OnTBSCharacterPossessed(AUnitBase* ControlledPawnAsTBS_PlayerPawn)
{
	// ★★★ 追加：ログ ★★★
	const FString RoomLocationStr = PlayerStartRoom ? PlayerStartRoom->GetActorLocation().ToString() : TEXT("nullptr");
	const FString Message1 = FString::Printf(TEXT("OnTBSCharacterPossessed - Event received. PlayerStartRoom is at: %s"), *RoomLocationStr);
	UE_LOG(LogTemp, Log, TEXT("UnitManager::%s"), *Message1);
	LogToCSV(TEXT("UnitManager"), Message1);

	if (!PlayerStartRoom || !ControlledPawnAsTBS_PlayerPawn || !PathFinder)
	{
		const FString Message2 = TEXT("OnTBSCharacterPossessed missing refs");
		UE_LOG(LogTemp, Warning, TEXT("UnitManager::%s"), *Message2);
		LogToCSV(TEXT("UnitManager"), Message2);
		return;
	}

	// プレイヤーの開始位置へワープ（部屋の中心）
	const FTransform T(FRotator::ZeroRotator, PlayerStartRoom->GetActorLocation());
	FHitResult SweepHit;
	ControlledPawnAsTBS_PlayerPawn->K2_SetActorTransform(T, false, SweepHit, false);

	// プレイヤー分のスポーン位置を算出（TeamSize[0]）
	const TArray<FVector> PlayerSpawns = SpawnLocations(PlayerStartRoom, TeamSize[0]);

	// ひとまず "最初の 1 体" をプレイヤー駒として配置（BP は同一 Pawn をループで動かしてているためここは保守的に）
	if (PlayerSpawns.Num() > 0)
	{
		// ★★★ 追加：ログ ★★★
		const FString Message3 = FString::Printf(TEXT("OnTBSCharacterPossessed - Final Spawn Location: %s"), *PlayerSpawns[0].ToString());
		UE_LOG(LogTemp, Log, TEXT("UnitManager::%s"), *Message3);
		LogToCSV(TEXT("UnitManager"), Message3);

		FHitResult LocationHit;
		ControlledPawnAsTBS_PlayerPawn->K2_SetActorLocation(PlayerSpawns[0] + FVector(0,0,50), false, LocationHit, false);
		ControlledPawnAsTBS_PlayerPawn->StatBlock = DefaultStatBlock; // BP: Set StatBlock = Default
		ControlledPawnAsTBS_PlayerPawn->SetStatVars();                // BP: SetStatVars()
		AllUnits.Add(ControlledPawnAsTBS_PlayerPawn);
		ControlledPawnAsTBS_PlayerPawn->Team = 0;

		UnitManager_Private::OccupyInitialCell(GetWorld(), PathFinder, ControlledPawnAsTBS_PlayerPawn);
	}

	// Controller の PathFinder 参照を注入
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		if (APlayerControllerBase* TBSPC = Cast<APlayerControllerBase>(PC))
		{
			TBSPC->PathFinder = PathFinder;
		}
	}

	const int32 SpawnedEnemies = SpawnEnemyUnits();
	UE_LOG(LogTemp, Log, TEXT("UnitManager::OnTBSCharacterPossessed spawned %d enemies"), SpawnedEnemies);
}

// ========================= Trace: RoomArea(InputAABB) =========================
int32 AUnitManager::RoomArea(AAABB* InputAABB) const
{
	if (!IsValid(InputAABB)) return 0;
	// BP: FFloor( ((BoxExtent*2)/100).X * ((BoxExtent*2)/100).Y )
	const FVector Half = GetRoomHalfExtents(InputAABB);
	const FVector SizeWorld = Half * 2.0f;
	const FVector SizeTiles = SizeWorld / 100.0f; // 1 タイル=100cm
	return FMath::FloorToInt(SizeTiles.X * SizeTiles.Y);
}

// ========================= Trace: SpawnLocations(InputRoom, NumberOfSpawns) =========================
TArray<FVector> AUnitManager::SpawnLocations(AAABB* InputRoom, int32 NumberOfSpawns)
{
	TArray<FVector> Out;
	if (!IsValid(InputRoom) || NumberOfSpawns <= 0 || !PathFinder) return Out;

	const FVector Center = InputRoom->GetActorLocation();
	const FVector Half   = GetRoomHalfExtents(InputRoom);

	// ★★★ 占有情報チェック用のサブシステム取得 ★★★
	UWorld* World = GetWorld();
	UGridOccupancySubsystem* Occupancy = World ? World->GetSubsystem<UGridOccupancySubsystem>() : nullptr;

	UE_LOG(LogTemp, Warning, TEXT("SpawnLocations: Searching for %d spawns in room at %s with half-extents %s"), NumberOfSpawns, *Center.ToString(), *Half.ToString());

	for (int32 n = 0; n < NumberOfSpawns; ++n)
	{
		// 16 回まで試行して被り/進入不可を避ける
		for (int32 t = 0; t <= 15; ++t)
		{
			const int32 RX = FMath::RandRange(
				FMath::FloorToInt((Center.X - Half.X) / 100.f),
				FMath::FloorToInt((Center.X + Half.X) / 100.f) - 1);
			const int32 RY = FMath::RandRange(
				FMath::FloorToInt((Center.Y - Half.Y) / 100.f),
				FMath::FloorToInt((Center.Y + Half.Y) / 100.f) - 1);

			const FVector Candidate( RX * 100.f + 50.f, RY * 100.f + 50.f, Center.Z );

			if (!Out.Contains(Candidate))
			{
				// ★★★ 1. グリッドステータスをチェック（通行可能か） ★★★
				const int32 Status = PathFinder->ReturnGridStatus(Candidate);

				// ★★★ 2. 占有状態をチェック（既に他のユニットがいないか） ★★★
				const FIntPoint Cell = PathFinder->WorldToGrid(Candidate);
				const bool bIsOccupied = Occupancy ? Occupancy->IsCellOccupied(Cell) : false;

				UE_LOG(LogTemp, Log, TEXT("SpawnLocations: Attempt %d for spawn %d. Candidate: %s, Cell: (%d,%d), Status: %d, Occupied: %d"),
					t, n, *Candidate.ToString(), Cell.X, Cell.Y, Status, bIsOccupied ? 1 : 0);

				// ★★★ 通行可能かつ未占有の場合のみスポーン位置として採用 ★★★
				if (Status >= 0 && !bIsOccupied)
				{
					Out.Add(Candidate);
					UE_LOG(LogTemp, Warning, TEXT("SpawnLocations: Found valid spawn at %s (Cell: %d,%d)"), *Candidate.ToString(), Cell.X, Cell.Y);
					break; // 次のユニットへ
				}
			}
		}
	}

	return Out;
}

// ======= AAABB から "半径" を取るヘルパ =======
FVector AUnitManager::GetRoomHalfExtents(AAABB* Room) const
{
	return Room ? Room->GetBoxExtent() : FVector::ZeroVector;
}

// ★★★ CSVログ出力ヘルパ ★★★
void AUnitManager::LogToCSV(const FString& Category, const FString& Message)
{
	if (DebugObserverCSV)
	{
		DebugObserverCSV->LogMessage(Category, Message);
	}
}
