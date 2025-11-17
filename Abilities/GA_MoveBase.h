// GA_MoveBase.h
#pragma once

#include "CoreMinimal.h"
#include "GA_TurnActionBase.h"
#include "GameplayTagContainer.h"
#include "GA_MoveBase.generated.h"

class UGridPathfindingSubsystem;
class AUnitBase;
class UGridOccupancySubsystem;
class AGameTurnManagerBase;

UCLASS(Abstract, Blueprintable)
class LYRAGAME_API UGA_MoveBase : public UGA_TurnActionBase
{
	GENERATED_BODY()

public:
	UGA_MoveBase(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	// GameplayAbility overrides
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData
	) override;

	virtual bool CanActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr
	) const override;

	// ★ override は付けない（エラー回避）
	virtual void ActivateAbilityFromEvent(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* EventData
	);

	virtual bool ShouldRespondToEvent(
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayEventData* Payload
	) const;

	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled
	) override;

	virtual void CancelAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateCancelAbility
	) override;

protected:
	virtual void SendCompletionEvent(bool bTimedOut = false) override;

	// 計測
	double AbilityStartTime = 0.0;

	// 移動パラメータ
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Move")
	float Speed = 600.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Move")
	float SpeedBuff = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Move")
	float SpeedDebuff = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Move")
	float GridSize = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Move|Animation")
	float SkipAnimIfUnderDistance = 0.0f;

	UPROPERTY(EditDefaultsOnly, Category = "GAS|Tags")
	FGameplayTag StateMovingTag = FGameplayTag::RequestGameplayTag(TEXT("State.Moving"));

	// 状態
	UPROPERTY(BlueprintReadOnly, Category = "Move|State")
	FVector Direction = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Move|State")
	FVector2D MoveDir2D = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Move|State")
	FVector FirstLoc = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Move|State")
	FVector NextTileStep = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Move|State")
	float DesiredYaw = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Move|State")
	float CurrentSpeed = 0.0f;

	int32 CompletedTurnIdForEvent = INDEX_NONE;

	// ロジック系 C++ 関数

	// EventData から方向／セルを抽出（セルは Z=-1 で表現）
	bool ExtractDirectionFromEventData(
		const FGameplayEventData* EventData,
		FVector& OutDirection
	);

	FVector2D QuantizeToGridDirection(const FVector& InDirection);

	bool IsTileWalkable(const FVector& TilePosition, AUnitBase* Self = nullptr);
	void UpdateGridState(const FVector& Position, int32 Value);
	bool IsTileWalkable(const FIntPoint& Cell) const;

	float RoundYawTo45Degrees(float Yaw);

	void BindMoveFinishedDelegate();

	UFUNCTION()
	void OnMoveFinished(AUnitBase* Unit);

	void StartMoveToCell(const FIntPoint& TargetCell);

	// 位置調整・デバッグ
	/**
	 * ワールド座標をグリッドセルの中心座標に変換（座標のみ返す）
	 * Note: Actorの位置は変更しない。座標変換のみを行う。
	 * For Actor movement, use GridUtils::SnapActorToGridCell() instead.
	 */
	FVector SnapToCellCenter(const FVector& WorldPos) const;
	
	/**
	 * ワールド座標をグリッドセルの中心座標に変換（Z座標を固定値に設定）
	 * Note: Actorの位置は変更しない。座標変換のみを行う。
	 * For Actor movement, use GridUtils::SnapActorToGridCell() instead.
	 */
	FVector SnapToCellCenterFixedZ(const FVector& WorldPos, float FixedZ) const;
	float ComputeFixedZ(const AUnitBase* Unit, const UGridPathfindingSubsystem* Pathfinding) const;
	FVector AlignZToGround(const FVector& WorldPos, float TraceUp = 200.0f, float TraceDown = 2000.0f) const;
	void DebugDumpAround(const FIntPoint& Center);

	// アニメーション関連（Blueprint 実装フック）
	UFUNCTION(BlueprintImplementableEvent, Category = "Move|Animation")
	void ExecuteMoveAnimation(const TArray<FVector>& Path);

	UFUNCTION(BlueprintNativeEvent, Category = "Move|Animation")
	bool ShouldSkipAnimation();
	virtual bool ShouldSkipAnimation_Implementation();

private:
	// タグキャッシュ
	UPROPERTY()
	FGameplayTag TagStateMoving;

	// サブシステム／マネージャキャッシュ
	mutable TWeakObjectPtr<AGameTurnManagerBase> CachedTurnManager;

	// PathfindingSubsystem アクセス
	UGridPathfindingSubsystem* GetGridPathfindingSubsystem() const;

	AGameTurnManagerBase* GetTurnManager() const;

	bool bMoveFinishedDelegateBound = false;

	// Barrier 連携用
	FGuid MoveActionId;
	int32 MoveTurnId = -1;
	bool bBarrierRegistered = false;

	// 移動状態キャッシュ
	FVector CachedStartLocWS = FVector::ZeroVector;
	FIntPoint CachedNextCell = FIntPoint(-1, -1);

	FGameplayAbilitySpecHandle CachedSpecHandle;
	FGameplayAbilityActorInfo CachedActorInfo;
	FGameplayAbilityActivationInfo CachedActivationInfo;
	FVector CachedFirstLoc = FVector::ZeroVector;

	// EndAbility 再入防止
	bool bIsEnding = false;
};
