#include "Abilities/GA_MoveBase.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/GameplayStatics.h"
// CodeRevision: INC-2025-00030-R2 (Migrate to UGridPathfindingSubsystem) (2025-11-17 00:40)
#include "Rogue/Character/UnitBase.h"
#include "Rogue/Character/UnitMovementComponent.h"
#include "Rogue/Grid/GridOccupancySubsystem.h"
#include "Rogue/Grid/GridPathfindingSubsystem.h"
#include "Rogue/Utility/RogueGameplayTags.h"
#include "Turn/GameTurnManagerBase.h"
#include "Turn/TurnFlowCoordinator.h"
#include "Utility/PathFinderUtils.h"
#include "Utility/TurnCommandEncoding.h"
#include "TimerManager.h"

#include "../ProjectDiagnostics.h"

DEFINE_LOG_CATEGORY_STATIC(LogMoveVerbose, Log, All);

namespace GA_MoveBase_Private
{
	static constexpr float MaxAllowedStepDistance = 1000.0f;
	static constexpr float WaypointSpacingCm = 10.0f;
	static constexpr float AlignTraceUp = 200.0f;
	static constexpr float AlignTraceDown = 2000.0f;

	static int32 AxisSign(float Value)
	{
		if (Value > KINDA_SMALL_NUMBER)
		{
			return 1;
		}
		if (Value < -KINDA_SMALL_NUMBER)
		{
			return -1;
		}
		return 0;
	}

	static FIntPoint QuantizeStepToGrid(const FIntPoint& RawStep, const FVector2D& RawDir, bool& bAdjusted)
	{
		const int32 XSign = AxisSign(RawDir.X);
		const int32 YSign = AxisSign(RawDir.Y);

		FIntPoint Result(XSign, YSign);
		const bool bHasDiagIntent = (XSign != 0 && YSign != 0);

		if (Result == FIntPoint::ZeroValue)
		{
			Result.X = AxisSign(static_cast<float>(RawStep.X));
			Result.Y = AxisSign(static_cast<float>(RawStep.Y));
		}

		Result.X = FMath::Clamp(Result.X, -1, 1);
		Result.Y = FMath::Clamp(Result.Y, -1, 1);

		const int32 ManDist = FMath::Abs(Result.X) + FMath::Abs(Result.Y);
		if (ManDist == 0)
		{
			return FIntPoint::ZeroValue;
		}

		bAdjusted = (Result.X != RawStep.X || Result.Y != RawStep.Y);

		if (ManDist > 2)
		{
			Result.X = FMath::Clamp(Result.X, -1, 1);
			Result.Y = FMath::Clamp(Result.Y, -1, 1);
		}

		if (!bHasDiagIntent && ManDist > 1)
		{
			// Prefer the dominant axis when direction input was mostly axial.
			if (FMath::Abs(RawDir.X) >= FMath::Abs(RawDir.Y))
			{
				Result.Y = 0;
			}
			else
			{
				Result.X = 0;
			}
		}

		return Result;
	}
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
	Trigger.TriggerTag = RogueGameplayTags::GameplayEvent_Intent_Move;
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

bool UGA_MoveBase::ShouldRespondToEvent(
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayEventData* Payload) const
{
	if (!Payload)
	{
		UE_LOG(LogMoveVerbose, Verbose, TEXT("[GA_MoveBase] ShouldRespondToEvent: Payload is null"));
		return false;
	}

	if (!Payload->EventTag.MatchesTagExact(RogueGameplayTags::GameplayEvent_Intent_Move))
	{
		UE_LOG(LogMoveVerbose, Verbose,
			TEXT("[GA_MoveBase] ShouldRespondToEvent: EventTag mismatch (Expected=%s, Got=%s)"),
			*RogueGameplayTags::GameplayEvent_Intent_Move.GetTag().ToString(),
			*Payload->EventTag.ToString());
		return false;
	}

	const int32 RawMagnitude = FMath::RoundToInt(Payload->EventMagnitude);
	if (RawMagnitude < TurnCommandEncoding::kDirBase)
	{
		UE_LOG(LogMoveVerbose, Verbose,
			TEXT("[GA_MoveBase] ShouldRespondToEvent: Invalid magnitude %d (expected >= %d)"),
			RawMagnitude, TurnCommandEncoding::kDirBase);
		return false;
	}

	if (!ActorInfo || !ActorInfo->AvatarActor.IsValid())
	{
		UE_LOG(LogMoveVerbose, Verbose, TEXT("[GA_MoveBase] ShouldRespondToEvent: Invalid ActorInfo"));
		return false;
	}

	UE_LOG(LogMoveVerbose, Log,
		TEXT("[GA_MoveBase] ShouldRespondToEvent: 笨・Passed all checks (Actor=%s, EventTag=%s, Magnitude=%d)"),
		*GetNameSafe(ActorInfo->AvatarActor.Get()),
		*Payload->EventTag.ToString(),
		RawMagnitude);

	return true;
}

void UGA_MoveBase::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	AActor* Avatar = GetAvatarActorFromActorInfo();
	int32 TeamId = -1;
	if (AUnitBase* Unit = Cast<AUnitBase>(Avatar))
	{
		TeamId = Unit->Team;
	}
	UE_LOG(LogTurnManager, Log,
		TEXT("[GA_MoveBase] ABILITY ACTIVATED: Actor=%s, Team=%d"),
		*GetNameSafe(Avatar), TeamId);

	// CodeRevision: INC-2025-00030-R3 (Add Barrier sync to GA_MoveBase) (2025-11-17 01:00)
	// P1: Fix turn hang - Register player move with TurnActionBarrierSubsystem
	bBarrierRegistered = false;
	MoveTurnId = INDEX_NONE;
	MoveActionId.Invalidate();

	if (Avatar)
	{
		if (UWorld* World = GetWorld())
		{
			if (UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>())
			{
				MoveTurnId = Barrier->GetCurrentTurnId();
				MoveActionId = Barrier->RegisterAction(Avatar, MoveTurnId);
				bBarrierRegistered = MoveActionId.IsValid();

				UE_LOG(LogTurnManager, Log,
					TEXT("[GA_MoveBase] Registered with Barrier: Turn=%d ActionId=%s Registered=%d"),
					MoveTurnId,
					*MoveActionId.ToString(),
					bBarrierRegistered ? 1 : 0);
			}
		}
	}

	CachedSpecHandle = Handle;
	if (ActorInfo)
	{
		CachedActorInfo = *ActorInfo;
	}
	CachedActivationInfo = ActivationInfo;

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

	// CodeRevision: INC-2025-00018-R3 (Remove barrier management - Phase 3) (2025-11-17)
	// CodeRevision: INC-2025-00030-R1 (Use TurnFlowCoordinator instead of GetCurrentTurnIndex) (2025-11-16 00:00)
	// TurnId retrieval simplified - only from TurnManager for CompletedTurnIdForEvent
	if (const AGameTurnManagerBase* TurnManager = Cast<AGameTurnManagerBase>(TriggerEventData->OptionalObject.Get()))
	{
		if (UWorld* World = GetWorld())
		{
			if (UTurnFlowCoordinator* TFC = World->GetSubsystem<UTurnFlowCoordinator>())
			{
				CompletedTurnIdForEvent = TFC->GetCurrentTurnId();
			}
			else
			{
				CompletedTurnIdForEvent = TurnManager->GetCurrentTurnId();
			}
		}
		else
		{
			CompletedTurnIdForEvent = TurnManager->GetCurrentTurnId();
		}
		UE_LOG(LogTurnManager, Log,
			TEXT("[GA_MoveBase] TurnId retrieved from TurnManager: %d (Actor=%s)"),
			CompletedTurnIdForEvent, *GetNameSafe(Avatar));
	}
	else
	{
		CompletedTurnIdForEvent = INDEX_NONE;
		UE_LOG(LogTurnManager, Warning,
			TEXT("[GA_MoveBase] TurnManager not available in TriggerEventData - CompletedTurnIdForEvent set to INDEX_NONE"));
	}

	// 笘・・笘・Magnitude 讀懆ｨｼ・・urnCommandEncoding 遽・峇繝√ぉ繝・け・・笘・・笘・
			const int32 RawMagnitude = FMath::RoundToInt(TriggerEventData->EventMagnitude);
			FIntPoint TargetCell(-1, -1);
			if (RawMagnitude >= TurnCommandEncoding::kCellBase)
			{
				int32 GridX = 0;
				int32 GridY = 0;
				if (TurnCommandEncoding::UnpackCell(RawMagnitude, GridX, GridY))
				{
					TargetCell = FIntPoint(GridX, GridY);
				}
			}

			if (TargetCell.X == -1 || TargetCell.Y == -1)
			{
				UE_LOG(LogTurnManager, Error,
					TEXT(\"[GA_MoveBase] ABORT: TriggerEventData missing resolved TargetCell (magnitude=%d)\"),
					RawMagnitude);
				EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
				return;
			}

			CachedNextCell = TargetCell;
			UE_LOG(LogTurnManager, Log,
				TEXT(\"[GA_MoveBase] TargetCell resolved by ConflictResolver: From=%s -> Target=%s\"),
				*CurrentCell.ToString(), *TargetCell.ToString());

			if (!Pathfinding->IsCellWalkableIgnoringActor(TargetCell, Unit))
			{
				UE_LOG(LogTurnManager, Warning,
					TEXT(\"[GA_MoveBase] Cell (%d,%d) is blocked by terrain; aborting move\"),
					TargetCell.X, TargetCell.Y);
				EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
				return;
			}

			// CodeRevision: INC-2025-00018-R3 (Remove barrier management - Phase 3) (2025-11-17)
			// Barrier registration removed - handled by other systems
			const FVector DestWorldLoc = Pathfinding->GridToWorld(CachedNextCell);
	NextTileStep = SnapToCellCenterFixedZ(DestWorldLoc, FixedZ);
	NextTileStep.Z = FixedZ;

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
		UE_LOG(LogTurnManager, Verbose, TEXT("[GA_MoveBase] Zero distance move detected 窶・ending ability"));
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

	BindMoveFinishedDelegate();

	if (Unit)
	{
		TArray<FVector> PathPoints;
		const FVector EndPos = NextTileStep;
		const int32 NumSamples = FMath::Max(2, FMath::CeilToInt(MoveDistance / GA_MoveBase_Private::WaypointSpacingCm));

		for (int32 Index = 1; Index <= NumSamples; ++Index)
		{
			const float Alpha = static_cast<float>(Index) / static_cast<float>(NumSamples);
			FVector Sample = FMath::Lerp(CurrentLocation, EndPos, Alpha);
			Sample.Z = FixedZ;
			PathPoints.Add(Sample);
		}

		Unit->MoveUnit(PathPoints);
	}
	else
	{
		UE_LOG(LogTurnManager, Warning, TEXT("[GA_MoveBase] Unit cast failed 窶・finishing immediately"));
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
	// Sparky fix: Prevent re-entry
	if (bIsEnding)
	{
		UE_LOG(LogTurnManager, Warning, TEXT("[GA_MoveBase] Already ending, ignoring recursive call"));
		return;
	}
	bIsEnding = true;

	const double Elapsed = (AbilityStartTime > 0.0)
		? (FPlatformTime::Seconds() - AbilityStartTime)
		: 0.0;
	AbilityStartTime = 0.0;

	UE_LOG(LogMoveVerbose, Verbose,
		TEXT("[GA_MoveBase] EndAbility (cancelled=%d, elapsed=%.2fs)"),
		bWasCancelled ? 1 : 0,
		Elapsed);

	// CodeRevision: INC-2025-00018-R1 (Bind move completion delegate through UnitMovementComponent) (2025-11-17 13:15)
	if (bMoveFinishedDelegateBound)
	{
		if (AUnitBase* Unit = Cast<AUnitBase>(GetAvatarActorFromActorInfo()))
		{
			if (UUnitMovementComponent* MovementComp = Unit->MovementComp.Get())
			{
				MovementComp->OnMoveFinished.RemoveDynamic(this, &UGA_MoveBase::OnMoveFinished);
			}
		}
		bMoveFinishedDelegateBound = false;
	}

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->RemoveLooseGameplayTag(RogueGameplayTags::Event_Dungeon_Step);
	}

	// CodeRevision: INC-2025-00018-R3 (Remove barrier management - Phase 3) (2025-11-17)
	// Barrier completion removed - handled by other systems

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);

	CompletedTurnIdForEvent = INDEX_NONE;

	bIsEnding = false;
}

void UGA_MoveBase::SendCompletionEvent(bool bTimedOut)
{
    if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
    {
        const int32 NotifiedTurnId = CompletedTurnIdForEvent;

        if (NotifiedTurnId == INDEX_NONE || NotifiedTurnId < 0)
        {
            UE_LOG(LogTurnManager, Error,
                TEXT("[SendCompletionEvent] INVALID TurnId=%d, NOT sending completion event! (Actor=%s)"),
                NotifiedTurnId, *GetNameSafe(GetAvatarActorFromActorInfo()));
            return;
        }

        UE_LOG(LogTurnManager, Warning,
            TEXT("[TurnNotify] GA_MoveBase completion: Actor=%s TurnId=%d"),
            *GetNameSafe(GetAvatarActorFromActorInfo()), NotifiedTurnId);

        FGameplayEventData EventData;
        EventData.Instigator = GetAvatarActorFromActorInfo();
        EventData.OptionalObject = this;
        EventData.EventMagnitude = static_cast<float>(NotifiedTurnId);
        EventData.EventTag = RogueGameplayTags::Gameplay_Event_Turn_Ability_Completed;

        ASC->HandleGameplayEvent(RogueGameplayTags::Gameplay_Event_Turn_Ability_Completed, &EventData);

        EventData.EventTag = RogueGameplayTags::Ability_Move_Completed;
        ASC->HandleGameplayEvent(RogueGameplayTags::Ability_Move_Completed, &EventData);
    }
}


	const int32 Magnitude = FMath::RoundToInt(EventData->EventMagnitude);
	DIAG_LOG(Log, TEXT("[GA_MoveBase] ExtractDirection magnitude=%d (raw=%.2f)"),
		Magnitude, EventData->EventMagnitude);

	if (Magnitude >= TurnCommandEncoding::kCellBase)
	{
		int32 GridX = 0;
		int32 GridY = 0;
		if (TurnCommandEncoding::UnpackCell(Magnitude, GridX, GridY))
		{
			OutDirection = FVector(GridX, GridY, -1.0f);
			return true;
		}

		UE_LOG(LogTurnManager, Warning,
			TEXT("[GA_MoveBase] UnpackCell failed for magnitude=%d"), Magnitude);
		return false;
	}

	if (Magnitude >= TurnCommandEncoding::kDirBase)
	{
		int32 Dx = 0;
		int32 Dy = 0;
		if (TurnCommandEncoding::UnpackDir(Magnitude, Dx, Dy))
		{
			OutDirection = FVector(Dx, Dy, 0.0f);
			return true;
		}

		UE_LOG(LogTurnManager, Warning,
			TEXT("[GA_MoveBase] UnpackDir failed for code=%d"), Magnitude);
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
	const FVector StepDelta(Dir.X * GridSize, Dir.Y * GridSize, 0.0f);
	return CurrentPosition + StepDelta;
}

// CodeRevision: INC-2025-00030-R1 (Migrate to UGridPathfindingSubsystem) (2025-11-16 23:55)
bool UGA_MoveBase::IsTileWalkable(const FVector& TilePosition, AUnitBase* Self)
{
    UGridPathfindingSubsystem* Pathfinding = GetGridPathfindingSubsystem();
    if (!Pathfinding)
    {
        return false;
    }

    const FIntPoint Cell = Pathfinding->WorldToGrid(TilePosition);
    AActor* ActorToIgnore = Self ? static_cast<AActor*>(Self) : const_cast<AActor*>(GetAvatarActorFromActorInfo());
    return Pathfinding->IsCellWalkableIgnoringActor(Cell, ActorToIgnore);
}

// CodeRevision: INC-2025-00030-R1 (Migrate to UGridPathfindingSubsystem) (2025-11-16 23:55)
bool UGA_MoveBase::IsTileWalkable(const FIntPoint& Cell) const
{
    UGridPathfindingSubsystem* Pathfinding = GetGridPathfindingSubsystem();
    if (!Pathfinding)
    {
        return false;
    }

    AActor* IgnoreActor = const_cast<AActor*>(GetAvatarActorFromActorInfo());
    if (!Pathfinding->IsCellWalkableIgnoringActor(Cell, IgnoreActor))
    {
        UE_LOG(LogTurnManager, Verbose,
            TEXT("[IsTileWalkable] Cell (%d,%d) blocked for %s."),
            Cell.X, Cell.Y, *GetNameSafe(IgnoreActor));
        return false;
    }

    return true;
}

// CodeRevision: INC-2025-00030-R1 (Migrate to UGridPathfindingSubsystem) (2025-11-16 23:55)
void UGA_MoveBase::UpdateGridState(const FVector& Position, int32 Value)
{
	UGridPathfindingSubsystem* Pathfinding = GetGridPathfindingSubsystem();
	if (!Pathfinding)
	{
		return;
	}

	const FVector Snapped = SnapToCellCenter(Position);
	Pathfinding->GridChangeVector(Snapped, Value);
	// CodeRevision: INC-2025-00018-R2 (Remove UpdateOccupancy call - grid update moved to UnitMovementComponent) (2025-11-17)
	// UpdateOccupancy() removed - grid occupancy update is now handled by UnitMovementComponent::FinishMovement()
}

float UGA_MoveBase::RoundYawTo45Degrees(float Yaw)
{
	return FMath::RoundToInt(Yaw / 45.0f) * 45.0f;
}

// CodeRevision: INC-2025-00027-R1 (Add Subsystem access - Phase 2.4) (2025-11-16 00:00)
// CodeRevision: INC-2025-00030-R1 (Remove CachedPathFinder dependency) (2025-11-16 23:55)
// CodeRevision: INC-2025-00030-R2 (Migrate to UGridPathfindingSubsystem) (2025-11-17 00:40)
// GetPathFinder() is deprecated - use GetGridPathfindingSubsystem() instead
// Kept for backward compatibility during migration
const UGridPathfindingSubsystem* UGA_MoveBase::GetPathFinder() const
{
	if (const UWorld* World = GetWorld())
	{
		return FPathFinderUtils::GetCachedPathFinder(const_cast<UWorld*>(World));
	}

	return nullptr;
}

UGridPathfindingSubsystem* UGA_MoveBase::GetGridPathfindingSubsystem() const
{
	if (UWorld* World = GetWorld())
	{
		return World->GetSubsystem<UGridPathfindingSubsystem>();
	}
	return nullptr;
}

AGameTurnManagerBase* UGA_MoveBase::GetTurnManager() const
{
	if (CachedTurnManager.IsValid())
	{
		return CachedTurnManager.Get();
	}

	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AGameTurnManagerBase> It(World); It; ++It)
		{
			CachedTurnManager = *It;
			return CachedTurnManager.Get();
		}
	}

	return nullptr;
}

void UGA_MoveBase::BindMoveFinishedDelegate()
{
	if (bMoveFinishedDelegateBound)
	{
		UE_LOG(LogTurnManager, Warning,
			TEXT("[BindMoveFinishedDelegate] %s: Already bound, skipping"),
			*GetNameSafe(GetAvatarActorFromActorInfo()));
		return;
	}

	if (AUnitBase* Unit = Cast<AUnitBase>(GetAvatarActorFromActorInfo()))
	{
		if (UUnitMovementComponent* MovementComp = Unit->MovementComp.Get())
		{
			// CodeRevision: INC-2025-00018-R1 (Bind move completion delegate through UnitMovementComponent) (2025-11-17 13:15)
			MovementComp->OnMoveFinished.AddDynamic(this, &UGA_MoveBase::OnMoveFinished);
			bMoveFinishedDelegateBound = true;

			UE_LOG(LogTurnManager, Log,
				TEXT("[BindMoveFinishedDelegate] SUCCESS: Unit %s delegate bound to UnitMovementComponent::OnMoveFinished"),
				*GetNameSafe(Unit));
			return;
		}

		UE_LOG(LogTurnManager, Error,
			TEXT("[BindMoveFinishedDelegate] FAILED: MovementComp missing for Unit %s - cannot bind OnMoveFinished"),
			*GetNameSafe(Unit));
	}
	else
	{
		// Sparky diagnostic: Cast failed
		UE_LOG(LogTurnManager, Error,
			TEXT("[BindMoveFinishedDelegate] FAILED: Avatar is not a UnitBase! Avatar=%s"),
			*GetNameSafe(GetAvatarActorFromActorInfo()));
	}
}

void UGA_MoveBase::OnMoveFinished(AUnitBase* Unit)
{
	// CodeRevision: INC-2025-00018-R3 (Remove barrier management and grid update - Phase 3) (2025-11-17)
	// Grid update moved to UnitMovementComponent::FinishMovement()
	// Barrier management removed - handled by other systems
	// CodeRevision: INC-2025-00030-R1 (Migrate to UGridPathfindingSubsystem) (2025-11-16 23:55)

	UGridPathfindingSubsystem* Pathfinding = GetGridPathfindingSubsystem();
	if (Unit && Pathfinding)
	{
		// Position snap to cell center
		const float FixedZ = ComputeFixedZ(Unit, Pathfinding);
		const FVector DestWorldLoc = Pathfinding->GridToWorld(CachedNextCell);
		const FVector SnappedLoc = SnapToCellCenterFixedZ(DestWorldLoc, FixedZ);
		const FVector LocationBefore = Unit->GetActorLocation();

		// CodeRevision: INC-2025-00022-R1 (Update actor rotation on move finish) (2025-11-17 19:00)
		FVector MoveDirection = SnappedLoc - LocationBefore;
		MoveDirection.Z = 0.0f;
		if (!MoveDirection.IsNearlyZero())
		{
			const float TargetYaw = MoveDirection.Rotation().Yaw;
			FRotator NewRotation = Unit->GetActorRotation();
			NewRotation.Yaw = TargetYaw;
			Unit->SetActorRotation(NewRotation);
			UE_LOG(LogTurnManager, Log,
				TEXT("[OnMoveFinished] Actor %s rotated to Yaw=%.1f"),
				*GetNameSafe(Unit), TargetYaw);
		}

		Unit->SetActorLocation(SnappedLoc, false, nullptr, ETeleportType::TeleportPhysics);
		CachedFirstLoc = SnappedLoc;
	}

	UE_LOG(LogTurnManager, Log,
		TEXT("[MoveComplete] Unit %s reached destination, GA_MoveBase ending."),
		*GetNameSafe(Unit));

	// CodeRevision: INC-2025-00030-R3 (Add Barrier sync to GA_MoveBase) (2025-11-17 01:00)
	// P1: Fix turn hang - Complete the registered barrier action before ending the ability
	if (bBarrierRegistered && MoveActionId.IsValid())
	{
		if (UWorld* World = GetWorld())
		{
			if (UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>())
			{
				Barrier->CompleteAction(Unit, MoveTurnId, MoveActionId);
				UE_LOG(LogTurnManager, Log,
					TEXT("[GA_MoveBase] Barrier completed for %s (ActionId=%s)"),
					*GetNameSafe(Unit),
					*MoveActionId.ToString());
			}
		}

		bBarrierRegistered = false;
		MoveActionId.Invalidate();
		MoveTurnId = INDEX_NONE;
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

	// CodeRevision: INC-2025-00030-R1 (Migrate to UGridPathfindingSubsystem) (2025-11-16 23:55)
	UGridPathfindingSubsystem* Pathfinding = GetGridPathfindingSubsystem();
    if (!Pathfinding)
    {
        UE_LOG(LogTurnManager, Error, TEXT("[GA_MoveBase] StartMoveToCell failed: GridPathfindingSubsystem missing"));
        EndAbility(CachedSpecHandle, &CachedActorInfo, CachedActivationInfo, true, true);
        return;
    }

    const AGameTurnManagerBase* TurnManager = GetTurnManager();
    const bool bTargetReserved = (TurnManager && Unit)
        ? TurnManager->HasReservationFor(Unit, TargetCell)
        : false;

    // 笘・・笘・Phase 5: Collision Prevention - Require reservation (2025-11-09) 笘・・笘・
    // CRITICAL FIX: Units can ONLY move to cells reserved for them
    // This prevents overlapping by enforcing the reservation system
    if (!bTargetReserved)
    {
        // Fallback for non-turn-based scenarios (exploration mode, etc.)
        if (!IsTileWalkable(TargetCell))
        {
            UE_LOG(LogTurnManager, Warning,
                TEXT("[GA_MoveBase] Target cell (%d,%d) not reserved and not walkable - Move blocked"),
                TargetCell.X, TargetCell.Y);
            EndAbility(CachedSpecHandle, &CachedActorInfo, CachedActivationInfo, true, true);
            return;
        }

        // Allow movement but log warning if no reservation system is active
        UE_LOG(LogTurnManager, Verbose,
            TEXT("[GA_MoveBase] Target cell (%d,%d) not reserved but walkable - Allowing (exploration mode?)"),
            TargetCell.X, TargetCell.Y);
    }

	const float FixedZ = ComputeFixedZ(Unit, Pathfinding);
	FVector StartPos = SnapToCellCenterFixedZ(Unit->GetActorLocation(), FixedZ);
	Unit->SetActorLocation(StartPos, false, nullptr, ETeleportType::TeleportPhysics);
	const FIntPoint CurrentCell = Pathfinding->WorldToGrid(StartPos);
	const FVector EndPos = Pathfinding->GridToWorld(TargetCell, FixedZ);
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
		FVector Sample = FMath::Lerp(StartPos, EndPos, Alpha);
		Sample.Z = FixedZ;
		PathPoints.Add(Sample);
	}

	BindMoveFinishedDelegate();
	Unit->MoveUnit(PathPoints);
}

// CodeRevision: INC-2025-00030-R1 (Migrate to UGridPathfindingSubsystem) (2025-11-16 23:55)
FVector UGA_MoveBase::SnapToCellCenter(const FVector& WorldPos) const
{
	UGridPathfindingSubsystem* Pathfinding = GetGridPathfindingSubsystem();
	if (!Pathfinding)
	{
		return WorldPos;
	}

	const FIntPoint Cell = Pathfinding->WorldToGrid(WorldPos);
	return Pathfinding->GridToWorld(Cell, WorldPos.Z);
}

// CodeRevision: INC-2025-00030-R1 (Migrate to UGridPathfindingSubsystem) (2025-11-16 23:55)
FVector UGA_MoveBase::SnapToCellCenterFixedZ(const FVector& WorldPos, float FixedZ) const
{
	UGridPathfindingSubsystem* Pathfinding = GetGridPathfindingSubsystem();
	if (!Pathfinding)
	{
		return FVector(WorldPos.X, WorldPos.Y, FixedZ);
	}

	const FIntPoint Cell = Pathfinding->WorldToGrid(WorldPos);
	return Pathfinding->GridToWorld(Cell, FixedZ);
}

// CodeRevision: INC-2025-00030-R1 (Migrate to UGridPathfindingSubsystem) (2025-11-16 23:55)
float UGA_MoveBase::ComputeFixedZ(const AUnitBase* Unit, const UGridPathfindingSubsystem* Pathfinding) const
{
	const float HalfHeight = (Unit && Unit->GetCapsuleComponent())
		? Unit->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
		: 0.f;

	float PlaneZ = Pathfinding ? Pathfinding->GetNavPlaneZ() : 0.f;
	if (!Pathfinding && Unit)
	{
		PlaneZ = Unit->GetActorLocation().Z - HalfHeight;
	}

	return PlaneZ + HalfHeight;
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

// CodeRevision: INC-2025-00030-R1 (Migrate to UGridPathfindingSubsystem) (2025-11-16 23:55)
void UGA_MoveBase::DebugDumpAround(const FIntPoint& Center)
{
#if !UE_BUILD_SHIPPING
	UGridPathfindingSubsystem* Pathfinding = GetGridPathfindingSubsystem();
	if (!Pathfinding)
	{
		return;
	}

	for (int32 DeltaY = 1; DeltaY >= -1; --DeltaY)
	{
		FString Row;
		for (int32 DeltaX = -1; DeltaX <= 1; ++DeltaX)
		{
			const FIntPoint Cell(Center.X + DeltaX, Center.Y + DeltaY);
			// CodeRevision: INC-2025-00021-R1 (Replace IsCellWalkable with IsCellWalkableIgnoringActor - Phase 2.4) (2025-11-17 15:05)
			// Debug code: only terrain check needed
			const bool bWalkable = Pathfinding->IsCellWalkableIgnoringActor(Cell, nullptr);
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
