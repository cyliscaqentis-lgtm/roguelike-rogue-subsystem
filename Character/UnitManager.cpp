#include "UnitManager.h"

#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

// ===== あなたのプロジェクト固有のヘッダ =====
#include "Grid/AABB.h"
#include "Character/UnitBase.h"
#include "Grid/GridPathfindingLibrary.h"
#include "Player/PlayerControllerBase.h"

AUnitManager::AUnitManager()
{
	PrimaryActorTick.bCanEverTick = false;

	// TeamSize を 2 要素にしておく（[0]=プレイヤー, [1]=敵）
	EnsureTeamSize2();
}

void AUnitManager::EnsureTeamSize2()
{
	if (TeamSize.Num() < 2)
	{
		TeamSize.SetNum(2);
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

	// TeamSize[1] = SelectInt(A=floor(MapSize.X), B=32, pickA=(floor(MapSize.X) <= 32))
	const int32 MapX = FMath::FloorToInt(MapSize.X);
	TeamSize[1] = (MapX <= 32) ? MapX : 32;

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

// ========================= Trace: GetUnitStatBlock =========================
void AUnitManager::GetUnitStatBlock(int32 Index)
{
	if (!StatBlock.IsValidIndex(Index)) return;
	const FUnitStatBlock& B = StatBlock[Index];

	Strength=B.Strength; Intelligence=B.Intelligence; Dexterity=B.Dexterity; Constitution=B.Constitution;
	Health=B.MaxHealth; CurrentHealth=B.CurrentHealth; Power=B.MaxPower; CurrentPower=B.CurrentPower;
	Speed=B.MaxSpeed; CurrentSpeed=B.CurrentSpeed;
	MaxSightRange=B.MaxSightRange; CurrentSightRange=B.CurrentSightRange;

	CanAct=B.bCanAct; HasActed=B.bHasActed; IsDisabled=B.bIsDisabled; CanMove=B.bCanMove; HasMoved=B.bHasMoved; IsRooted=B.bIsRooted;

	Team=B.Team; MovementRange=B.RangePerMove; NumberOfMoves=B.NumberOfMoves; CurrentMovementRange=B.CurrentTotalMovementRnage;

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

	Tmp.bCanAct=CanAct; Tmp.bHasActed=HasActed; Tmp.bIsDisabled=IsDisabled; Tmp.bCanMove=CanMove; Tmp.bHasMoved=HasMoved; Tmp.bIsRooted=IsRooted;

	Tmp.Team=Team; Tmp.RangePerMove=MovementRange; Tmp.NumberOfMoves=NumberOfMoves; Tmp.CurrentTotalMovementRnage=CurrentMovementRange;

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

	NumberOfMoves = Constitution / 4;
	MovementRange = Dexterity / 4;
	CurrentMovementRange = MovementRange * NumberOfMoves;

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

	// BigEnoughRooms: RoomArea > TeamSize[1] * 1
	for (AAABB* R : Rooms)
	{
		if (!R) continue;
		if (RoomArea(R) > TeamSize[1] * 1)
		{
			BigEnoughRooms.Add(R);
		}
	}

	// PlayerStartRooms: RoomArea > TeamSize[0] * 2
	for (AAABB* R : BigEnoughRooms)
	{
		if (RoomArea(R) > TeamSize[0] * 2)
		{
			PlayerStartRooms.Add(R);
		}
	}

	// ランダムでプレイヤー開始部屋を決定
	if (PlayerStartRooms.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("UnitManager: No PlayerStartRooms found."));
		return;
	}

	const int32 PlayerRoomStartIndex = FMath::RandRange(0, PlayerStartRooms.Num() - 1);
	PlayerStartRoom = PlayerStartRooms[PlayerRoomStartIndex];

	// BigEnoughRooms からプレイヤー部屋を除外
	BigEnoughRooms.Remove(PlayerStartRoom);
}

// ========================= Trace: OnTBSCharacterPossessed_Event =========================
void AUnitManager::OnTBSCharacterPossessed(AUnitBase* ControlledPawnAsTBS_PlayerPawn)
{
	if (!PlayerStartRoom || !ControlledPawnAsTBS_PlayerPawn || !PathFinder)
	{
		UE_LOG(LogTemp, Warning, TEXT("UnitManager::OnTBSCharacterPossessed missing refs"));
		return;
	}

	// プレイヤーの開始位置へワープ（部屋の中心）
	const FTransform T(FRotator::ZeroRotator, PlayerStartRoom->GetActorLocation());
	FHitResult SweepHit;
	ControlledPawnAsTBS_PlayerPawn->K2_SetActorTransform(T, false, SweepHit, false);

	// プレイヤー分のスポーン位置を算出（TeamSize[0]）
	const TArray<FVector> PlayerSpawns = SpawnLocations(PlayerStartRoom, TeamSize[0]);

	// ひとまず “最初の 1 体” をプレイヤー駒として配置（BP は同一 Pawn をループで動かしてているためここは保守的に）
	if (PlayerSpawns.Num() > 0)
	{
		FHitResult LocationHit;
		ControlledPawnAsTBS_PlayerPawn->K2_SetActorLocation(PlayerSpawns[0] + FVector(0,0,50), false, LocationHit, false);
		ControlledPawnAsTBS_PlayerPawn->StatBlock = DefaultStatBlock; // BP: Set StatBlock = Default
		ControlledPawnAsTBS_PlayerPawn->SetStatVars();                // BP: SetStatVars()
		AllUnits.Add(ControlledPawnAsTBS_PlayerPawn);
		ControlledPawnAsTBS_PlayerPawn->Team = 0;
		PathFinder->GridChangeVector(ControlledPawnAsTBS_PlayerPawn->K2_GetActorLocation(), -1);
	}

	// Controller の PathFinder 参照を注入
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		if (APlayerControllerBase* TBSPC = Cast<APlayerControllerBase>(PC))
		{
			TBSPC->PathFinder = PathFinder;
		}
	}

	// Trace: Set TeamIndex=1 → GenerateBasicEnemyTeam()
	GenerateBasicEnemyTeam();

	// ===== 敵ユニットを部屋ごとにスポーン =====
	const int32 RoomsToUse = FMath::Max(1, (TeamSize[1] + (TeamSize[0] - 1)) / FMath::Max(1, UnitsPerRoom));

	for (int32 i = TeamSize[0]; i < RoomsToUse + TeamSize[0]; ++i)
	{
		if (BigEnoughRooms.Num() == 0) break;

		int32 Counter = FMath::RandRange(0, BigEnoughRooms.Num() - 1);

		// プレイヤー開始部屋を引いた場合は入れ替え／再抽選の簡易処理
		if (BigEnoughRooms[Counter] == PlayerStartRoom)
		{
			BigEnoughRooms.RemoveAt(Counter);
			if (BigEnoughRooms.Num() == 0) break;
			Counter = (Counter == 0) ? 0 : Counter - 1;
		}

		AAABB* EnemyRoom = BigEnoughRooms.IsValidIndex(Counter) ? BigEnoughRooms[Counter] : nullptr;
		if (!IsValid(EnemyRoom))
		{
			UE_LOG(LogTemp, Warning, TEXT("BigEnoughRoomNotValid"));
			continue;
		}

		// この部屋のスポーン位置を決定し、使い切りとする
		const TArray<FVector> EnemySpawns = SpawnLocations(EnemyRoom, UnitsPerRoom);
		BigEnoughRooms.RemoveAt(Counter);

		for (const FVector& Loc : EnemySpawns)
		{
			if (!EnemyUnitClass) continue;

			FTransform SpawnTM(FRotator::ZeroRotator, Loc + FVector(0,0,30), FVector::OneVector);

			FActorSpawnParameters P;
			P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

			AUnitBase* Enemy = GetWorld()->SpawnActor<AUnitBase>(EnemyUnitClass, SpawnTM, P);
			if (!Enemy) continue;

			if (Enemy->HasAuthority())
			{
				// LyraTeamBlueprintLibrary::ChangeTeamForActor(Enemy, /*NewTeamId*/ 2); // 利用可能なら
				Enemy->StatBlock = DefaultStatBlock;
				Enemy->SetStatVars();
				Enemy->SetActorHiddenInGame(false);
				AllUnits.Add(Enemy);
				Enemy->Team = 1;
				PathFinder->GridChangeVector(Enemy->K2_GetActorLocation(), -1);
			}
			else
			{
				if (GEngine) GEngine->AddOnScreenDebugMessage(INDEX_NONE, 2.f, FColor::Yellow, TEXT("Hello"));
			}
		}

		// BP 最後の ChangeTeamForActor(プレイヤーPawn, 1) 相当（必要なら）
		// LyraTeamBlueprintLibrary::ChangeTeamForActor(ControlledPawnAsTBS_PlayerPawn, 1);
	}

	// BP: PrimeSound(None) は無処理で OK
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

			const FVector Candidate( RX * 100.f + 50.f, RY * 100.f + 50.f, 0.f );

			if (!Out.Contains(Candidate))
			{
				// BP: PathFinder.mGrid[...] == -1 で “不可” のニュアンス → ここでは “>=0 なら可” と解釈
				const int32 Status = PathFinder->ReturnGridStatus(Candidate);
				if (Status >= 0)
				{
					Out.Add(Candidate);
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
