// ============================================================================
// UnitMovementComponent.cpp
// ユニット移動処理Component実装
// UnitBaseから分離（2025-11-09）
// ============================================================================

// CodeRevision: INC-2025-00030-R2 (Migrate to UGridPathfindingSubsystem) (2025-11-17 00:40)
#include "Character/UnitMovementComponent.h"
#include "Character/UnitStatBlock.h"
#include "Character/UnitBase.h"
#include "Grid/GridPathfindingSubsystem.h"
#include "Grid/GridOccupancySubsystem.h"
#include "Utility/PathFinderUtils.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY(LogUnitMovement);

//------------------------------------------------------------------------------
// Component Lifecycle
//------------------------------------------------------------------------------

UUnitMovementComponent::UUnitMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false; // 必要な時だけTick

	MoveSpeed = 300.0f;
	ArrivalThreshold = 10.0f;
	bUseSmoothMovement = true;
	InterpSpeed = 5.0f;
	PixelsPerSec = MoveSpeed;
}

void UUnitMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bIsMoving)
	{
		UpdateMovement(DeltaTime);
	}
	else
	{
		// Ensure anim-facing variables decay to idle when not moving.
		MoveDirectionAnim = FVector::ZeroVector;
		MoveSpeedAnim = 0.0f;
	}
}

//------------------------------------------------------------------------------
// Movement Control
//------------------------------------------------------------------------------

void UUnitMovementComponent::MoveUnit(const TArray<FVector>& Path)
{
	if (Path.Num() == 0)
	{
		UE_LOG(LogUnitMovement, Warning, TEXT("[UnitMovementComponent] MoveUnit called with empty path"));
		return;
	}

	CurrentPath = Path;
	GridUpdateRetryCount = 0;
	CurrentPathIndex = 0;
	bIsMoving = true;

	// Tickを有効化
	SetComponentTickEnabled(true);

	// イベント配信
	AUnitBase* OwnerUnit = GetOwnerUnit();
	OnMoveStarted.Broadcast(OwnerUnit);

	UE_LOG(LogUnitMovement, Log, TEXT("[UnitMovementComponent] Movement started with %d waypoints"), Path.Num());
}

// CodeRevision: INC-2025-00030-R1 (Migrate to UGridPathfindingSubsystem) (2025-11-16 23:55)
void UUnitMovementComponent::MoveUnitAlongGridPath(const TArray<FIntPoint>& GridPath)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogUnitMovement, Error, TEXT("[UnitMovementComponent] World not available"));
		return;
	}

	UGridPathfindingSubsystem* PathFinder = World->GetSubsystem<UGridPathfindingSubsystem>();
	if (!PathFinder)
	{
		UE_LOG(LogUnitMovement, Error, TEXT("[UnitMovementComponent] UGridPathfindingSubsystem not available"));
		return;
	}

	if (GridPath.Num() == 0)
	{
		UE_LOG(LogUnitMovement, Warning, TEXT("[UnitMovementComponent] MoveUnitAlongGridPath called with empty path"));
		return;
	}

	// Convert grid path to world coordinates
	TArray<FVector> WorldPath;
	WorldPath.Reserve(GridPath.Num());

	for (const FIntPoint& GridCell : GridPath)
	{
		const FVector WorldPos = PathFinder->GridToWorld(GridCell);
		WorldPath.Add(WorldPos);
	}

	MoveUnit(WorldPath);
}

void UUnitMovementComponent::CancelMovement()
{
	if (!bIsMoving)
	{
		return;
	}

	const FVector LastPosition = GetOwner()->GetActorLocation();

	bIsMoving = false;
	CurrentPath.Empty();
	CurrentPathIndex = 0;

	// Tickを無効化
	SetComponentTickEnabled(false);

	// イベント配信
	AUnitBase* OwnerUnit = GetOwnerUnit();
	OnMoveCancelled.Broadcast(OwnerUnit, LastPosition);
    ResetAnimMotion();

	UE_LOG(LogUnitMovement, Log, TEXT("[UnitMovementComponent] Movement cancelled at %.2f, %.2f, %.2f"),
		LastPosition.X, LastPosition.Y, LastPosition.Z);
}

void UUnitMovementComponent::SetMoveSpeed(float NewSpeed)
{
	MoveSpeed = FMath::Max(0.0f, NewSpeed);
	PixelsPerSec = MoveSpeed;
	UE_LOG(LogUnitMovement, Log, TEXT("[UnitMovementComponent] Move speed set to %.2f"), MoveSpeed);
}

void UUnitMovementComponent::UpdateSpeedFromStats(const FUnitStatBlock& StatBlock)
{
	const float StatSpeed =
		(StatBlock.CurrentSpeed > KINDA_SMALL_NUMBER) ? StatBlock.CurrentSpeed :
		(StatBlock.MaxSpeed > KINDA_SMALL_NUMBER ? StatBlock.MaxSpeed : PixelsPerSec);

	const float TargetSpeed = FMath::Clamp(StatSpeed, MinPixelsPerSec, MaxPixelsPerSec);
	if (!FMath::IsNearlyEqual(TargetSpeed, PixelsPerSec))
	{
		UE_LOG(LogUnitMovement, Display,
			TEXT("[UnitMovementComponent] UpdateSpeedFromStats: PixelsPerSec %.1f -> %.1f (Stat=%.1f)"),
			PixelsPerSec, TargetSpeed, StatSpeed);
	}

	PixelsPerSec = TargetSpeed;
	MoveSpeed = PixelsPerSec;
}

//------------------------------------------------------------------------------
// Internal Movement
//------------------------------------------------------------------------------

void UUnitMovementComponent::UpdateMovement(float DeltaTime)
{
	if (CurrentPathIndex >= CurrentPath.Num())
	{
		FinishMovement();
		return;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		FinishMovement();
		return;
	}

	const FVector CurrentLocation = Owner->GetActorLocation();
	const FVector TargetLocation = CurrentPath[CurrentPathIndex];
	const FVector Direction = (TargetLocation - CurrentLocation).GetSafeNormal();
    MoveDirectionAnim = FVector(Direction.X, Direction.Y, 0.0f).GetSafeNormal();
    MoveSpeedAnim = PixelsPerSec;
	const float DistanceToTarget = FVector::Dist(CurrentLocation, TargetLocation);

	// 到達判定
	if (DistanceToTarget <= ArrivalThreshold)
	{
		MoveToNextWaypoint();
		return;
	}

	// 移動方向への回転（滑らかに補間）
	if (!Direction.IsNearlyZero())
	{
		FVector DirectionNoZ = Direction;
		DirectionNoZ.Z = 0.0f; // 垂直成分を無視

		if (!DirectionNoZ.IsNearlyZero())
		{
			const FRotator TargetRotation = DirectionNoZ.Rotation();
			const FRotator CurrentRotation = Owner->GetActorRotation();
			const FRotator NewRotation = FMath::RInterpTo(CurrentRotation, TargetRotation, DeltaTime, 10.0f);
			Owner->SetActorRotation(NewRotation);
		}
	}

	// 移動計算
	FVector NewLocation;
	if (bUseSmoothMovement)
	{
		// スムーズな補間移動
		NewLocation = FMath::VInterpConstantTo(CurrentLocation, TargetLocation, DeltaTime, PixelsPerSec);
	}
	else
	{
		// 直線移動
		const float MoveDistance = PixelsPerSec * DeltaTime;
		NewLocation = CurrentLocation + Direction * FMath::Min(MoveDistance, DistanceToTarget);
	}

	Owner->SetActorLocation(NewLocation);
    MoveDirectionAnim = FVector(Direction.X, Direction.Y, 0.0f).GetSafeNormal();
    MoveSpeedAnim = PixelsPerSec;
}

void UUnitMovementComponent::MoveToNextWaypoint()
{
	CurrentPathIndex++;

	if (CurrentPathIndex >= CurrentPath.Num())
	{
		FinishMovement();
	}
}

void UUnitMovementComponent::FinishMovement()
{
	// CodeRevision: INC-2025-1129-R1 (Prevent movement hang by capping grid occupancy retries) (2025-11-27 16:00)
	bIsMoving = false;
	SetComponentTickEnabled(false); // Tickは先に止める

	AUnitBase* OwnerUnit = GetOwnerUnit();

	if (OwnerUnit && CurrentPath.Num() > 0)
	{
		const FVector FinalDestination = CurrentPath.Last();

		// 向きと位置のスナップは現行どおり
		FVector MoveDirection = FinalDestination - OwnerUnit->GetActorLocation();
		MoveDirection.Z = 0.0f;
		if (!MoveDirection.IsNearlyZero())
		{
			const float TargetYaw = MoveDirection.Rotation().Yaw;
			FRotator NewRotation = OwnerUnit->GetActorRotation();
			NewRotation.Yaw = TargetYaw;
			OwnerUnit->SetActorRotation(NewRotation);
		}
		OwnerUnit->SetActorLocation(FinalDestination, false, nullptr, ETeleportType::None);

		if (UWorld* World = GetWorld())
		{
			if (UGridOccupancySubsystem* Occupancy = World->GetSubsystem<UGridOccupancySubsystem>())
			{
				if (UGridPathfindingSubsystem* PathFinder = World->GetSubsystem<UGridPathfindingSubsystem>())
				{
					const FIntPoint FinalCell = PathFinder->WorldToGrid(FinalDestination);

					if (!Occupancy->UpdateActorCell(OwnerUnit, FinalCell))
					{
						GridUpdateRetryCount++;

						if (GridUpdateRetryCount <= MaxGridUpdateRetries)
						{
							UE_LOG(LogUnitMovement, Warning,
								TEXT("[UnitMovementComponent] UpdateActorCell FAILED (Retry=%d/%d) for %s at (%d,%d) - scheduling retry"),
								GridUpdateRetryCount, MaxGridUpdateRetries,
								*GetNameSafe(OwnerUnit), FinalCell.X, FinalCell.Y);

							// CodeRevision: INC-2025-1122-PERF-R3 (Use NextTick instead of timed delay for instant retry)
							// Two-Phase Commit requires waiting for the previous occupant to commit.
							// Using NextTick instead of 0.02s delay reduces retry latency to ~1 frame.
							World->GetTimerManager().SetTimerForNextTick(this, &UUnitMovementComponent::FinishMovement);

							return;
						}
						else
						{
						UE_LOG(LogUnitMovement, Error,
							TEXT("[UnitMovementComponent] UpdateActorCell FAILED after %d retries. Forcing movement completion to avoid game hang. Actor=%s FinalCell=(%d,%d)"),
							GridUpdateRetryCount,
							*GetNameSafe(OwnerUnit),
							FinalCell.X, FinalCell.Y);
							// fall through and finish movement anyway
						}
					}
					else
					{
						GridUpdateRetryCount = 0;
						World->GetTimerManager().ClearTimer(GridUpdateRetryHandle);

						UE_LOG(LogUnitMovement, Verbose,
							TEXT("[UnitMovementComponent] GridOccupancy updated: Actor=%s Cell=(%d,%d)"),
							*GetNameSafe(OwnerUnit), FinalCell.X, FinalCell.Y);
					}
				}
			}
		}
	}

	CurrentPath.Empty();
	CurrentPathIndex = 0;

	OnMoveFinished.Broadcast(OwnerUnit);
    MoveDirectionAnim = FVector::ZeroVector;
    MoveSpeedAnim = 0.0f;

	UE_LOG(LogUnitMovement, Log,
		TEXT("[UnitMovementComponent] Movement finished and snapped. (RetryCount=%d)"),
		GridUpdateRetryCount);
}
AUnitBase* UUnitMovementComponent::GetOwnerUnit() const
{
	return Cast<AUnitBase>(GetOwner());
}

void UUnitMovementComponent::ResetAnimMotion()
{
	MoveDirectionAnim = FVector::ZeroVector;
	MoveSpeedAnim = 0.0f;
}
