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
#include "Rogue/Character/UnitBase.h"
#include "Rogue/Grid/GridOccupancySubsystem.h"
#include "Rogue/Grid/GridPathfindingLibrary.h"
#include "Rogue/Turn/TurnActionBarrierSubsystem.h"
#include "Rogue/Utility/RogueGameplayTags.h"
#include "Turn/GameTurnManagerBase.h"
#include "Utility/PathFinderUtils.h"
#include "Utility/TurnCommandEncoding.h"

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

	// ActivationOwnedTags.AddTag(RogueGameplayTags::State_Action_InProgress.GetTag()); // Manually managed now
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

void UGA_MoveBase::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (ActorInfo && ActorInfo->IsNetAuthority())
	{
		if (UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get())
		{
			const FGameplayTag Tag = RogueGameplayTags::State_Action_InProgress;
			ASC->AddLooseGameplayTag(Tag);
			++InProgressStack;
			UE_LOG(LogTurnManager, Verbose, TEXT("[InProgress] %s ++ (ThisAbility=%d, Total=%d)"), *GetNameSafe(ActorInfo->AvatarActor.Get()), InProgressStack, ASC->GetTagCount(Tag));
		}
	}

	// ☁E�E☁ESparky診断�E�アビリチE��起動確誁E☁E�E☁E
	AActor* Avatar = GetAvatarActorFromActorInfo();
	int32 TeamId = -1;
	if (AUnitBase* Unit = Cast<AUnitBase>(Avatar))
	{
		TeamId = Unit->Team;
	}
	UE_LOG(LogTurnManager, Error,
		TEXT("[GA_MoveBase] ☁E�E☁EABILITY ACTIVATED: Actor=%s, Team=%d"),
		*GetNameSafe(Avatar), TeamId);

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

	// ★★★ TurnId を EventData.OptionalObject から取得（2025-11-09） ★★★
	if (const AGameTurnManagerBase* TurnManager = Cast<AGameTurnManagerBase>(TriggerEventData->OptionalObject.Get()))
	{
		MoveTurnId = TurnManager->GetCurrentTurnIndex();
		UE_LOG(LogTurnManager, Log,
			TEXT("[GA_MoveBase] TurnId retrieved from TurnManager: %d (Actor=%s)"),
			MoveTurnId, *GetNameSafe(Avatar));
	}
	else
	{
		// フォールバック: BarrierSubsystem から取得
		if (UTurnActionBarrierSubsystem* Barrier = GetBarrierSubsystem())
		{
			MoveTurnId = Barrier->GetCurrentTurnId();
			UE_LOG(LogTurnManager, Warning,
				TEXT("[GA_MoveBase] TurnId fallback from Barrier: %d (Actor=%s)"),
				MoveTurnId, *GetNameSafe(Avatar));
		}
		else
		{
			UE_LOG(LogTurnManager, Error,
				TEXT("[GA_MoveBase] Failed to retrieve TurnId - TurnManager and Barrier not available"));
			MoveTurnId = INDEX_NONE;
		}
	}

	// ★★★ Magnitude 検証（TurnCommandEncoding 範囲チェック） ★★★
	const int32 RawMagnitude = FMath::RoundToInt(TriggerEventData->EventMagnitude);
	if (RawMagnitude < TurnCommandEncoding::kDirBase)  // 1000未満は無効
	{
		UE_LOG(LogTurnManager, Error,
			TEXT("[GA_MoveBase] ❌ INVALID MAGNITUDE: %d (expected >= %d, got %.2f) - Check GameTurnManager encoding!"),
			RawMagnitude, TurnCommandEncoding::kDirBase, TriggerEventData->EventMagnitude);
		UE_LOG(LogTurnManager, Error,
			TEXT("[GA_MoveBase] Sender should use TurnCommandEncoding::PackDir() - ABORTING"));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	else
	{
		UE_LOG(LogTurnManager, Log,
			TEXT("[GA_MoveBase] ✅ Magnitude validated: %d (range OK)"), RawMagnitude);
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

	// ★★★ Token方式: 一度だけ登録（冪等） ★★★
	if (HasAuthority(&ActivationInfo))
	{
		if (UTurnActionBarrierSubsystem* Barrier = GetBarrierSubsystem())
		{
			// bBarrierRegistered は既存の RegisterBarrier() で設定されるため、
			// Token登録は常に実行する（別トラッキング）
			Barrier->RegisterActionOnce(GetAvatarActorFromActorInfo(), /*out*/BarrierToken);
			UE_LOG(LogTurnManager, Verbose,
				TEXT("[GA_MoveBase] Token registered: %s"), *BarrierToken.ToString());
		}
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (ASC)
	{
		int32 CountBefore = ASC->GetTagCount(RogueGameplayTags::State_Action_InProgress.GetTag());
		DIAG_LOG(Log, TEXT("[GA_MoveBase] ActivateAbility: InProgress count before=%d"), CountBefore);
		ASC->AddLooseGameplayTag(RogueGameplayTags::Event_Dungeon_Step);
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

	const float FixedZ = ComputeFixedZ(Unit, PathFinder);
	FVector CurrentLocation = SnapToCellCenterFixedZ(Avatar->GetActorLocation(), FixedZ);
	Avatar->SetActorLocation(CurrentLocation, false, nullptr, ETeleportType::TeleportPhysics);
	CachedStartLocWS = CurrentLocation;
	CachedFirstLoc = CurrentLocation;
	const FIntPoint CurrentCell = PathFinder->WorldToGrid(CurrentLocation);

	// ★★★ 予約セル取得（SSOT: Single Source of Truth） ★★★
	FIntPoint ReservedCell(-1, -1);
	if (UWorld* World = GetWorld())
	{
		if (UGridOccupancySubsystem* Occupancy = World->GetSubsystem<UGridOccupancySubsystem>())
		{
			ReservedCell = Occupancy->GetReservedCellForActor(Avatar);
		}
	}

	FVector2D Dir2D(EncodedDirection.X, EncodedDirection.Y);
	if (!Dir2D.Normalize())
	{
		UE_LOG(LogTurnManager, Warning, TEXT("[GA_MoveBase] Direction vector length is zero"));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	FIntPoint Step(
		FMath::RoundToInt(Dir2D.X),
		FMath::RoundToInt(Dir2D.Y));

	bool bAxisAdjusted = false;
	Step = GA_MoveBase_Private::QuantizeStepToGrid(Step, Dir2D, bAxisAdjusted);

	if (Step == FIntPoint::ZeroValue)
	{
		UE_LOG(LogTurnManager, Warning, TEXT("[GA_MoveBase] Normalized direction truncated to zero step"));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (bAxisAdjusted)
	{
		UE_LOG(LogTurnManager, VeryVerbose,
			TEXT("[GA_MoveBase] Direction quantized to grid: Step=(%d,%d)"),
			Step.X, Step.Y);
	}

	// ★★★ 予約セル優先、なければ計算値を使用（フォールバック） ★★★
	const FIntPoint CalculatedCell = CurrentCell + Step;
	const FIntPoint NextCell = (ReservedCell.X >= 0 && ReservedCell.Y >= 0) ? ReservedCell : CalculatedCell;

	// ★★★ NextCellをキャッシュ（OnMoveFinishedで使用） ★★★
	CachedNextCell = NextCell;

	// ★★★ デバッグログ：予約 vs 計算 vs 実際の移動先 ★★★
	UE_LOG(LogTurnManager, Warning,
		TEXT("[GA_MoveBase] From=%s ReservedCell=%s CalculatedCell=%s NextCell=%s (using %s)"),
		*CurrentCell.ToString(),
		*ReservedCell.ToString(),
		*CalculatedCell.ToString(),
		*NextCell.ToString(),
		(ReservedCell.X >= 0 && ReservedCell.Y >= 0) ? TEXT("RESERVED") : TEXT("CALCULATED"));

	if (const AGameTurnManagerBase* TurnManager = GetTurnManager())
	{
		if (!TurnManager->IsMoveAuthorized(Unit, NextCell))
		{
			UE_LOG(LogTurnManager, Warning,
				TEXT("[GA_MoveBase] ❌ AUTHORIZATION FAILED: %s → (%d,%d) | Reserved=%s Calculated=%s"),
				*GetNameSafe(Unit),
				NextCell.X, NextCell.Y,
				*ReservedCell.ToString(),
				*CalculatedCell.ToString());
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}
		else
		{
			UE_LOG(LogTurnManager, Log,
				TEXT("[GA_MoveBase] ✅ AUTHORIZED: %s → (%d,%d)"),
				*GetNameSafe(Unit), NextCell.X, NextCell.Y);
		}
	}

	if (!PathFinder->IsCellWalkableIgnoringActor(NextCell, Unit))
	{
		UE_LOG(LogTurnManager, Warning,
			TEXT("[GA_MoveBase] Cell (%d,%d) is blocked by terrain; aborting move"),
			NextCell.X, NextCell.Y);
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	const FVector2D StepDir2D(Step.X, Step.Y);
	const FVector LocalTarget = CalculateNextTilePosition(CurrentLocation, StepDir2D);

	NextTileStep = SnapToCellCenterFixedZ(LocalTarget, FixedZ);
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
		UE_LOG(LogTurnManager, Verbose, TEXT("[GA_MoveBase] Zero distance move detected  Eending ability"));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}

	const float DesiredRotation = RoundYawTo45Degrees(
		FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(Step.Y), static_cast<float>(Step.X))));
	DesiredYaw = DesiredRotation;

	const FRotator TargetRotator(0.f, DesiredYaw, 0.f);
	Avatar->SetActorRotation(TargetRotator, ETeleportType::TeleportPhysics);

	// Walkability check
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
		UE_LOG(LogTurnManager, Warning, TEXT("[GA_MoveBase] Unit cast failed  Efinishing immediately"));
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

	if (ActorInfo && ActorInfo->IsNetAuthority())
	{
		if (UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get())
		{
			const FGameplayTag Tag = RogueGameplayTags::State_Action_InProgress;
			while (InProgressStack > 0)
			{
				ASC->RemoveLooseGameplayTag(Tag);
				--InProgressStack;
			}
			UE_LOG(LogTurnManager, Verbose, TEXT("[InProgress] %s -- (Cancelled, ThisAbility=0, Total=%d)"), *GetNameSafe(ActorInfo->AvatarActor.Get()), ASC->GetTagCount(Tag));
		}
	}

	Super::CancelAbility(Handle, ActorInfo, ActivationInfo, bReplicateCancelAbility);
}

void UGA_MoveBase::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	if (ActorInfo && ActorInfo->IsNetAuthority())
	{
		if (UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get())
		{
			const FGameplayTag Tag = RogueGameplayTags::State_Action_InProgress;
			while (InProgressStack > 0)
			{
				ASC->RemoveLooseGameplayTag(Tag);
				--InProgressStack;
			}
			UE_LOG(LogTurnManager, Verbose, TEXT("[InProgress] %s -- (ThisAbility=0, Total=%d)"), *GetNameSafe(ActorInfo->AvatarActor.Get()), ASC->GetTagCount(Tag));
		}
	}

	// ☁E�E☁ESparky修正: 再�E防止 ☁E�E☁E
	if (bIsEnding)
	{
		UE_LOG(LogTurnManager, Error, TEXT("[GA_MoveBase] ☁E�E☁EAlready ending, ignoring recursive call"));
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
		ASC->RemoveLooseGameplayTag(RogueGameplayTags::Event_Dungeon_Step);
	}

	// ☁E�E☁ESparky修正: 状態を保存してからBarrierに通知�E�クリア前に�E�E��E☁E�E☁E
	const int32 SavedTurnId = MoveTurnId;
	const FGuid SavedActionId = MoveActionId;
	AActor* SavedAvatar = GetAvatarActorFromActorInfo();

	// ☁E�E☁E修正: Barrierに通知�E�新ActionID APIのみ使用�E�E☁E�E☁E
	if (UWorld* World = GetWorld())
	{
		if (UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>())
		{
			if (SavedAvatar && SavedTurnId != INDEX_NONE && SavedActionId.IsValid())
			{
				Barrier->CompleteAction(SavedAvatar, SavedTurnId, SavedActionId);
				// ☁E�E☁EレガシーAPI�E�EotifyMoveFinished�E��E削除 - 二重通知を防止 ☁E�E☁E

				UE_LOG(LogTurnManager, Log,
					TEXT("[GA_MoveBase] Barrier notified: Actor=%s, TurnId=%d, ActionId=%s"),
					*GetNameSafe(SavedAvatar), SavedTurnId, *SavedActionId.ToString());
			}

			// ★★★ Token方式: 冪等Complete ★★★
			if (HasAuthority(&ActivationInfo) && BarrierToken.IsValid())
			{
				Barrier->CompleteActionToken(BarrierToken);
				UE_LOG(LogTurnManager, Verbose,
					TEXT("[GA_MoveBase] Token completed: %s"), *BarrierToken.ToString());
				BarrierToken.Invalidate();
			}
		}
	}

	// ☁E�E☁ESparky修正: CompletedTurnIdForEventを設定！EendCompletionEventで使用�E�E☁E�E☁E
	CompletedTurnIdForEvent = SavedTurnId;

	// ☁E�E☁E重要E Super::EndAbility()の前にMoveTurnIdをクリアしなぁE��E☁E�E☁E
	// 基底クラスがSendCompletionEventを呼ぶ可能性があめE

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);

	// ☁E�E☁ESparky修正: Super::EndAbility()の後にクリア ☁E�E☁E
	MoveTurnId = INDEX_NONE;
	MoveActionId.Invalidate();
	CompletedTurnIdForEvent = INDEX_NONE;
	bBarrierRegistered = false; // ☁E�E☁EHotfix: 次回�EアビリチE��起動で再登録可能にする ☁E�E☁E

	bIsEnding = false;
}

void UGA_MoveBase::SendCompletionEvent(bool bTimedOut)
{
    if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
    {
        const int32 NotifiedTurnId = (CompletedTurnIdForEvent != INDEX_NONE)
            ? CompletedTurnIdForEvent
            : MoveTurnId;

        // ☁E�E☁ESparky修正: 無効なTurnIdでイベントを送信しなぁE☁E�E☁E
        if (NotifiedTurnId == INDEX_NONE || NotifiedTurnId < 0)
        {
            UE_LOG(LogTurnManager, Error,
                TEXT("[SendCompletionEvent] ☁E�E☁EINVALID TurnId=%d, NOT sending completion event! (Actor=%s)"),
                NotifiedTurnId, *GetNameSafe(GetAvatarActorFromActorInfo()));
            return;  // イベント送信を中止
        }

        UE_LOG(LogTurnManager, Warning,
            TEXT("[TurnNotify] GA_MoveBase completion: Actor=%s TurnId=%d"),
            *GetNameSafe(GetAvatarActorFromActorInfo()), NotifiedTurnId);

        FGameplayEventData EventData;
        EventData.Instigator = GetAvatarActorFromActorInfo();
        EventData.OptionalObject = this;
        EventData.EventMagnitude = static_cast<float>(NotifiedTurnId);  // ☁E�E☁ESparky修正: 直接使用�E�EMath::Maxなし！E☁E�E☁E
        EventData.EventTag = RogueGameplayTags::Gameplay_Event_Turn_Ability_Completed;

        ASC->HandleGameplayEvent(RogueGameplayTags::Gameplay_Event_Turn_Ability_Completed, &EventData);

        EventData.EventTag = RogueGameplayTags::Ability_Move_Completed;
        ASC->HandleGameplayEvent(RogueGameplayTags::Ability_Move_Completed, &EventData);
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

bool UGA_MoveBase::IsTileWalkable(const FVector& TilePosition, AUnitBase* Self)
{
    const AGridPathfindingLibrary* PathFinder = GetPathFinder();
    if (!PathFinder)
    {
        return false;
    }

    const FIntPoint Cell = PathFinder->WorldToGrid(TilePosition);
    AActor* ActorToIgnore = Self ? static_cast<AActor*>(Self) : const_cast<AActor*>(GetAvatarActorFromActorInfo());
    return PathFinder->IsCellWalkableIgnoringActor(Cell, ActorToIgnore);
}

bool UGA_MoveBase::IsTileWalkable(const FIntPoint& Cell) const
{
    const AGridPathfindingLibrary* PathFinder = GetPathFinder();
    if (!PathFinder)
    {
        return false;
    }

    AActor* IgnoreActor = const_cast<AActor*>(GetAvatarActorFromActorInfo());
    if (!PathFinder->IsCellWalkableIgnoringActor(Cell, IgnoreActor))
    {
        UE_LOG(LogTurnManager, Verbose,
            TEXT("[IsTileWalkable] Cell (%d,%d) blocked for %s."),
            Cell.X, Cell.Y, *GetNameSafe(IgnoreActor));
        return false;
    }

    return true;
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
			// ★★★ DEBUG: Log occupancy update (2025-11-09) ★★★
			const FIntPoint OldCell = Occupancy->GetCellOfActor(UnitActor);
			UE_LOG(LogTurnManager, Warning,
				TEXT("[UpdateOccupancy] ★ Actor=%s | OLD=(%d,%d) → NEW=(%d,%d)"),
				*GetNameSafe(UnitActor), OldCell.X, OldCell.Y, NewCell.X, NewCell.Y);

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
	// ★★★ 最適化: PathFinderUtils使用（重複コード削除）
	if (const UWorld* World = GetWorld())
	{
		return FPathFinderUtils::GetCachedPathFinder(
			const_cast<UWorld*>(World),
			const_cast<TWeakObjectPtr<AGridPathfindingLibrary>*>(&CachedPathFinder)
		);
	}

	return nullptr;
}

UTurnActionBarrierSubsystem* UGA_MoveBase::GetBarrierSubsystem() const
{
	if (CachedBarrier.IsValid())
	{
		return CachedBarrier.Get();
	}

	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Avatar)
	{
		return nullptr;
	}

	if (UWorld* World = Avatar->GetWorld())
	{
		CachedBarrier = World->GetSubsystem<UTurnActionBarrierSubsystem>();
		return CachedBarrier.Get();
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
	if (MoveFinishedHandle.IsValid())
	{
		UE_LOG(LogTurnManager, Warning,
			TEXT("[BindMoveFinishedDelegate] %s: Already bound, skipping"),
			*GetNameSafe(GetAvatarActorFromActorInfo()));
		return;
	}

	if (AUnitBase* Unit = Cast<AUnitBase>(GetAvatarActorFromActorInfo()))
	{
		MoveFinishedHandle = Unit->OnMoveFinished.AddUObject(this, &UGA_MoveBase::OnMoveFinished);

		// ☁E�E☁ESparky診断�E�デリゲートバインド�E功確誁E☁E�E☁E
		UE_LOG(LogTurnManager, Error,
			TEXT("[BindMoveFinishedDelegate] ☁E�E☁ESUCCESS: Unit %s delegate bound to GA_MoveBase::OnMoveFinished (Handle IsValid=%d)"),
			*GetNameSafe(Unit), MoveFinishedHandle.IsValid() ? 1 : 0);
	}
	else
	{
		// ☁E�E☁ESparky診断�E�キャスト失敁E☁E�E☁E
		UE_LOG(LogTurnManager, Error,
			TEXT("[BindMoveFinishedDelegate] ☁E�E☁EFAILED: Avatar is not a UnitBase! Avatar=%s"),
			*GetNameSafe(GetAvatarActorFromActorInfo()));
	}
}

void UGA_MoveBase::OnMoveFinished(AUnitBase* Unit)
{
    // ☁E�E☁E二重通知防止: Barrier通知は EndAbility() でのみ行う ☁E�E☁E
    // OnMoveFinished ↁEEndAbility の頁E��呼ばれるため、ここでは Barrier を触らなぁE

    // TurnId変更の検�E�E�デバッグログのみ�E�E
    if (UWorld* World = GetWorld())
    {
        if (UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>())
        {
            const int32 CurrentTurnId = Barrier->GetCurrentTurnId();
            if (MoveTurnId != INDEX_NONE && MoveTurnId != CurrentTurnId)
            {
                UE_LOG(LogTurnManager, Warning,
                    TEXT("[OnMoveFinished] TurnId mismatch detected: MoveTurnId=%d, CurrentTurnId=%d (Actor=%s)"),
                    MoveTurnId, CurrentTurnId, *GetNameSafe(Unit));
                UE_LOG(LogTurnManager, Warning,
                    TEXT("[OnMoveFinished] Barrier notification will be handled by EndAbility() with TurnId=%d"),
                    MoveTurnId);
            }
        }
    }

    int32 TagCountBefore = -1;
    int32 TagCountAfter = -1;

    const AGridPathfindingLibrary* PathFinder = GetPathFinder();
    if (Unit)
    {
        // ★★★ DEBUG: Log position BEFORE setting (2025-11-09) ★★★
        const FVector LocationBefore = Unit->GetActorLocation();
        const FIntPoint GridBefore = PathFinder ? PathFinder->WorldToGrid(LocationBefore) : FIntPoint::ZeroValue;

        // ★★★ FIX: PathFinder->GridToWorld()で移動先セルの中心座標を計算 ★★★
        const float FixedZ = ComputeFixedZ(Unit, PathFinder);
        const FVector DestWorldLoc = PathFinder ? PathFinder->GridToWorld(CachedNextCell) : Unit->GetActorLocation();
        const FVector SnappedLoc = SnapToCellCenterFixedZ(DestWorldLoc, FixedZ);
        Unit->SetActorLocation(SnappedLoc, false, nullptr, ETeleportType::TeleportPhysics);

        // ★★★ DEBUG: Log position AFTER setting (2025-11-09) ★★★
        const FVector LocationAfter = Unit->GetActorLocation();
        const FIntPoint GridAfter = PathFinder ? PathFinder->WorldToGrid(LocationAfter) : FIntPoint::ZeroValue;

        UE_LOG(LogTurnManager, Warning,
            TEXT("[OnMoveFinished] ★ POSITION UPDATE: Actor=%s | BEFORE=Grid(%d,%d) World(%s) | DestCell=(%d,%d) | AFTER=Grid(%d,%d) World(%s) | TurnId=%d"),
            *GetNameSafe(Unit),
            GridBefore.X, GridBefore.Y, *LocationBefore.ToCompactString(),
            CachedNextCell.X, CachedNextCell.Y,
            GridAfter.X, GridAfter.Y, *LocationAfter.ToCompactString(),
            MoveTurnId);

        CachedFirstLoc = SnappedLoc;
        // ★★★ REMOVED: UpdateGridState(CachedFirstLoc, 1); ← レガシーコード削除 (2025-11-09)
        // このレガシー呼び出しがWalkable値3を占有値1に上書きし、次ターンで地形ブロック判定を破壊していた。
        // 占有管理はUpdateOccupancy（OccupancySubsystem）で既に行われているため、この呼び出しは冗長かつ有害。
        // GridCostはあくまで「地形の通行コスト/Walkable状態」を表し、占有状態とは別チャンネルで管理すべき。
        // UpdateGridState(CachedFirstLoc, 1);  // ← LEGACY CODE REMOVED

        UE_LOG(LogTurnManager, Log,
            TEXT("[MoveComplete] Unit %s reached destination, GA_MoveBase ending (TurnId=%d)"),
            *GetNameSafe(Unit), MoveTurnId);
    }

    if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
    {
        TagCountBefore = ASC->GetTagCount(RogueGameplayTags::State_Action_InProgress.GetTag());
    }

    EndAbility(CachedSpecHandle, &CachedActorInfo, CachedActivationInfo, true, false);

    if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
    {
        TagCountAfter = ASC->GetTagCount(RogueGameplayTags::State_Action_InProgress.GetTag());
    }

    UE_LOG(LogTurnManager, Verbose,
        TEXT("[MoveComplete] InProgress tag count (Actor=%s): %d -> %d"),
        *GetNameSafe(Unit), TagCountBefore, TagCountAfter);
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

    const AGameTurnManagerBase* TurnManager = GetTurnManager();
    const bool bTargetReserved = (TurnManager && Unit)
        ? TurnManager->HasReservationFor(Unit, TargetCell)
        : false;

    // ★★★ Phase 5: Collision Prevention - Require reservation (2025-11-09) ★★★
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

	const float FixedZ = ComputeFixedZ(Unit, PathFinder);
	FVector StartPos = SnapToCellCenterFixedZ(Unit->GetActorLocation(), FixedZ);
	Unit->SetActorLocation(StartPos, false, nullptr, ETeleportType::TeleportPhysics);
	const FIntPoint CurrentCell = PathFinder->WorldToGrid(StartPos);
	const FVector EndPos = PathFinder->GridToWorld(TargetCell, FixedZ);
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

FVector UGA_MoveBase::SnapToCellCenterFixedZ(const FVector& WorldPos, float FixedZ) const
{
	const AGridPathfindingLibrary* PathFinder = GetPathFinder();
	if (!PathFinder)
	{
		return FVector(WorldPos.X, WorldPos.Y, FixedZ);
	}

	const FIntPoint Cell = PathFinder->WorldToGrid(WorldPos);
	return PathFinder->GridToWorld(Cell, FixedZ);
}

float UGA_MoveBase::ComputeFixedZ(const AUnitBase* Unit, const AGridPathfindingLibrary* PathFinder) const
{
	const float HalfHeight = (Unit && Unit->GetCapsuleComponent())
		? Unit->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
		: 0.f;

	float PlaneZ = PathFinder ? PathFinder->GetNavPlaneZ() : 0.f;
	if (!PathFinder && Unit)
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

	// ☁E�E☁EHotfix: 二重登録防止ガーチE☁E�E☁E
	if (bBarrierRegistered)
	{
		UE_LOG(LogTurnManager, Warning,
			TEXT("[GA_MoveBase] RegisterBarrier called again for %s - already registered with ActionId=%s"),
			*Avatar->GetName(), *MoveActionId.ToString());
		return true; // 既に登録済みなのでtrueを返す
	}

	if (UWorld* World = Avatar->GetWorld())
	{
		if (UTurnActionBarrierSubsystem* Barrier = World->GetSubsystem<UTurnActionBarrierSubsystem>())
		{
			// ★★★ TurnId が未設定の場合のみフォールバック取得（2025-11-09） ★★★
			if (MoveTurnId == INDEX_NONE)
			{
				MoveTurnId = Barrier->GetCurrentTurnId();
				UE_LOG(LogTurnManager, Warning,
					TEXT("[RegisterBarrier] TurnId fallback: %d (Actor=%s)"),
					MoveTurnId, *Avatar->GetName());
			}

			MoveActionId = Barrier->RegisterAction(Avatar, MoveTurnId);
			bBarrierRegistered = true;
			UE_LOG(LogTurnManager, Log,
				TEXT("[RegisterBarrier] Registered: TurnId=%d, ActionId=%s (Actor=%s)"),
				MoveTurnId, *MoveActionId.ToString(), *Avatar->GetName());
			return true;
		}
	}

	return false;
}

