// Copyright Epic Games, Inc. All Rights Reserved.

// TurnCorePhaseManager.cpp
#include "TurnCorePhaseManager.h"
#include "DistanceFieldSubsystem.h"
#include "ConflictResolverSubsystem.h"
#include "StableActorRegistry.h"
#include "Turn/GameTurnManagerBase.h"
#include "TurnSystemTypes.h"
#include "../Grid/GridOccupancySubsystem.h"
#include "../Utility/TurnCommandEncoding.h"
#include "UObject/UnrealType.h"
#include "../AI/Enemy/EnemyThinkerBase.h"
#include "GameplayTagsManager.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemGlobals.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Controller.h"
#include "Character/LyraCharacter.h"
#include "Character/LyraPawnExtensionComponent.h"
#include "AbilitySystem/LyraAbilitySystemComponent.h"
#include "../Utility/RogueGameplayTags.h"
#include "EngineUtils.h"


DEFINE_LOG_CATEGORY(LogTurnCore);

namespace TurnCorePhaseManagerPrivate
{
    static FGameplayTag MapIntentToGameplayEvent(const FGameplayTag& IntentTag)
    {
        const FGameplayTag MoveIntent = RogueGameplayTags::AI_Intent_Move;
        const FGameplayTag AttackIntent = RogueGameplayTags::AI_Intent_Attack;
        const FGameplayTag WaitIntent = RogueGameplayTags::AI_Intent_Wait;

        if (IntentTag.MatchesTag(MoveIntent))
        {
            return RogueGameplayTags::GameplayEvent_Intent_Move;
        }
        if (IntentTag.MatchesTag(AttackIntent))
        {
            return RogueGameplayTags::GameplayEvent_Intent_Attack;
        }
        if (IntentTag.MatchesTag(WaitIntent))
        {
            return RogueGameplayTags::GameplayEvent_Intent_Wait;
        }

        return IntentTag;
    }
}

static int32 ReadGenerationOrderFromBlueprint(const AActor* Actor)
{
    static const FName GenName(TEXT("GenerationOrder"));

    if (!Actor)
    {
        return TNumericLimits<int32>::Max();
    }

    if (const FIntProperty* Prop = FindFProperty<FIntProperty>(Actor->GetClass(), GenName))
    {
        if (const int32* ValuePtr = Prop->ContainerPtrToValuePtr<int32>(Actor))
        {
            int32 Order = *ValuePtr;
            return Order >= 0 ? Order : TNumericLimits<int32>::Max();
        }
    }

    return TNumericLimits<int32>::Max();
}

void UTurnCorePhaseManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    static const FName RequiredTags[] = {
        FName("AI.Intent.Move"),
        FName("AI.Intent.Attack"),
        FName("AI.Intent.Wait"),
        FName("AI.Intent.Dash")
    };

    for (const FName& TagName : RequiredTags)
    {
        const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TagName, false);
        if (!ensureMsgf(Tag.IsValid(), TEXT("[TurnCore] Missing required GameplayTag: %s"), *TagName.ToString()))
        {
            UE_LOG(LogTemp, Error, TEXT("[TurnCore] Please add tag '%s' to DefaultGameplayTags.ini"), *TagName.ToString());
        }
    }

    ConflictResolver = GetWorld()->GetSubsystem<UConflictResolverSubsystem>();
    DistanceField = GetWorld()->GetSubsystem<UDistanceFieldSubsystem>();
    ActorRegistry = GetWorld()->GetSubsystem<UStableActorRegistry>();

    UE_LOG(LogTemp, Log, TEXT("[TurnCore] Initialized"));
}

void UTurnCorePhaseManager::Deinitialize()
{
    UE_LOG(LogTemp, Log, TEXT("[TurnCore] Deinitialized"));
    Super::Deinitialize();
}

// ????????????????????????????????????????????????????????????????????????
// Core Phase Pipeline
// ????????????????????????????????????????????????????????????????????????

void UTurnCorePhaseManager::CoreObservationPhase(const FIntPoint& PlayerCell)
{
    UE_LOG(LogTurnCore, Warning,
        TEXT("[TurnCore] CoreObservationPhase received PlayerCell=(%d, %d)"),
        PlayerCell.X, PlayerCell.Y);

    if (!DistanceField)
    {
        UE_LOG(LogTemp, Error, TEXT("[TurnCore] DistanceField is null"));
        return;
    }

    if (UWorld* World = GetWorld())
    {
        if (UGridOccupancySubsystem* GridOccupancy = World->GetSubsystem<UGridOccupancySubsystem>())
        {
            if (AGameTurnManagerBase* TurnMgr = ResolveTurnManager())
            {
                int32 CurrentTurnId = TurnMgr->GetCurrentTurnIndex();
                GridOccupancy->SetCurrentTurnId(CurrentTurnId);
                GridOccupancy->PurgeOutdatedReservations(CurrentTurnId);
                UE_LOG(LogTemp, Log, TEXT("[TurnCore] ObservationPhase: Purged outdated reservations for turn %d"), CurrentTurnId);
            }
        }
    }

    DistanceField->UpdateDistanceField(PlayerCell);
    UE_LOG(LogTemp, Log, TEXT("[TurnCore] ObservationPhase: Complete"));
}

TArray<FEnemyIntent> UTurnCorePhaseManager::CoreThinkPhase(const TArray<AActor*>& Enemies)
{
    TArray<FEnemyIntent> Intents;
    Intents.Reserve(Enemies.Num());

    for (AActor* Enemy : Enemies)
    {
        if (!Enemy)
        {
            continue;
        }

        UEnemyThinkerBase* Thinker = Enemy->FindComponentByClass<UEnemyThinkerBase>();
        if (!Thinker)
        {
            UE_LOG(LogTemp, Warning, TEXT("[TurnCore] Enemy %s has no EnemyThinkerBase component"), *Enemy->GetName());
            continue;
        }

        FEnemyIntent Intent = Thinker->DecideIntent();
        Intents.Add(Intent);
    }

    UE_LOG(LogTemp, Log, TEXT("[TurnCore] ThinkPhase: Collected %d intents"), Intents.Num());
    return Intents;
}

TArray<FResolvedAction> UTurnCorePhaseManager::CoreResolvePhase(const TArray<FEnemyIntent>& Intents)
{
    if (!ConflictResolver || !ActorRegistry || !DistanceField)
    {
        UE_LOG(LogTemp, Error, TEXT("[TurnCore] Required subsystems are null"));
        return TArray<FResolvedAction>();
    }

    UGridOccupancySubsystem* GridOccupancy = GetWorld()->GetSubsystem<UGridOccupancySubsystem>();

    if (GridOccupancy)
    {
        int32 CurrentTurnId = 0;
        if (AGameTurnManagerBase* TurnManager = ResolveTurnManager())
        {
            CurrentTurnId = TurnManager->GetCurrentTurnIndex();
        }

        GridOccupancy->SetCurrentTurnId(CurrentTurnId);
        GridOccupancy->PurgeOutdatedReservations(CurrentTurnId);
    }

    if (GridOccupancy)
    {
        GridOccupancy->BeginMovePhase();
    }
    auto GetLiveCell = [&](AActor* A) -> FIntPoint
        {
            if (GridOccupancy)
            {
                FIntPoint Cell = GridOccupancy->GetCellOfActor(A);
                if (Cell != FIntPoint::ZeroValue)
                {
                    return Cell;
                }
            }
            return FIntPoint::ZeroValue;
        };

    ConflictResolver->ClearReservations();

    for (const FEnemyIntent& Intent : Intents)
    {
        if (!Intent.Actor.IsValid())
        {
            continue;
        }

        AActor* Actor = Intent.Actor.Get();
        if (!IsValid(Actor) || Actor->IsActorBeingDestroyed())
        {
            UE_LOG(LogTemp, Log, TEXT("[TurnCore] Actor %s is dead/destroyed, skipping"),
                *GetNameSafe(Actor));
            continue;
        }

        FStableActorID ActorID = ActorRegistry->GetStableID(Actor);

        FIntPoint LiveCurrentCell = GetLiveCell(Actor);
        if (LiveCurrentCell == FIntPoint::ZeroValue)
        {
            LiveCurrentCell = Intent.CurrentCell;
        }

        FIntPoint ResolvedNextCell = Intent.NextCell;
        if (Intent.AbilityTag.MatchesTag(RogueGameplayTags::AI_Intent_Attack))
        {
            ResolvedNextCell = LiveCurrentCell;
            UE_LOG(LogTemp, Verbose,
                TEXT("[TurnCore] Attack intent detected: %s stays at (%d,%d) (original NextCell=(%d,%d))"),
                *GetNameSafe(Actor), LiveCurrentCell.X, LiveCurrentCell.Y, Intent.NextCell.X, Intent.NextCell.Y);
        }

        int32 CurrentDist = DistanceField->GetDistance(LiveCurrentCell);
        int32 NextDist = DistanceField->GetDistance(ResolvedNextCell);
        int32 DistanceReduction = FMath::Max(0, CurrentDist - NextDist);

        FReservationEntry Entry;
        Entry.Actor = Actor;
        Entry.ActorID = ActorID;
        Entry.TimeSlot = Intent.TimeSlot;
        Entry.CurrentCell = LiveCurrentCell;
        Entry.Cell = ResolvedNextCell;
        Entry.AbilityTag = Intent.AbilityTag;
        Entry.ActionTier = 1;
        Entry.BasePriority = 100;
        Entry.DistanceReduction = DistanceReduction;
        Entry.GenerationOrder = ReadGenerationOrderFromBlueprint(Actor);

        ConflictResolver->AddReservation(Entry);
    }

    TArray<FResolvedAction> Resolved = ConflictResolver->ResolveAllConflicts();

    uint32 TurnHash = 2166136261u;
    auto Mix = [&](uint32 Value) { TurnHash ^= Value; TurnHash *= 16777619u; };

    for (const FResolvedAction& Action : Resolved)
    {
        Mix((uint32)Action.GenerationOrder);
        Mix(GetTypeHash(Action.ActorID.PersistentGUID.A));
        Mix(GetTypeHash(Action.NextCell.X));
        Mix(GetTypeHash(Action.NextCell.Y));
        Mix((uint32)Action.AllowedDashSteps);
        Mix(GetTypeHash(Action.FinalAbilityTag));
        Mix((uint32)Action.TimeSlot);
    }

    UE_LOG(LogTemp, Log, TEXT("[TurnCore] ResolvePhase: Resolved %d actions, Hash=0x%08X"),
        Resolved.Num(), TurnHash);

    if (AGameTurnManagerBase* TurnManager = ResolveTurnManager())
    {
        for (FResolvedAction& Action : Resolved)
        {
            if (!Action.bIsWait)
            {
                const bool bReserved = TurnManager->RegisterResolvedMove(Action.SourceActor.Get(), Action.NextCell);
                if (!bReserved)
                {
                    UE_LOG(LogTemp, Error,
                        TEXT("[TurnCore] RegisterResolvedMove FAILED for %s -> (%d,%d) - MARKING AS WAIT (no movement)"),
                        *GetNameSafe(Action.SourceActor.Get()), Action.NextCell.X, Action.NextCell.Y);

                    Action.bIsWait = true;
                }
            }
            else
            {
                UE_LOG(LogTemp, Verbose,
                    TEXT("[TurnCore] Skip reservation for loser: %s (bIsWait=true)"),
                    *GetNameSafe(Action.SourceActor.Get()));
            }
        }
    }

    return Resolved;
}

// TurnCorePhaseManager.cpp

// TurnCorePhaseManager.cpp
void UTurnCorePhaseManager::CoreExecutePhase(const TArray<FResolvedAction>& ResolvedActions)
{
    for (const FResolvedAction& Action : ResolvedActions)
    {
        if (!Action.SourceActor)
        {
            UE_LOG(LogTurnCore, Error, TEXT("[Execute] Skip: SourceActor is None"));
            continue;
        }

        if (Action.bIsWait)
        {
            UE_LOG(LogTurnCore, Verbose,
                TEXT("[Execute] Skip: Actor=%s marked as bIsWait=true (loser or reservation failed)"),
                *GetNameSafe(Action.SourceActor.Get()));
            continue;
        }

        bool bHandledByTurnManager = false;
        if (AGameTurnManagerBase* TurnManager = ResolveTurnManager())
        {
            bHandledByTurnManager = TurnManager->DispatchResolvedMove(Action);
        }

        if (bHandledByTurnManager)
        {
            continue;
        }
        UAbilitySystemComponent* ASC = ResolveASC(Action.SourceActor);
        if (!ASC)
        {
            UE_LOG(LogTurnCore, Error, TEXT("[Execute] ASC not found for %s"),
                *GetNameSafe(Action.SourceActor));
            continue;
        }

        // C++ event sending
        FGameplayEventData EventData;
        const FGameplayTag EventTag = TurnCorePhaseManagerPrivate::MapIntentToGameplayEvent(Action.FinalAbilityTag);
        EventData.EventTag = EventTag;
        EventData.Instigator = Action.SourceActor;
        EventData.Target = Action.SourceActor;

        if (Action.NextCell != FIntPoint(-1, -1))
        {
            const int32 EncodedCell = TurnCommandEncoding::PackCell(Action.NextCell.X, Action.NextCell.Y);
            EventData.EventMagnitude = static_cast<float>(EncodedCell);

            UE_LOG(LogTurnCore, Log,
                TEXT("[Execute] Encoded NextCell: (%d,%d) -> Magnitude=%.0f"),
                Action.NextCell.X, Action.NextCell.Y, EventData.EventMagnitude);
        }

        UE_LOG(LogTurnCore, Log,
            TEXT("[Execute] Sending ability event: Intent=%s, Event=%s, Actor=%s, Cell=(%d,%d)"),
            *Action.FinalAbilityTag.ToString(),
            *EventTag.ToString(),
            *GetNameSafe(Action.SourceActor),
            Action.NextCell.X, Action.NextCell.Y);

        // Dump the ASC's active tags before the event to trace blocking tags.
        FGameplayTagContainer OwnedTags;
        ASC->GetOwnedGameplayTags(OwnedTags);
        UE_LOG(LogTurnCore, Log,
            TEXT("[Execute] DEBUG: Owned Tags BEFORE HandleGameplayEvent: %s"),
            *OwnedTags.ToStringSimple());

        int32 NumActivated = ASC->HandleGameplayEvent(EventTag, &EventData);

        UE_LOG(LogTurnCore, Log,
            TEXT("[Execute] Ability activation result: NumActivated=%d"),
            NumActivated);
    }

    UGridOccupancySubsystem* GridOccupancy = GetWorld()->GetSubsystem<UGridOccupancySubsystem>();
    if (GridOccupancy)
    {
        GridOccupancy->EndMovePhase();
    }
}



void UTurnCorePhaseManager::CoreCleanupPhase()
{
    if (ConflictResolver)
    {
        ConflictResolver->ClearReservations();
    }

    UE_LOG(LogTemp, Log, TEXT("[TurnCore] CleanupPhase: Complete"));
}

const FGameplayTag& UTurnCorePhaseManager::Tag_Move()
{
    static const FGameplayTag Tag = RogueGameplayTags::AI_Intent_Move;
    return Tag;
}

AGameTurnManagerBase* UTurnCorePhaseManager::ResolveTurnManager()
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

const FGameplayTag& UTurnCorePhaseManager::Tag_Attack()
{
    static const FGameplayTag Tag = RogueGameplayTags::AI_Intent_Attack;
    return Tag;
}

const FGameplayTag& UTurnCorePhaseManager::Tag_Wait()
{
    static const FGameplayTag Tag = RogueGameplayTags::AI_Intent_Wait;
    return Tag;
}

void UTurnCorePhaseManager::BucketizeIntentsBySlot(
    const TArray<FEnemyIntent>& All,
    TArray<TArray<FEnemyIntent>>& OutBuckets,
    int32& OutMaxSlot)
{
    OutMaxSlot = 0;
    OutBuckets.Reset();

    for (const FEnemyIntent& Intent : All)
    {
        if (Intent.TimeSlot > OutMaxSlot)
        {
            OutMaxSlot = Intent.TimeSlot;
        }
    }

    OutBuckets.SetNum(OutMaxSlot + 1);

    for (const FEnemyIntent& Intent : All)
    {
        OutBuckets[Intent.TimeSlot].Add(Intent);
    }
}

int32 UTurnCorePhaseManager::ExecuteMovePhaseWithSlots(
    const TArray<FEnemyIntent>& AllIntents,
    TArray<FResolvedAction>& OutActions)
{
    check(GetWorld() && GetWorld()->GetNetMode() != NM_Client);

    OutActions.Reset();
    OutActions.Reserve(AllIntents.Num());

    if (AllIntents.Num() == 0)
    {
        UE_LOG(LogTemp, Log, TEXT("[ExecuteMovePhaseWithSlots] No intents to process"));
        return 0;
    }

    TArray<TArray<FEnemyIntent>> Buckets;
    int32 MaxSlot = 0;
    BucketizeIntentsBySlot(AllIntents, Buckets, MaxSlot);

    UE_LOG(LogTemp, Log, TEXT("[ExecuteMovePhaseWithSlots] MaxTimeSlot = %d, Total Intents = %d"),
        MaxSlot, AllIntents.Num());

    for (int32 Slot = 0; Slot <= MaxSlot; ++Slot)
    {
        const TArray<FEnemyIntent>& SlotIntents = Buckets[Slot];

        if (SlotIntents.Num() == 0)
        {
            UE_LOG(LogTemp, Log, TEXT("[Move Slot %d] No intents, skipping"), Slot);
            continue;
        }

        UE_LOG(LogTemp, Log, TEXT("[Move Slot %d] Processing %d intents"),
            Slot, SlotIntents.Num());

        TArray<FResolvedAction> SlotActions = CoreResolvePhase(SlotIntents);

        for (FResolvedAction& Action : SlotActions)
        {
            Action.TimeSlot = Slot;
        }

        CoreExecutePhase(SlotActions);

        OutActions.Append(SlotActions);
    }

    UE_LOG(LogTemp, Log, TEXT("[ExecuteMovePhaseWithSlots] Complete. Total Actions = %d"),
        OutActions.Num());

    return MaxSlot;
}

int32 UTurnCorePhaseManager::ExecuteAttackPhaseWithSlots(
    const TArray<FEnemyIntent>& AllIntents,
    TArray<FResolvedAction>& OutActions)
{
    check(GetWorld() && GetWorld()->GetNetMode() != NM_Client);

    OutActions.Reset();

    if (AllIntents.Num() == 0)
    {
        UE_LOG(LogTemp, Log, TEXT("[ExecuteAttackPhaseWithSlots] No intents"));
        return 0;
    }

    const FGameplayTag& AttackTag = Tag_Attack();
    ensureMsgf(AttackTag.IsValid(), TEXT("Tag 'AI.Intent.Attack' is not registered."));

    TArray<TArray<FEnemyIntent>> Buckets;
    int32 MaxSlot = 0;
    BucketizeIntentsBySlot(AllIntents, Buckets, MaxSlot);

    UE_LOG(LogTemp, Log, TEXT("[ExecuteAttackPhaseWithSlots] MaxTimeSlot = %d"), MaxSlot);

    for (int32 Slot = 0; Slot <= MaxSlot; ++Slot)
    {
        const TArray<FEnemyIntent>& Src = Buckets[Slot];
        if (Src.Num() == 0) { continue; }

        TArray<FEnemyIntent> Attacks;
        Attacks.Reserve(Src.Num());

        for (const FEnemyIntent& Intent : Src)
        {
            if (Intent.AbilityTag.IsValid() && Intent.AbilityTag.MatchesTag(AttackTag))
            {
                Attacks.Add(Intent);
            }
        }

        if (Attacks.Num() == 0)
        {
            UE_LOG(LogTemp, Log, TEXT("[Attack Slot %d] No attacks"), Slot);
            continue;
        }

        UE_LOG(LogTemp, Log, TEXT("[Attack Slot %d] Processing %d attacks"),
            Slot, Attacks.Num());

        TArray<FResolvedAction> SlotActions;
        SlotActions.Reserve(Attacks.Num());

        for (const FEnemyIntent& Intent : Attacks)
        {
            AActor* IntentActor = Intent.Actor.Get();
            if (!IntentActor || !IsValid(IntentActor))
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("[Attack Slot %d] Skipping invalid Intent.Actor (nullptr or destroyed)"),
                    Slot);
                continue;
            }

            FResolvedAction Action;
            Action.ActorID = ActorRegistry ? ActorRegistry->GetStableID(IntentActor) : FStableActorID{};
            Action.Actor = IntentActor;
            Action.SourceActor = IntentActor;
            Action.FinalAbilityTag = AttackTag;
            Action.AbilityTag = RogueGameplayTags::GameplayEvent_Intent_Attack;
            Action.NextCell = Intent.NextCell;
            Action.TimeSlot = Slot;

            if (AActor* TargetActor = Intent.Target.Get())
            {
                FGameplayAbilityTargetData_SingleTargetHit* TargetData = new FGameplayAbilityTargetData_SingleTargetHit();
                FHitResult HitResult;
                HitResult.HitObjectHandle = FActorInstanceHandle(TargetActor);
                HitResult.Location = TargetActor->GetActorLocation();
                TargetData->HitResult = HitResult;
                Action.TargetData.Add(TargetData);

                UE_LOG(LogTemp, Log, TEXT("[Attack Slot %d] %s -> Target: %s"),
                    Slot, *GetNameSafe(IntentActor), *GetNameSafe(TargetActor));
            }
            else if (AActor* TargetActorAlt = Intent.TargetActor)
            {
                FGameplayAbilityTargetData_SingleTargetHit* TargetData = new FGameplayAbilityTargetData_SingleTargetHit();
                FHitResult HitResult;
                HitResult.HitObjectHandle = FActorInstanceHandle(TargetActorAlt);
                HitResult.Location = TargetActorAlt->GetActorLocation();
                TargetData->HitResult = HitResult;
                Action.TargetData.Add(TargetData);

                UE_LOG(LogTemp, Log, TEXT("[Attack Slot %d] %s -> Target (Alt): %s"),
                    Slot, *GetNameSafe(IntentActor), *GetNameSafe(TargetActorAlt));
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("[Attack Slot %d] %s has no target!"),
                    Slot, *GetNameSafe(IntentActor));
            }

            SlotActions.Add(Action);
        }

        TArray<FResolvedAction> ValidActions;
        ValidActions.Reserve(SlotActions.Num());
        for (const FResolvedAction& Action : SlotActions)
        {
            if (Action.Actor.IsValid() && Action.SourceActor != nullptr && IsValid(Action.SourceActor))
            {
                ValidActions.Add(Action);
            }
            else
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("[Attack Slot %d] Filtered out invalid action: Actor=%s"),
                    Slot, *GetNameSafe(Action.SourceActor));
            }
        }

        OutActions.Append(ValidActions);
    }

    UE_LOG(LogTemp, Log, TEXT("[ExecuteAttackPhaseWithSlots] Complete. Total = %d"),
        OutActions.Num());

    return MaxSlot;
}

void UTurnCorePhaseManager::CoreThinkPhaseWithTimeSlots(
    const TArray<AActor*>& Enemies,
    TArray<FEnemyIntent>& OutIntents)
{
    OutIntents.Reset();
    OutIntents.Reserve(Enemies.Num() * 2);

    const FName TagQuick(TEXT("Quick"));

    for (AActor* Enemy : Enemies)
    {
        if (!Enemy) { continue; }

        TArray<FEnemyIntent> FirstIntents = CoreThinkPhase({ Enemy });
        if (FirstIntents.Num() > 0)
        {
            FEnemyIntent Intent = FirstIntents[0];
            Intent.TimeSlot = 0;
            OutIntents.Add(Intent);
        }

        if (Enemy->ActorHasTag(TagQuick))
        {
            TArray<FEnemyIntent> SecondIntents = CoreThinkPhase({ Enemy });
            if (SecondIntents.Num() > 0)
            {
                FEnemyIntent Intent2nd = SecondIntents[0];
                Intent2nd.TimeSlot = 1;
                OutIntents.Add(Intent2nd);
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("[CoreThinkPhaseWithTimeSlots] Generated %d intents for %d enemies"),
        OutIntents.Num(), Enemies.Num());
}

// TurnCorePhaseManager.cpp

#include "AbilitySystemGlobals.h"

UAbilitySystemComponent* UTurnCorePhaseManager::ResolveASC(AActor* Actor)
{
    static TSet<TWeakObjectPtr<AActor>> WarnedMissingASC;
    AActor* const SourceActor = Actor;
    const auto ReturnWithSuccess = [SourceActor](UAbilitySystemComponent* Result)
    {
        if (Result)
        {
            WarnedMissingASC.Remove(SourceActor);
        }
        return Result;
    };
    if (!Actor)
    {
        UE_LOG(LogTurnCore, Verbose, TEXT("[TurnCore] ResolveASC: Actor is null"));
        return nullptr;
    }

    if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor))
    {
        return ReturnWithSuccess(ASC);
    }

    if (IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Actor))
    {
        if (UAbilitySystemComponent* ASC = ASI->GetAbilitySystemComponent())
        {
            return ReturnWithSuccess(ASC);
        }
    }

    if (UAbilitySystemComponent* ASC = Actor->FindComponentByClass<UAbilitySystemComponent>())
    {
        return ReturnWithSuccess(ASC);
    }

    if (APawn* Pawn = Cast<APawn>(Actor))
    {
        if (APlayerState* PS = Pawn->GetPlayerState<APlayerState>())
        {
            if (IAbilitySystemInterface* PSI = Cast<IAbilitySystemInterface>(PS))
            {
                if (UAbilitySystemComponent* ASC = PSI->GetAbilitySystemComponent())
                {
                    return ReturnWithSuccess(ASC);
                }
            }
            if (UAbilitySystemComponent* PS_ASC = PS->FindComponentByClass<UAbilitySystemComponent>())
            {
                return PS_ASC;
            }
        }
        else
        {
            UE_LOG(LogTemp, Verbose, TEXT("[TurnCore] ResolveASC: %s has no PlayerState"), *GetNameSafe(Pawn));
        }

        if (AController* C = Pawn->GetController())
        {
            if (IAbilitySystemInterface* CSI = Cast<IAbilitySystemInterface>(C))
            {
                if (UAbilitySystemComponent* ASC = CSI->GetAbilitySystemComponent())
                {
                    return ReturnWithSuccess(ASC);
                }
            }
            if (APlayerState* PS = C->GetPlayerState<APlayerState>())
            {
                if (IAbilitySystemInterface* PSI = Cast<IAbilitySystemInterface>(PS))
                {
                    if (UAbilitySystemComponent* ASC = PSI->GetAbilitySystemComponent())
                    {
                        return ReturnWithSuccess(ASC);
                    }
                }
                if (UAbilitySystemComponent* PS_ASC = PS->FindComponentByClass<UAbilitySystemComponent>())
                {
                    return PS_ASC;
                }
            }
            else
            {
                UE_LOG(LogTemp, Verbose, TEXT("[TurnCore] ResolveASC: %s's Controller has no PlayerState"), *GetNameSafe(C));
            }
        }
        else
        {
            UE_LOG(LogTemp, Verbose, TEXT("[TurnCore] ResolveASC: %s has no Controller"), *GetNameSafe(Pawn));
        }
    }

    if (AController* C = Cast<AController>(Actor))
    {
        if (APlayerState* PS = C->GetPlayerState<APlayerState>())
        {
            if (IAbilitySystemInterface* PSI = Cast<IAbilitySystemInterface>(PS))
            {
                if (UAbilitySystemComponent* ASC = PSI->GetAbilitySystemComponent())
                {
                    return ReturnWithSuccess(ASC);
                }
            }
            if (UAbilitySystemComponent* PS_ASC = PS->FindComponentByClass<UAbilitySystemComponent>())
            {
                return PS_ASC;
            }
        }
        else
        {
            UE_LOG(LogTemp, Verbose, TEXT("[TurnCore] ResolveASC: Controller %s has no PlayerState"), *GetNameSafe(C));
        }
    }

    if (!WarnedMissingASC.Contains(SourceActor))
    {
        WarnedMissingASC.Add(SourceActor);
        UE_LOG(LogTurnCore, Warning,
            TEXT("[TurnCore] ResolveASC: No ASC found for %s"),
            *GetNameSafe(SourceActor));
    }
    else
    {
        UE_LOG(LogTurnCore, VeryVerbose,
            TEXT("[TurnCore] ResolveASC: ASC still missing for %s"),
            *GetNameSafe(SourceActor));
    }
    return nullptr;
}



bool UTurnCorePhaseManager::IsGASReady(AActor* Actor)
{
    if (!Actor)
    {
        return false;
    }

    UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor);
    if (!ASC)
    {
        return false;
    }


    const FGameplayAbilityActorInfo* Info = ASC->AbilityActorInfo.Get();
    if (!Info || !Info->AvatarActor.IsValid())
    {
        UE_LOG(LogTemp, Verbose, TEXT("[TurnCore] IsGASReady: %s has invalid ActorInfo"), *GetNameSafe(Actor));
        return false;
    }

    const TArray<FGameplayAbilitySpec>& Abilities = ASC->GetActivatableAbilities();

    if (Abilities.Num() == 0)
    {
        UE_LOG(LogTemp, Verbose, TEXT("[TurnCore] IsGASReady: %s has ASC but no abilities yet"), *GetNameSafe(Actor));
        return false;
    }

    UE_LOG(LogTemp, Verbose, TEXT("[TurnCore] IsGASReady: %s has %d abilities - READY"), *GetNameSafe(Actor), Abilities.Num());
    return true;
}

bool UTurnCorePhaseManager::AllEnemiesReady(const TArray<AActor*>& Enemies) const
{
    int32 ReadyCount = 0;
    int32 NotReadyCount = 0;

    for (AActor* Enemy : Enemies)
    {
        if (IsGASReady(Enemy))
        {
            ReadyCount++;
        }
        else
        {
            NotReadyCount++;
            if (NotReadyCount <= 3)
            {
                UE_LOG(LogTemp, Verbose, TEXT("[TurnCore] AllEnemiesReady: %s not ready yet"),
                    *GetNameSafe(Enemy));
            }
        }
    }

    if (ReadyCount < Enemies.Num())
    {
        UE_LOG(LogTemp, Log, TEXT("[TurnCore] AllEnemiesReady: %d/%d enemies ready (waiting for %d)"),
            ReadyCount, Enemies.Num(), NotReadyCount);
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("[TurnCore] AllEnemiesReady: All %d enemies ready ?"), Enemies.Num());
    return true;
}
