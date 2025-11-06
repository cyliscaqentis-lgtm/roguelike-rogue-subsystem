#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Character/UnitStatBlock.h"
#include "UnitManager.generated.h"

// ===== 前方宣言 =====
class AAABB;
class AUnitBase;
class AGridPathfindingLibrary;
class APlayerControllerBase;

UCLASS()
class LYRAGAME_API AUnitManager : public AActor
{
	GENERATED_BODY()

public:
	AUnitManager();

	// ===== Trace: Event BeginPlay =====
	virtual void BeginPlay() override;

	// ===== 外部（GameMode）から渡される依存関係 =====
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Init")
	FVector MapSize = FVector::ZeroVector; // タイル数（X,Y）想定

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Init")
	TObjectPtr<AGridPathfindingLibrary> PathFinder = nullptr;

	// ===== 設定値（BP のデフォルト相当）=====
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Units")
	int32 UnitsPerRoom = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Units")
	int32 DefaultPoints = 6; // 敵の初期振り分けポイント

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Units")
	FUnitStatBlock DefaultStatBlock;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Units")
	TSubclassOf<AUnitBase> EnemyUnitClass;

	// ===== ランタイム配列 =====
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Units")
	TArray<TObjectPtr<AUnitBase>> AllUnits;

	/** StatBlock配列（BP互換） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Units")
	TArray<FUnitStatBlock> StatBlock;

	// TeamSize[0]=プレイヤー人数, TeamSize[1]=敵人数（BP がこの配列で扱うため踏襲）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Units")
	TArray<int32> TeamSize; // 最低2要素

	// ===== BP トレース対応関数 =====
	// Trace: InitUnitArrays
	UFUNCTION(BlueprintCallable) void InitUnitArrays();

	// Trace: BuildUnits(RoomsIn)
	UFUNCTION(BlueprintCallable) void BuildUnits(const TArray<AAABB*>& RoomsIn);

	// Trace: GenerateBasicEnemyTeam
	UFUNCTION(BlueprintCallable) void GenerateBasicEnemyTeam();

	// Trace: OnTBSCharacterPossessed_Event
	UFUNCTION(BlueprintCallable) void OnTBSCharacterPossessed(AUnitBase* ControlledPawn);

	// Trace: GetUnitStatBlock / SetUnitStatBlock / CalculateDerivedValues / SetStartingHealthAndPower
	UFUNCTION(BlueprintCallable) void GetUnitStatBlock(int32 Index);
	UFUNCTION(BlueprintCallable) void SetUnitStatBlock(int32 Index);
	UFUNCTION(BlueprintCallable) void CalculateDerivedValues();
	UFUNCTION(BlueprintCallable) void SetStartingHealthAndPower();

	// Trace: SpawnLocations / RoomArea
	UFUNCTION(BlueprintCallable) TArray<FVector> SpawnLocations(AAABB* InputRoom, int32 NumberOfSpawns);
	UFUNCTION(BlueprintCallable) int32 RoomArea(AAABB* InputAABB) const;

private:
	// 作業用 “現在編集中のステータス変数” （BP と同じ書き込み順）
	int32 Strength=0, Intelligence=0, Dexterity=0, Constitution=0;
	float Health=0, CurrentHealth=0, Power=0, CurrentPower=0, Speed=0, CurrentSpeed=0;
	float MaxSightRange=0, CurrentSightRange=0;
	bool  CanAct=true, HasActed=false, IsDisabled=false, CanMove=true, HasMoved=false, IsRooted=false;
	int32 Team=0, MovementRange=0, NumberOfMoves=0, CurrentMovementRange=0;
	int32 MeleeBaseAttack=0, RangedBaseAttack=0, MagicBaseAttack=0;
	float MeleeBaseDamage=0, RangedBaseDamage=0, MagicBaseDamage=0;
	float BaseDamageAbsorb=0, BaseDamageAvoidance=0, BaseMagicResist=0, BaseMagicPenetration=0;
	float CurrentDamageAbsorb=0, CurrentDamageAvoidance=0, CurrentMagicResist=0, CurrentMagicPenetration=0;

	// ルーム管理
	UPROPERTY() TArray<TObjectPtr<AAABB>> Rooms;
	UPROPERTY() TArray<TObjectPtr<AAABB>> BigEnoughRooms;
	UPROPERTY() TArray<TObjectPtr<AAABB>> PlayerStartRooms;
	UPROPERTY() TObjectPtr<AAABB> PlayerStartRoom = nullptr;

	// ヘルパ
	void EnsureTeamSize2();
	FVector GetRoomHalfExtents(AAABB* Room) const;
};
