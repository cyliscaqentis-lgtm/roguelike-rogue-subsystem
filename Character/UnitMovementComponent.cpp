// ============================================================================
// UnitMovementComponent.cpp
// ユニット移動処理Component実装
// UnitBaseから分離（2025-11-09）
// ============================================================================

#include "Character/UnitMovementComponent.h"
#include "Character/UnitBase.h"
#include "Grid/GridPathfindingLibrary.h"
#include "GameFramework/Actor.h"
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

void UUnitMovementComponent::MoveUnitAlongGridPath(const TArray<FIntPoint>& GridPath, AGridPathfindingLibrary* PathFinder)
{
	if (!PathFinder)
	{
		UE_LOG(LogTemp, Error, TEXT("[UnitMovementComponent] MoveUnitAlongGridPath called with null PathFinder"));
		return;
	}

	if (GridPath.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[UnitMovementComponent] MoveUnitAlongGridPath called with empty path"));
		return;
	}

	// グリッドパスをワールド座標に変換
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
	UE_LOG(LogTemp, Log, TEXT("[UnitMovementComponent] Move speed set to %.2f"), MoveSpeed);
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
		NewLocation = FMath::VInterpConstantTo(CurrentLocation, TargetLocation, DeltaTime, MoveSpeed);
	}
	else
	{
		// 直線移動
		const float MoveDistance = MoveSpeed * DeltaTime;
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
	CurrentPath.Empty();
	CurrentPathIndex = 0;

	// Tickを無効化
	SetComponentTickEnabled(false);

	// イベント配信
	AUnitBase* OwnerUnit = GetOwnerUnit();
	OnMoveFinished.Broadcast(OwnerUnit);

	UE_LOG(LogTemp, Log, TEXT("[UnitMovementComponent] Movement finished"));
}

AUnitBase* UUnitMovementComponent::GetOwnerUnit() const
{
	return Cast<AUnitBase>(GetOwner());
}
