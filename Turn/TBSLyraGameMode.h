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
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTurnWorldReady);

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
	FORCEINLINE AGridPathfindingLibrary* GetPathFinder() const { return nullptr; } // GameTurnManagerが所有
	FORCEINLINE AUnitManager* GetUnitManager() const { return nullptr; } // GameTurnManagerが所有
	FORCEINLINE URogueDungeonSubsystem* GetDungeonSubsystem() const { return Dgn; }

	// ダンジョン準備完了イベント（Grid完了後に一度だけ発火）
	UPROPERTY(BlueprintAssignable)
	FTurnWorldReady OnDungeonReady;

protected:
	virtual void BeginPlay() override;

	// BP からも呼べる入口（必要に応じて）
	UFUNCTION(BlueprintCallable, Category="TBS|Grid")
	void GridComplete(const TArray<int32>& Grid, const TArray<AAABB*>& Rooms);

	UFUNCTION(BlueprintCallable, Category="TBS|Team")
	void TeamTurnReset(int32 Team);

	UFUNCTION(BlueprintCallable, Category="TBS|Unit")
	void UnitDestroyed(FVector Location, AUnitBase* UnitToDestroy);

	UFUNCTION(BlueprintCallable, Category="TBS|World")
	FWorldVariables GetWorldVariables() const;

private:
	// DungeonSubsystemのみ保持（SubsystemはWorldが所有）
	UPROPERTY() TObjectPtr<URogueDungeonSubsystem> Dgn = nullptr;

	// PathFinderとUnitManagerはGameTurnManagerが所有するため、ここでは保持しない
	// 後方互換性のため、クラスプロパティは残すが使用しない
	UPROPERTY(EditDefaultsOnly, Category="TBS|Pathfinding")
	TSubclassOf<AGridPathfindingLibrary> PathfindingLibraryClass;

	UPROPERTY(EditDefaultsOnly, Category="TBS|Units")
	TSubclassOf<AUnitManager> UnitManagerClass;

	void SpawnBootstrapActors();   // PFL, UM の生成
	void BuildDungeonAsync();      // ダンジョン生成を開始（既存フローにフック）
	UFUNCTION() void HandleGridComplete(); // 生成完了で OnDungeonReady を発火
};
