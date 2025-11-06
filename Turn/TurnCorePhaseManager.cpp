// Copyright Epic Games, Inc. All Rights Reserved.

#include "TurnCorePhaseManager.h"
#include "DistanceFieldSubsystem.h"
#include "ConflictResolverSubsystem.h"
// #include "Action/ActionExecutorSubsystem.h"  // ★★★ 統合完了により削除 ★★★
#include "StableActorRegistry.h"
#include "TurnSystemTypes.h"
#include "../Grid/GridOccupancySubsystem.h"
#include "UObject/UnrealType.h"
#include "../AI/Enemy/EnemyThinkerBase.h"
#include "GameplayTagsManager.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemGlobals.h"  
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Controller.h"
#include "Character/LyraCharacter.h"
#include "Character/LyraPawnExtensionComponent.h"
#include "AbilitySystem/LyraAbilitySystemComponent.h"


DEFINE_LOG_CATEGORY(LogTurnCore);

// ????????????????????????????????????????????????????????????????????????
// GenerationOrder読み取りヘルパー（反射を1回だけ）
// ????????????????????????????????????????????????????????????????????????
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

// ????????????????????????????????????????????????????????????????????????
// Subsystem Lifecycle
// ????????????????????????????????????????????????????????????????????????

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
    if (!DistanceField)
    {
        UE_LOG(LogTemp, Error, TEXT("[TurnCore] DistanceField is null"));
        return;
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

        int32 CurrentDist = DistanceField->GetDistance(LiveCurrentCell);
        int32 NextDist = DistanceField->GetDistance(Intent.NextCell);
        int32 DistanceReduction = FMath::Max(0, CurrentDist - NextDist);

        FReservationEntry Entry;
        Entry.Actor = Actor;
        Entry.ActorID = ActorID;
        Entry.TimeSlot = Intent.TimeSlot;
        Entry.CurrentCell = LiveCurrentCell;
        Entry.Cell = Intent.NextCell;
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

    return Resolved;
}

// TurnCorePhaseManager.cpp

// TurnCorePhaseManager.cpp
void UTurnCorePhaseManager::CoreExecutePhase(const TArray<FResolvedAction>& ResolvedActions)
{
    for (const FResolvedAction& Action : ResolvedActions)
    {
        UAbilitySystemComponent* ASC = ResolveASC(Action.SourceActor);
        if (!ASC) continue;
        
        // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        // 一時的に無効化：Blueprint側で実装
        // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        /*
        FGameplayEventData EventData;
        EventData.EventTag = Action.FinalAbilityTag;
        EventData.Instigator = Action.SourceActor;
        EventData.Target = Action.SourceActor;
        
        int32 NumActivated = ASC->HandleGameplayEvent(EventData.EventTag, &EventData);
        
        UE_LOG(LogTurnCore, Log, TEXT("[Execute] → Sending %s to %s"),
            *EventData.EventTag.ToString(),
            *GetNameSafe(Action.SourceActor));
        */
        
        // ログのみ残す
        UE_LOG(LogTurnCore, Log, TEXT("[Execute] Skipping C++ send for %s (BP will handle)"),
            *GetNameSafe(Action.SourceActor));
    }
    
    // BP呼び出しは維持（既存のBP_ExecuteResolvedActionsイベント）
    // ※既にある場合は以下は不要
    // OnExecuteResolvedActions.Broadcast(ResolvedActions);
}



void UTurnCorePhaseManager::CoreCleanupPhase()
{
    if (ConflictResolver)
    {
        ConflictResolver->ClearReservations();
    }

    UE_LOG(LogTemp, Log, TEXT("[TurnCore] CleanupPhase: Complete"));
}

// ????????????????????????????????????????????????????????????????????????
// TimeSlot対応：高速実行ラッパ（v2.4完全版）
// ????????????????????????????????????????????????????????????????????????

const FGameplayTag& UTurnCorePhaseManager::Tag_Move()
{
    static const FGameplayTag G = UGameplayTagsManager::Get()
        .RequestGameplayTag(TEXT("AI.Intent.Move"), false);
    return G;
}

const FGameplayTag& UTurnCorePhaseManager::Tag_Attack()
{
    static const FGameplayTag G = UGameplayTagsManager::Get()
        .RequestGameplayTag(TEXT("AI.Intent.Attack"), false);
    return G;
}

const FGameplayTag& UTurnCorePhaseManager::Tag_Wait()
{
    static const FGameplayTag G = UGameplayTagsManager::Get()
        .RequestGameplayTag(TEXT("AI.Intent.Wait"), false);
    return G;
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
// ????????????????????????????????????????????????????????????????????????
// ExecuteMovePhaseWithSlots（完全版）
// ????????????????????????????????????????????????????????????????????????

int32 UTurnCorePhaseManager::ExecuteMovePhaseWithSlots(
    const TArray<FEnemyIntent>& AllIntents,
    TArray<FResolvedAction>& OutActions)
{
    check(GetWorld() && GetWorld()->GetNetMode() != NM_Client);

    OutActions.Reset();

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
        // ★ 変更: Verbose → Log
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
            // ★ 変更: Verbose → Log
            UE_LOG(LogTemp, Log, TEXT("[Attack Slot %d] No attacks"), Slot);
            continue;
        }

        UE_LOG(LogTemp, Log, TEXT("[Attack Slot %d] Processing %d attacks"),
            Slot, Attacks.Num());

        TArray<FResolvedAction> SlotActions;
        SlotActions.Reserve(Attacks.Num());

        for (const FEnemyIntent& Intent : Attacks)
        {
            FResolvedAction Action;
            Action.ActorID = ActorRegistry ? ActorRegistry->GetStableID(Intent.Actor.Get()) : FStableActorID{};
            Action.SourceActor = Intent.Actor.Get();
            Action.FinalAbilityTag = AttackTag;
            Action.NextCell = Intent.NextCell;
            Action.TimeSlot = Slot;
            SlotActions.Add(Action);
        }

        for (const FResolvedAction& A : SlotActions)
        {
            if (!A.SourceActor) { continue; }

            UAbilitySystemComponent* ASC = ResolveASC(A.SourceActor);
            if (!ASC)
            {
                UE_LOG(LogTemp, Warning, TEXT("[Attack] %s has no ASC"),
                    *GetNameSafe(A.SourceActor));
                continue;
            }

            FGameplayEventData Evt;
            Evt.EventTag = AttackTag;
            Evt.Instigator = A.SourceActor;

            UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
                ASC->GetOwner(), AttackTag, Evt);

            // ★ 変更: Verbose → Log
            UE_LOG(LogTemp, Log, TEXT("[Attack] %s attacked"),
                *GetNameSafe(A.SourceActor));
        }

        OutActions.Append(SlotActions);
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

    const FName TagQuick(TEXT("神速"));

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

// ????????????????????????????????????????????????????????????????????????
// ResolveASC (Enhanced)
// ????????????????????????????????????????????????????????????????????????

UAbilitySystemComponent* UTurnCorePhaseManager::ResolveASC(AActor* Actor)
{
    if (!Actor)
    {
        UE_LOG(LogTemp, Warning, TEXT("[TurnCore] ResolveASC: Actor is null"));
        return nullptr;
    }

    if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor))
    {
        return ASC;
    }

    if (IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Actor))
    {
        if (UAbilitySystemComponent* ASC = ASI->GetAbilitySystemComponent())
        {
            return ASC;
        }
    }

    if (UAbilitySystemComponent* ASC = Actor->FindComponentByClass<UAbilitySystemComponent>())
    {
        return ASC;
    }

    if (APawn* Pawn = Cast<APawn>(Actor))
    {
        if (APlayerState* PS = Pawn->GetPlayerState<APlayerState>())
        {
            if (IAbilitySystemInterface* PSI = Cast<IAbilitySystemInterface>(PS))
            {
                if (UAbilitySystemComponent* ASC = PSI->GetAbilitySystemComponent())
                {
                    return ASC;
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
                    return ASC;
                }
            }
            if (APlayerState* PS = C->GetPlayerState<APlayerState>())
            {
                if (IAbilitySystemInterface* PSI = Cast<IAbilitySystemInterface>(PS))
                {
                    if (UAbilitySystemComponent* ASC = PSI->GetAbilitySystemComponent())
                    {
                        return ASC;
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
                    return ASC;
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

    UE_LOG(LogTemp, Warning, TEXT("[TurnCore] ResolveASC: No ASC found for %s"), *GetNameSafe(Actor));
    return nullptr;
}


// ????????????????????????????????????????????????????????????????????????
// GAS 準備完了チェック
// ????????????????????????????????????????????????????????????????????????

// ????????????????????????????????????????????????????????????????????????
// GAS 準備完了チェック（UE 5.6 対応）
// ????????????????????????????????????????????????????????????????????????

bool UTurnCorePhaseManager::IsGASReady(AActor* Actor)
{
    if (!Actor)
    {
        return false;
    }

    // ────────────────────────────────────────────────────────────────────
    // Step 1: ASC を取得
    // ────────────────────────────────────────────────────────────────────

    UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor);
    if (!ASC)
    {
        return false;
    }

    // ────────────────────────────────────────────────────────────────────
    // Step 2: ActorInfo が有効か確認
    // ────────────────────────────────────────────────────────────────────

    const FGameplayAbilityActorInfo* Info = ASC->AbilityActorInfo.Get();
    if (!Info || !Info->AvatarActor.IsValid())
    {
        UE_LOG(LogTemp, Verbose, TEXT("[TurnCore] IsGASReady: %s has invalid ActorInfo"), *GetNameSafe(Actor));
        return false;
    }

    // ────────────────────────────────────────────────────────────────────
    // Step 3: 能力が1つ以上付与されているか確認（UE 5.6 対応）
    // ────────────────────────────────────────────────────────────────────

    // UE 5.6: GetActivatableAbilities() は直接 TArray<FGameplayAbilitySpec> を返す
    // .Items プロパティは存在しないため削除
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
            // 最初の数体のみ詳細ログ（ログ氾濫防止）
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
