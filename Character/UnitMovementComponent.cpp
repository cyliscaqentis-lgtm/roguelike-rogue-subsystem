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
#include "../ProjectDiagnostics.h"

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
}

//------------------------------------------------------------------------------
// Movement Control
//------------------------------------------------------------------------------

void UUnitMovementComponent::MoveUnit(const TArray<FVector>& Path)
{
	if (Path.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[UnitMovementComponent] MoveUnit called with empty path"));
		return;
	}

	CurrentPath = Path;
	CurrentPathIndex = 0;
	bIsMoving = true;

	// Tickを有効化
	SetComponentTickEnabled(true);

	// イベント配信
	AUnitBase* OwnerUnit = GetOwnerUnit();
	OnMoveStarted.Broadcast(OwnerUnit);

	UE_LOG(LogTemp, Log, TEXT("[UnitMovementComponent] Movement started with %d waypoints"), Path.Num());
}

// CodeRevision: INC-2025-00030-R1 (Migrate to UGridPathfindingSubsystem) (2025-11-16 23:55)
void UUnitMovementComponent::MoveUnitAlongGridPath(const TArray<FIntPoint>& GridPath)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("[UnitMovementComponent] World not available"));
		return;
	}

	UGridPathfindingSubsystem* PathFinder = World->GetSubsystem<UGridPathfindingSubsystem>();
	if (!PathFinder)
	{
		UE_LOG(LogTemp, Error, TEXT("[UnitMovementComponent] UGridPathfindingSubsystem not available"));
		return;
	}

	if (GridPath.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[UnitMovementComponent] MoveUnitAlongGridPath called with empty path"));
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

	UE_LOG(LogTemp, Log, TEXT("[UnitMovementComponent] Movement cancelled at %.2f, %.2f, %.2f"),
		LastPosition.X, LastPosition.Y, LastPosition.Z);
}

void UUnitMovementComponent::SetMoveSpeed(float NewSpeed)
{
	MoveSpeed = FMath::Max(0.0f, NewSpeed);
	PixelsPerSec = MoveSpeed;
	UE_LOG(LogTemp, Log, TEXT("[UnitMovementComponent] Move speed set to %.2f"), MoveSpeed);
}

void UUnitMovementComponent::UpdateSpeedFromStats(const FUnitStatBlock& StatBlock)
{
	const float StatSpeed =
		(StatBlock.CurrentSpeed > KINDA_SMALL_NUMBER) ? StatBlock.CurrentSpeed :
		(StatBlock.MaxSpeed > KINDA_SMALL_NUMBER ? StatBlock.MaxSpeed : PixelsPerSec);

	const float TargetSpeed = FMath::Clamp(StatSpeed, MinPixelsPerSec, MaxPixelsPerSec);
	if (!FMath::IsNearlyEqual(TargetSpeed, PixelsPerSec))
	{
		UE_LOG(LogTemp, Display,
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
	const float DistanceToTarget = FVector::Dist(CurrentLocation, TargetLocation);

	// 到達判定
	if (DistanceToTarget <= ArrivalThreshold)
	{
		MoveToNextWaypoint();
		return;
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
	bIsMoving = false;

	AUnitBase* OwnerUnit = GetOwnerUnit();
	if (OwnerUnit)
	{
		// CodeRevision: INC-2025-00030-R1 (Migrate to UGridPathfindingSubsystem) (2025-11-16 23:55)
		if (UWorld* World = GetWorld())
		{
			if (UGridOccupancySubsystem* Occupancy = World->GetSubsystem<UGridOccupancySubsystem>())
			{
				if (UGridPathfindingSubsystem* PathFinder = World->GetSubsystem<UGridPathfindingSubsystem>())
				{
					const FIntPoint FinalCell = PathFinder->WorldToGrid(OwnerUnit->GetActorLocation());
					if (!Occupancy->UpdateActorCell(OwnerUnit, FinalCell))
					{
						// 更新が失敗した場合（競合など）、短時間後に再試行する
						World->GetTimerManager().SetTimer(
							GridUpdateRetryHandle,
							this,
							&UUnitMovementComponent::FinishMovement,
							0.1f,
							false);
						return; // ここで処理を中断し、再試行に任せる
					}

					UE_LOG(LogTemp, Verbose,
						TEXT("[UnitMovementComponent] GridOccupancy updated: Actor=%s Cell=(%d,%d)"),
						*GetNameSafe(OwnerUnit), FinalCell.X, FinalCell.Y);
				}
			}
		}
	}

	CurrentPath.Empty();
	CurrentPathIndex = 0;

	// Tickを無効化
	SetComponentTickEnabled(false);

	// イベント配信
	OnMoveFinished.Broadcast(OwnerUnit);

	UE_LOG(LogTemp, Log, TEXT("[UnitMovementComponent] Movement finished"));
}

AUnitBase* UUnitMovementComponent::GetOwnerUnit() const
{
	return Cast<AUnitBase>(GetOwner());
}
