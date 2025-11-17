#pragma once

#include "CoreMinimal.h"
// 親クラスの実体ヘッダを必ず入れる（パスはプロジェクトに合わせて調整）
#include "GameModes/LyraGameMode.h"
#include "TBSLyraGameMode.generated.h"

// 前方宣言（ヘッダ依存を軽くするため）
struct FActorSpawnParameters;  // UHT非対象型の前方宣言
class AGridPathfindingLibrary;
class AUnitManager;
class AUnitBase;
class AAABB;
class URogueDungeonSubsystem;

// ダンジョン準備完了デリゲート（TurnManagerが購読）
// DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTurnWorldReady); // NOTE: This is now obsolete, GameTurnManager subscribes directly to the subsystem.

/** Blueprint の GetWorldVariables の戻り値に相当 */
USTRUCT(BlueprintType)
struct FWorldVariables
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) float ChanceToDropQuad = 0.f;
	UPROPERTY(BlueprintReadOnly) int32 CorridorWidth = 1;
	UPROPERTY(BlueprintReadOnly) FVector MapDimensions = FVector::ZeroVector; // タイル単位想定
	UPROPERTY(BlueprintReadOnly) int32 RoomMinSize = 2;
	UPROPERTY(BlueprintReadOnly) int32 TileSize = 100;
	UPROPERTY(BlueprintReadOnly) int32 WallThickness = 1;
};

/**
 * TBS（ターン制）用 GameMode。BPの親にできるよう Blueprintable を明示。
 */
UCLASS(BlueprintType, Blueprintable)
class LYRAGAME_API ATBSLyraGameMode : public ALyraGameMode
{
	GENERATED_BODY()

public:
	ATBSLyraGameMode();

	// C++専用ヘルパー（FActorSpawnParametersはUHT非対象のためUFUNCTION不可）
	static FActorSpawnParameters MakeAdjustDontSpawnParams();
	static FActorSpawnParameters MakeAlwaysSpawnParams();

	// TurnManager から参照されるゲッター（後方互換性のため残すが、通常はnullptrを返す）
	// PathFinderとUnitManagerはGameTurnManagerが所有するため、ここからは取得しない
	// CodeRevision: INC-2025-00032-R1 (Removed GetPathFinder() - PathFinder is now Subsystem, not Actor) (2025-01-XX XX:XX)
	FORCEINLINE AUnitManager* GetUnitManager() const { return nullptr; } // GameTurnManagerが所有
	FORCEINLINE URogueDungeonSubsystem* GetDungeonSubsystem() const { return Dgn; }

protected:
	virtual void BeginPlay() override;

	UFUNCTION(BlueprintCallable, Category="TBS|Team")
	void TeamTurnReset(int32 Team);

	UFUNCTION(BlueprintCallable, Category="TBS|Unit")
	void UnitDestroyed(FVector Location, AUnitBase* UnitToDestroy);

	UFUNCTION(BlueprintCallable, Category="TBS|World")
	FWorldVariables GetWorldVariables() const;

private:
	// DungeonSubsystem is now accessed via the GameTurnManager
	UPROPERTY()
	TObjectPtr<URogueDungeonSubsystem> Dgn = nullptr;

	void SpawnBootstrapActors();
};