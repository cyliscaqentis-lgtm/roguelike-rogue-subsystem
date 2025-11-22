// ============================================================================
// UnitMovementComponent.h
// ユニット移動処理Component
// UnitBaseから分離（2025-11-09）
// ============================================================================

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UnitMovementComponent.generated.h"

// Log category
DECLARE_LOG_CATEGORY_EXTERN(LogUnitMovement, Log, All);

class AUnitBase;
class UGridPathfindingSubsystem;
struct FUnitStatBlock;

/** 移動完了デリゲート */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnUnitMoveFinished, AUnitBase*, Unit);

/** 移動開始デリゲート */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnUnitMoveStarted, AUnitBase*, Unit);

/** 移動中断デリゲート */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnUnitMoveCancelled, AUnitBase*, Unit, FVector, LastPosition);

/**
 * ユニットの移動処理を担当するComponent
 * 責務: パス追従、移動アニメーション、移動イベント
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class LYRAGAME_API UUnitMovementComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// ========== Component Lifecycle ==========
	UUnitMovementComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ========== Movement Control ==========

	/**
	 * ユニットを移動
	 * @param Path 移動パス（ワールド座標）
	 */
	UFUNCTION(BlueprintCallable, Category = "Unit|Movement")
	void MoveUnit(const TArray<FVector>& Path);

	/**
	 * Move unit along grid path
	 * CodeRevision: INC-2025-00030-R1 (Migrate to UGridPathfindingSubsystem) (2025-11-16 23:55)
	 * @param GridPath Grid path to follow
	 */
	UFUNCTION(BlueprintCallable, Category = "Unit|Movement")
	void MoveUnitAlongGridPath(const TArray<FIntPoint>& GridPath);

	/**
	 * 移動を中断
	 */
	UFUNCTION(BlueprintCallable, Category = "Unit|Movement")
	void CancelMovement();

	/**
	 * 移動が完了したか
	 */
	UFUNCTION(BlueprintPure, Category = "Unit|Movement")
	bool IsMovementFinished() const { return !bIsMoving; }

	/**
	 * 現在移動中か
	 */
	UFUNCTION(BlueprintPure, Category = "Unit|Movement")
	bool IsMoving() const { return bIsMoving; }

	/**
	 * 現在のパスインデックスを取得
	 */
	UFUNCTION(BlueprintPure, Category = "Unit|Movement")
	int32 GetCurrentPathIndex() const { return CurrentPathIndex; }

	/**
	 * 移動速度を設定
	 * @param NewSpeed 新しい速度（cm/s）
	 */
	UFUNCTION(BlueprintCallable, Category = "Unit|Movement")
	void SetMoveSpeed(float NewSpeed);

	/**
	 * 移動速度を取得
	 */
	UFUNCTION(BlueprintPure, Category = "Unit|Movement")
	float GetMoveSpeed() const { return MoveSpeed; }

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement")
	float PixelsPerSec = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement")
	float MinPixelsPerSec = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement")
	float MaxPixelsPerSec = 1200.f;

	void UpdateSpeedFromStats(const FUnitStatBlock& StatBlock);

	// ========== Events ==========

	/** 移動完了時に呼ばれる */
	UPROPERTY(BlueprintAssignable, Category = "Unit|Movement")
	FOnUnitMoveFinished OnMoveFinished;

	/** 移動開始時に呼ばれる */
	UPROPERTY(BlueprintAssignable, Category = "Unit|Movement")
	FOnUnitMoveStarted OnMoveStarted;

	/** 移動中断時に呼ばれる */
	UPROPERTY(BlueprintAssignable, Category = "Unit|Movement")
	FOnUnitMoveCancelled OnMoveCancelled;

protected:
	// ========== Movement State ==========

	/** 現在の移動パス */
	UPROPERTY(Transient)
	TArray<FVector> CurrentPath;

	/** 現在のパスインデックス */
	UPROPERTY(Transient)
	int32 CurrentPathIndex = 0;

	/** 移動中フラグ */
	UPROPERTY(Transient)
	bool bIsMoving = false;

	/** 移動速度（cm/s） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit|Movement")
	float MoveSpeed = 300.0f;

	/** 目標地点への到達判定距離 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit|Movement")
	float ArrivalThreshold = 10.0f;

    /** スムーズな移動補間を使用するか */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit|Movement")
    bool bUseSmoothMovement = true;

    /** 補間速度 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit|Movement", meta = (EditCondition = "bUseSmoothMovement"))
    float InterpSpeed = 5.0f;

    /** Occupancy 更新の最大リトライ回数 */
    static constexpr int32 MaxGridUpdateRetries = 5;

    /** Occupancy 更新の現在のリトライ回数 */
    UPROPERTY(Transient)
    int32 GridUpdateRetryCount = 0;

    /** グリッド更新再試行用タイマーハンドル */
    UPROPERTY(Transient)
    FTimerHandle GridUpdateRetryHandle;

	// ========== Internal Methods ==========

	/**
	 * 移動を更新
	 * @param DeltaTime デルタタイム
	 */
	void UpdateMovement(float DeltaTime);

	/**
	 * 次のウェイポイントへ移動
	 */
	void MoveToNextWaypoint();

	/**
	 * 移動完了処理
	 */
	void FinishMovement();

	/**
	 * オーナーUnitを取得
	 */
	AUnitBase* GetOwnerUnit() const;
};
