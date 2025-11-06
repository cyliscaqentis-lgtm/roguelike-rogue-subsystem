#include "Abilities/GA_MoveBase.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Rogue/Character/UnitBase.h"
#include "Rogue/Data/MoveInputPayloadBase.h"
#include "Rogue/Grid/GridOccupancySubsystem.h"
#include "Rogue/Grid/GridPathfindingLibrary.h"
#include "Rogue/Turn/TurnActionBarrierSubsystem.h"
#include "Rogue/Utility/RogueGameplayTags.h"
#include "Turn/GameTurnManagerBase.h"
#include "Turn/TurnEncoding.h"

#include "../ProjectDiagnostics.h"

DEFINE_LOG_CATEGORY_STATIC(LogMoveVerbose, Log, All);

namespace GA_MoveBase_Private
{
	static constexpr float MaxAllowedStepDistance = 1000.0f;
	static constexpr float WaypointSpacingCm = 10.0f;
	static constexpr float AlignTraceUp = 200.0f;
	static constexpr float AlignTraceDown = 2000.0f;
	static const FGameplayTag DungeonStepTag = FGameplayTag::RequestGameplayTag(TEXT("Event.Dungeon.Step"));
	static const FGameplayTag TurnAbilityCompletedTag = FGameplayTag::RequestGameplayTag(TEXT("Gameplay.Event.Turn.Ability.Completed"));
	static const FGameplayTag MoveAbilityCompletedTag = FGameplayTag::RequestGameplayTag(TEXT("Ability.Move.Completed"));
}

UGA_MoveBase::UGA_MoveBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;
	ReplicationPolicy = EGameplayAbilityReplicationPolicy::ReplicateYes;
	MaxExecutionTime = 10.0f;

	TagStateMoving = RogueGameplayTags::State_Moving.GetTag();

	ActivationBlockedTags.AddTag(RogueGameplayTags::State_Action_InProgress.GetTag());
	ActivationBlockedTags.AddTag(RogueGameplayTags::State_Ability_Executing.GetTag());

	ActivationOwnedTags.AddTag(RogueGameplayTags::State_Action_InProgress.GetTag());
	ActivationOwnedTags.AddTag(TagStateMoving);

	FAbilityTriggerData Trigger;
	Trigger.TriggerTag = RogueGameplayTags::Ability_Move.GetTag();
	Trigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	AbilityTriggers.Add(Trigger);

	UE_LOG(LogMoveVerbose, Verbose,
		TEXT("[GA_MoveBase] Constructor registered trigger %s"),
		*Trigger.TriggerTag.ToString());
}

bool UGA_MoveBase::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	return ActorInfo && ActorInfo->AvatarActor.IsValid();
}

void UGA_MoveBase::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	CachedSpecHandle = Handle;
	if (ActorInfo)
	{
		CachedActorInfo = *ActorInfo;
	}
	CachedActivationInfo = ActivationInfo;

	AActor* Avatar = GetAvatarActorFromActorInfo();
	CachedStartLocWS = Avatar ? Avatar->GetActorLocation() : FVector::ZeroVector;
	CachedFirstLoc = CachedStartLocWS;
	FirstLoc = CachedStartLocWS;
	AbilityStartTime = FPlatformTime::Seconds();

	if (!TriggerEventData)
	{
		UE_LOG(LogTurnManager, Error, TEXT("[GA_MoveBase] ActivateAbility failed: TriggerEventData is null"));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	FVector EncodedDirection = FVector::ZeroVector;
	if (!ExtractDirectionFromEventData(TriggerEventData, EncodedDirection))
	{
		UE_LOG(LogTurnManager, Error, TEXT("[GA_MoveBase] Failed to decode direction payload"));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	const AGridPathfindingLibrary* PathFinder = GetPathFinder();
	if (!PathFinder)
	{
		UE_LOG(LogTurnManager, Error, TEXT("[GA_MoveBase] PathFinder not available"));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (!RegisterBarrier(Avatar))
	{
		UE_LOG(LogTurnManager, Warning, TEXT("[GA_MoveBase] Barrier subsystem not found"));
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (ASC)
	{
		int32 CountBefore = ASC->GetTagCount(RogueGameplayTags::State_Action_InProgress.GetTag());
		DIAG_LOG(Log, TEXT("[GA_MoveBase] ActivateAbility: InProgress count before=%d"), CountBefore);
		ASC->AddLooseGameplayTag(GA_MoveBase_Private::DungeonStepTag);
	}

	AUnitBase* Unit = Cast<AUnitBase>(Avatar);
	if (Unit)
	{
		Unit->bSkipMoveAnimation = ShouldSkipAnimation_Implementation();
	}

	if (EncodedDirection.Z < -0.5f)
	{
		const FIntPoint TargetCell(
			FMath::RoundToInt(EncodedDirection.X),
			FMath::RoundToInt(EncodedDirection.Y));

		StartMoveToCell(TargetCell);
		return;
	}

	if (!Avatar)
	{
		UE_LOG(LogTurnManager, Error, TEXT("[GA_MoveBase] Avatar actor is null"));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	const FVector CurrentLocation = Avatar->GetActorLocation();
	const FIntPoint CurrentCell = PathFinder->WorldToGrid(CurrentLocation);

	FVector2D Dir2D(EncodedDirection.X, EncodedDirection.Y);
	if (!Dir2D.Normalize())
	{
		UE_LOG(LogTurnManager, Warning, TEXT("[GA_MoveBase] Direction vector length is zero"));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	const FIntPoint Step(
		FMath::RoundToInt(Dir2D.X),
		FMath::RoundToInt(Dir2D.Y));

	if (Step.X == 0 && Step.Y == 0)
	{
		UE_LOG(LogTurnManager, Warning, TEXT("[GA_MoveBase] Normalized direction truncated to zero step"));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	const FIntPoint NextCell = CurrentCell + Step;
	const FVector2D StepDir2D(Step.X, Step.Y);
	const FVector LocalTarget = CalculateNextTilePosition(CurrentLocation, StepDir2D);

	NextTileStep = SnapToCellCenter(LocalTarget);
	NextTileStep.Z = CachedStartLocWS.Z;

	const float MoveDistance = FVector::Dist(CurrentLocation, NextTileStep);
	if (MoveDistance > GA_MoveBase_Private::MaxAllowedStepDistance)
	{
		UE_LOG(LogTurnManager, Warning,
			TEXT("[GA_MoveBase] Step distance %.1f is suspicious (from %s to %s)"),
			MoveDistance,
			*CurrentLocation.ToCompactString(),
			*NextTileStep.ToCompactString());
	}

	if (MoveDistance < KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogTurnManager, Verbose, TEXT("[GA_MoveBase] Zero distance move detected – ending ability"));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}

	if (!IsTileWalkable(NextTileStep, Unit))
	{
		UE_LOG(LogTurnManager, Warning,
			TEXT("[GA_MoveBase] Next tile %s is not walkable"),
			*NextTileStep.ToCompactString());
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	const float DesiredRotation = RoundYawTo45Degrees(
		FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(Step.Y), static_cast<float>(Step.X))));
	DesiredYaw = DesiredRotation;

	const FRotator TargetRotator(0.f, DesiredYaw, 0.f);
	Avatar->SetActorRotation(TargetRotator, ETeleportType::TeleportPhysics);

	BindMoveFinishedDelegate();

	if (Unit)
	{
		TArray<FVector> PathPoints;
		const FVector EndPos = NextTileStep;
		const int32 NumSamples = FMath::Max(2, FMath::CeilToInt(MoveDistance / GA_MoveBase_Private::WaypointSpacingCm));

		for (int32 Index = 1; Index <= NumSamples; ++Index)
		{
			const float Alpha = static_cast<float>(Index) / static_cast<float>(NumSamples);
			PathPoints.Add(FMath::Lerp(CurrentLocation, EndPos, Alpha));
		}

		Unit->MoveUnit(PathPoints);
	}
	else
	{
		UE_LOG(LogTurnManager, Warning, TEXT("[GA_MoveBase] Unit cast failed – finishing immediately"));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
	}
}

void UGA_MoveBase::ActivateAbilityFromEvent(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* EventData)
{
	ActivateAbility(Handle, ActorInfo, ActivationInfo, EventData);
}

void UGA_MoveBase::CancelAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateCancelAbility)
{
	UE_LOG(LogMoveVerbose, Verbose, TEXT("[GA_MoveBase] Ability cancelled"));
	Super::CancelAbility(Handle, ActorInfo, ActivationInfo, bReplicateCancelAbility);
}

void UGA_MoveBase::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	const double Elapsed = (AbilityStartTime > 0.0)
		? (FPlatformTime::Seconds() - AbilityStartTime)
		: 0.0;
	AbilityStartTime = 0.0;

	UE_LOG(LogMoveVerbose, Verbose,
		TEXT("[GA_MoveBase] EndAbility (cancelled=%d, elapsed=%.2fs)"),
		bWasCancelled ? 1 : 0,
		Elapsed);

	if (MoveFinishedHandle.IsValid())
	{
		if (AUnitBase* Unit = Cast<AUnitBase>(GetAvatarActorFromActorInfo()))
		{
			Unit->OnMoveFinished.Remove(MoveFinishedHandle);
		}
		MoveFinishedHandle.Reset();
	}

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->RemoveLooseGameplayTag(GA_MoveBase_Private::DungeonStepTag);
	}

	if (UWorld* World = GetWorld())
	{
		if (UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>())
		{
			AActor* Avatar = GetAvatarActorFromActorInfo();
			if (Barrier && Avatar)
			{
				Barrier->CompleteAction(Avatar, MoveTurnId, MoveActionId);
				Barrier->NotifyMoveFinished(Avatar, MoveTurnId);
			}
		}
	}

	MoveTurnId = INDEX_NONE;
	MoveActionId.Invalidate();

	SendCompletionEvent(bWasCancelled);

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_MoveBase::SendCompletionEvent(bool bTimedOut)
{
	Super::SendCompletionEvent(bTimedOut);

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		FGameplayEventData EventData;
		EventData.EventMagnitude = bTimedOut ? 1.0f : 0.0f;
		EventData.Instigator = GetAvatarActorFromActorInfo();

		ASC->HandleGameplayEvent(GA_MoveBase_Private::TurnAbilityCompletedTag, &EventData);
		ASC->HandleGameplayEvent(GA_MoveBase_Private::MoveAbilityCompletedTag, &EventData);
	}
}

bool UGA_MoveBase::ExtractDirectionFromEventData(const FGameplayEventData* EventData, FVector& OutDirection)
{
	OutDirection = FVector::ZeroVector;
	if (!EventData)
	{
		return false;
	}

	const int32 Magnitude = FMath::RoundToInt(EventData->EventMagnitude);
	DIAG_LOG(Log, TEXT("[GA_MoveBase] ExtractDirection magnitude=%d (raw=%.2f)"),
		Magnitude, EventData->EventMagnitude);

	if (Magnitude >= 2000000)
	{
		int32 GridX = 0;
		int32 GridY = 0;
		if (TurnEncoding::UnpackCell(Magnitude, GridX, GridY))
		{
			OutDirection = FVector(GridX, GridY, -1.0f);
			return true;
		}

		UE_LOG(LogTurnManager, Warning,
			TEXT("[GA_MoveBase] UnpackCell failed for magnitude=%d"), Magnitude);
		return false;
	}

	if (Magnitude >= 1000)
	{
		const int32 PackedDir = Magnitude - 1000;
		int32 Dx = 0;
		int32 Dy = 0;
		if (TurnEncoding::UnpackDir(PackedDir, Dx, Dy))
		{
			OutDirection = FVector(Dx, Dy, 0.0f);
			return true;
		}

		UE_LOG(LogTurnManager, Warning,
			TEXT("[GA_MoveBase] UnpackDir failed for code=%d"), PackedDir);
		return false;
	}

	UE_LOG(LogTurnManager, Warning,
		TEXT("[GA_MoveBase] Invalid magnitude=%d (expected >= 1000)"),
		Magnitude);
	return false;
}

FVector2D UGA_MoveBase::QuantizeToGridDirection(const FVector& InDirection)
{
	const FVector2D Dir2D(InDirection.X, InDirection.Y);
	const FVector2D Normalized = Dir2D.GetSafeNormal();

	const float AbsX = FMath::Abs(Normalized.X);
	const float AbsY = FMath::Abs(Normalized.Y);

	const float X = (AbsX >= AbsY) ? FMath::Sign(Normalized.X) : 0.0f;
	const float Y = (AbsY >= AbsX) ? FMath::Sign(Normalized.Y) : 0.0f;
	return FVector2D(X, Y);
}

FVector UGA_MoveBase::CalculateNextTilePosition(const FVector& CurrentPosition, const FVector2D& Dir)
{
	const FVector PlanarDir(Dir.X, Dir.Y, 0.0f);
	const FVector SafeDir = PlanarDir.SizeSquared() > KINDA_SMALL_NUMBER
		? PlanarDir.GetSafeNormal()
		: FVector::ZeroVector;
	return CurrentPosition + SafeDir * GridSize;
}

bool UGA_MoveBase::IsTileWalkable(const FVector& TilePosition, AUnitBase* Self)
{
	const FVector Aligned = AlignZToGround(TilePosition);
	const FIntPoint Cell = GetPathFinder()
		? GetPathFinder()->WorldToGrid(Aligned)
		: FIntPoint::ZeroValue;

	return IsTileWalkable(Cell);
}

bool UGA_MoveBase::IsTileWalkable(const FIntPoint& Cell) const
{
	const AGridPathfindingLibrary* PathFinder = GetPathFinder();
	if (!PathFinder)
	{
		return false;
	}
	return PathFinder->IsCellWalkable(Cell);
}

void UGA_MoveBase::UpdateGridState(const FVector& Position, int32 Value)
{
	AGridPathfindingLibrary* PathFinder = const_cast<AGridPathfindingLibrary*>(GetPathFinder());
	if (!PathFinder)
	{
		return;
	}

	const FVector Snapped = SnapToCellCenter(Position);
	PathFinder->GridChangeVector(Snapped, Value);
	UpdateOccupancy(GetAvatarActorFromActorInfo(), PathFinder->WorldToGrid(Snapped));
}

void UGA_MoveBase::UpdateOccupancy(AActor* UnitActor, const FIntPoint& NewCell)
{
	if (!UnitActor)
	{
		return;
	}

	if (UWorld* World = UnitActor->GetWorld())
	{
		if (UGridOccupancySubsystem* Occupancy = World->GetSubsystem<UGridOccupancySubsystem>())
		{
			Occupancy->UpdateActorCell(UnitActor, NewCell);
		}
	}
}

float UGA_MoveBase::RoundYawTo45Degrees(float Yaw)
{
	return FMath::RoundToInt(Yaw / 45.0f) * 45.0f;
}

const AGridPathfindingLibrary* UGA_MoveBase::GetPathFinder() const
{
	if (CachedPathFinder.IsValid())
	{
		return CachedPathFinder.Get();
	}

	if (const UWorld* World = GetWorld())
	{
		if (AGridPathfindingLibrary* Found = Cast<AGridPathfindingLibrary>(
			UGameplayStatics::GetActorOfClass(World, AGridPathfindingLibrary::StaticClass())))
		{
			CachedPathFinder = Found;
			return CachedPathFinder.Get();
		}
	}

	return nullptr;
}

void UGA_MoveBase::BindMoveFinishedDelegate()
{
	if (MoveFinishedHandle.IsValid())
	{
		return;
	}

	if (AUnitBase* Unit = Cast<AUnitBase>(GetAvatarActorFromActorInfo()))
	{
		MoveFinishedHandle = Unit->OnMoveFinished.AddUObject(this, &UGA_MoveBase::OnMoveFinished);
	}
}

void UGA_MoveBase::OnMoveFinished(AUnitBase* Unit)
{
	if (Unit)
	{
		CachedFirstLoc = Unit->GetActorLocation();
		UpdateGridState(CachedFirstLoc, 1);
	}

	EndAbility(CachedSpecHandle, &CachedActorInfo, CachedActivationInfo, true, false);
}

void UGA_MoveBase::StartMoveToCell(const FIntPoint& TargetCell)
{
	AUnitBase* Unit = Cast<AUnitBase>(GetAvatarActorFromActorInfo());
	if (!Unit)
	{
		UE_LOG(LogTurnManager, Error, TEXT("[GA_MoveBase] StartMoveToCell failed: Avatar is not a UnitBase"));
		EndAbility(CachedSpecHandle, &CachedActorInfo, CachedActivationInfo, true, true);
		return;
	}

	const AGridPathfindingLibrary* PathFinder = GetPathFinder();
	if (!PathFinder)
	{
		UE_LOG(LogTurnManager, Error, TEXT("[GA_MoveBase] StartMoveToCell failed: PathFinder missing"));
		EndAbility(CachedSpecHandle, &CachedActorInfo, CachedActivationInfo, true, true);
		return;
	}

	if (!IsTileWalkable(TargetCell))
	{
		UE_LOG(LogTurnManager, Warning,
			TEXT("[GA_MoveBase] Target cell (%d,%d) not walkable"), TargetCell.X, TargetCell.Y);
		EndAbility(CachedSpecHandle, &CachedActorInfo, CachedActivationInfo, true, true);
		return;
	}

	const FVector StartPos = Unit->GetActorLocation();
	const FIntPoint CurrentCell = PathFinder->WorldToGrid(StartPos);
	const FVector EndPos = SnapToCellCenter(PathFinder->GridToWorld(TargetCell, StartPos.Z));
	const float TotalDistance = FVector::Dist(StartPos, EndPos);

	if (TotalDistance < KINDA_SMALL_NUMBER)
	{
		EndAbility(CachedSpecHandle, &CachedActorInfo, CachedActivationInfo, true, false);
		return;
	}

	NextTileStep = EndPos;
	const FIntPoint MoveDir(
		FMath::Clamp(TargetCell.X - CurrentCell.X, -1, 1),
		FMath::Clamp(TargetCell.Y - CurrentCell.Y, -1, 1));
	DesiredYaw = RoundYawTo45Degrees(
		FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(MoveDir.Y), static_cast<float>(MoveDir.X))));

	TArray<FVector> PathPoints;
	const int32 NumSamples = FMath::Max(2, FMath::CeilToInt(TotalDistance / GA_MoveBase_Private::WaypointSpacingCm));

	for (int32 Index = 1; Index <= NumSamples; ++Index)
	{
		const float Alpha = static_cast<float>(Index) / static_cast<float>(NumSamples);
		PathPoints.Add(FMath::Lerp(StartPos, EndPos, Alpha));
	}

	BindMoveFinishedDelegate();
	Unit->MoveUnit(PathPoints);
}

FVector UGA_MoveBase::SnapToCellCenter(const FVector& WorldPos) const
{
	const AGridPathfindingLibrary* PathFinder = GetPathFinder();
	if (!PathFinder)
	{
		return WorldPos;
	}

	const FIntPoint Cell = PathFinder->WorldToGrid(WorldPos);
	return PathFinder->GridToWorld(Cell, WorldPos.Z);
}

FVector UGA_MoveBase::AlignZToGround(const FVector& WorldPos, float TraceUp, float TraceDown) const
{
	if (UWorld* World = GetWorld())
	{
		const FVector TraceStart = WorldPos + FVector(0.f, 0.f, TraceUp);
		const FVector TraceEnd = WorldPos - FVector(0.f, 0.f, TraceDown);

		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MoveAlign), false);

		if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, Params))
		{
			return Hit.ImpactPoint;
		}
	}

	return WorldPos;
}

void UGA_MoveBase::DebugDumpAround(const FIntPoint& Center)
{
#if !UE_BUILD_SHIPPING
	const AGridPathfindingLibrary* PathFinder = GetPathFinder();
	if (!PathFinder)
	{
		return;
	}

	for (int32 DeltaY = 1; DeltaY >= -1; --DeltaY)
	{
		FString Row;
		for (int32 DeltaX = -1; DeltaX <= 1; ++DeltaX)
		{
			const FIntPoint Cell(Center.X + DeltaX, Center.Y + DeltaY);
			const bool bWalkable = PathFinder->IsCellWalkable(Cell);
			Row += FString::Printf(TEXT("%s%2d "),
				(DeltaX == 0 && DeltaY == 0) ? TEXT("[") : TEXT(" "),
				bWalkable ? 1 : 0);
		}
		UE_LOG(LogMoveVerbose, Verbose, TEXT("[GA_MoveBase] %s"), *Row);
	}
#endif
}

bool UGA_MoveBase::ShouldSkipAnimation_Implementation()
{
	return false;
}

bool UGA_MoveBase::RegisterBarrier(AActor* Avatar)
{
	if (!Avatar)
	{
		return false;
	}

	if (UWorld* World = Avatar->GetWorld())
	{
		if (UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>())
		{
			const int32 CurrentTurn = Barrier->GetCurrentTurnId();
			MoveTurnId = CurrentTurn;
			MoveActionId = Barrier->RegisterAction(Avatar, MoveTurnId);
			Barrier->NotifyMoveStarted(Avatar, MoveTurnId);
			return true;
		}
	}

	return false;
}
