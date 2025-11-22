// Copyright Epic Games, Inc. All Rights Reserved.

#include "EnemyTurnDataSubsystem.h"
#include "UObject/UnrealType.h"
#include "Kismet/GameplayStatics.h"
#include "GameplayTagsManager.h"
#include "EnemyAISubsystem.h"
#include "Grid/GridPathfindingSubsystem.h"
#include "Grid/GridOccupancySubsystem.h"
#include "Turn/DistanceFieldSubsystem.h"
#include "GameFramework/Pawn.h"
#include "Turn/UnitTurnStateSubsystem.h"

// ログカテゴリ定義
DEFINE_LOG_CATEGORY(LogEnemyTurnDataSys);

//--------------------------------------------------------------------------
// Lifecycle
//--------------------------------------------------------------------------

void UEnemyTurnDataSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    NextGenerationOrder = 0;
    EnemyToOrder.Reset();
    EnemiesSorted.Reset();
    Intents.Reset();
    DataRevision = 0;

    UE_LOG(LogEnemyTurnDataSys, Log,
        TEXT("[EnemyTurnData] Subsystem Initialized"));
}

void UEnemyTurnDataSubsystem::Deinitialize()
{
    UE_LOG(LogEnemyTurnDataSys, Log,
        TEXT("[EnemyTurnData] Subsystem Deinitialized (Total Enemies: %d, Final Revision=%d)"),
        EnemyToOrder.Num(), DataRevision);

    NextGenerationOrder = 0;
    EnemyToOrder.Reset();
    EnemiesSorted.Reset();
    Intents.Reset();

    Super::Deinitialize();
}

//--------------------------------------------------------------------------
// GenerationOrder管理
//--------------------------------------------------------------------------

void UEnemyTurnDataSubsystem::RegisterEnemyAndAssignOrder(AActor* Enemy)
{
    if (!Enemy)
    {
        UE_LOG(LogEnemyTurnDataSys, Warning,
            TEXT("[RegisterEnemy] Enemy is null"));
        return;
    }

    // 既に登録されている場合はスキップ
    if (EnemyToOrder.Contains(Enemy))
    {
        UE_LOG(LogEnemyTurnDataSys, Verbose,
            TEXT("[RegisterEnemy] %s already registered with Order=%d"),
            *Enemy->GetName(), EnemyToOrder[Enemy]);
        return;
    }

    // 新しい順番を採番
    const int32 AssignedOrder = NextGenerationOrder++;
    EnemyToOrder.Add(Enemy, AssignedOrder);

    // EnemyのGenerationOrder変数（Blueprint変数）に書き込む
    static const FName GenName(TEXT("GenerationOrder"));
    if (FIntProperty* Prop = FindFProperty<FIntProperty>(Enemy->GetClass(), GenName))
    {
        int32* ValuePtr = Prop->ContainerPtrToValuePtr<int32>(Enemy);
        if (ValuePtr)
        {
            *ValuePtr = AssignedOrder;
            UE_LOG(LogEnemyTurnDataSys, Log,
                TEXT("[RegisterEnemy] %s → GenerationOrder=%d (written to BP variable)"),
                *Enemy->GetName(), AssignedOrder);
        }
    }
    else
    {
        UE_LOG(LogEnemyTurnDataSys, Warning,
            TEXT("[RegisterEnemy] %s does not have GenerationOrder property"),
            *Enemy->GetName());
    }
}

int32 UEnemyTurnDataSubsystem::GetEnemyGenerationOrder(AActor* Enemy) const
{
    if (!Enemy)
    {
        return TNumericLimits<int32>::Max();
    }

    if (const int32* OrderPtr = EnemyToOrder.Find(Enemy))
    {
        return *OrderPtr;
    }

    return TNumericLimits<int32>::Max();
}

void UEnemyTurnDataSubsystem::ClearAllRegistrations()
{
    const int32 Count = EnemyToOrder.Num();

    NextGenerationOrder = 0;
    EnemyToOrder.Reset();
    EnemiesSorted.Reset();
    Intents.Reset();
    DataRevision = 0;

    UE_LOG(LogEnemyTurnDataSys, Log,
        TEXT("[ClearAll] Cleared %d registrations"), Count);
}

//--------------------------------------------------------------------------
// 🌟 統合API（Lumina提言A4: RebuildEnemyList）
//--------------------------------------------------------------------------

void UEnemyTurnDataSubsystem::SyncEnemiesFromList(const TArray<AActor*>& EnemyList)
{
    UE_LOG(LogEnemyTurnDataSys, Verbose,
        TEXT("[SyncEnemiesFromList] Syncing with %d enemies"), EnemyList.Num());

    int32 RegisteredCount = 0;
    int32 AlreadyRegisteredCount = 0;

    for (AActor* Enemy : EnemyList)
    {
        if (!Enemy) continue;

        if (EnemyToOrder.Contains(Enemy))
        {
            AlreadyRegisteredCount++;
        }
        else
        {
            RegisterEnemyAndAssignOrder(Enemy);
            RegisteredCount++;
        }
    }

    if (RegisteredCount > 0)
    {
        UE_LOG(LogEnemyTurnDataSys, Log,
            TEXT("[SyncEnemiesFromList] Registered %d new enemies, %d already registered"),
            RegisteredCount, AlreadyRegisteredCount);
    }

    RebuildSortedArray();
}

void UEnemyTurnDataSubsystem::RebuildEnemyList(FName TagFilter)
{
    UE_LOG(LogEnemyTurnDataSys, Log,
        TEXT("[RebuildEnemyList] Starting rebuild with Tag='%s'"),
        *TagFilter.ToString());

    // ────────────────────────────────────────────────────────────────────
    // Step 1: 大量取得（GetAllActorsWithTag）
    // ────────────────────────────────────────────────────────────────────

    TArray<AActor*> FoundEnemies;
    FoundEnemies.Reserve(128);

    UGameplayStatics::GetAllActorsWithTag(
        GetWorld(), TagFilter, FoundEnemies
    );

    UE_LOG(LogEnemyTurnDataSys, Log,
        TEXT("[RebuildEnemyList] Found %d actors with tag '%s'"),
        FoundEnemies.Num(), *TagFilter.ToString());

    // ────────────────────────────────────────────────────────────────────
    // Step 2: 登録（既存を保持、新規のみ採番）
    // ────────────────────────────────────────────────────────────────────

    SyncEnemiesFromList(FoundEnemies);

    UE_LOG(LogEnemyTurnDataSys, Log,
        TEXT("[RebuildEnemyList] Complete. Total enemies: %d (Revision=%d)"),
        EnemiesSorted.Num(), DataRevision);
}


void UEnemyTurnDataSubsystem::RebuildSortedArray()
{
    EnemiesSorted.Reset();
    EnemiesSorted.Reserve(EnemyToOrder.Num());

    // ────────────────────────────────────────────────────────────────────
    // Step 1: Map → Array（無効なActorを除外）
    // ────────────────────────────────────────────────────────────────────

    for (const TPair<TObjectPtr<AActor>, int32>& Pair : EnemyToOrder)
    {
        if (Pair.Key != nullptr)
        {
            EnemiesSorted.Add(Pair.Key);
        }
        else
        {
            UE_LOG(LogEnemyTurnDataSys, Verbose,
                TEXT("[RebuildSortedArray] Skipping invalid actor"));
        }
    }

    // ────────────────────────────────────────────────────────────────────
    // Step 2: 安定ソート（GenerationOrder昇順）
    // ────────────────────────────────────────────────────────────────────

    EnemiesSorted.StableSort([this](const AActor& A, const AActor& B) {
        const int32 OrderA = EnemyToOrder.FindRef(&A);
        const int32 OrderB = EnemyToOrder.FindRef(&B);
        return OrderA < OrderB;
        });

    ++DataRevision;

    UE_LOG(LogEnemyTurnDataSys, Verbose,
        TEXT("[RebuildSortedArray] Sorted %d enemies (Revision=%d)"),
        EnemiesSorted.Num(), DataRevision);
}

//--------------------------------------------------------------------------
// 🌟 統合API（Lumina提言B1: HasAttackIntent）
//--------------------------------------------------------------------------

bool UEnemyTurnDataSubsystem::HasAttackIntent() const
{
    // ────────────────────────────────────────────────────────────────────
    // Step 1: AI.Intent.Attack タグ取得
    // ────────────────────────────────────────────────────────────────────

    const FGameplayTag AttackTag = FGameplayTag::RequestGameplayTag(
        TEXT("AI.Intent.Attack"), false
    );

    if (!AttackTag.IsValid())
    {
        UE_LOG(LogEnemyTurnDataSys, Warning,
            TEXT("[HasAttackIntent] AI.Intent.Attack tag not found"));
        return false;
    }

    // ────────────────────────────────────────────────────────────────────
    // Step 2: Intent配列を走査（1パス処理）
    // ────────────────────────────────────────────────────────────────────

    for (const FEnemyIntent& Intent : Intents)
    {
        if (Intent.AbilityTag.MatchesTag(AttackTag))
        {
            UE_LOG(LogEnemyTurnDataSys, Verbose,
                TEXT("[HasAttackIntent] Attack intent found: %s"),
                *GetNameSafe(Intent.Actor.Get()));
            return true;
        }
    }

    UE_LOG(LogEnemyTurnDataSys, Verbose,
        TEXT("[HasAttackIntent] No attack intents (Total: %d intents)"),
        Intents.Num());

    return false;
}

bool UEnemyTurnDataSubsystem::HasWaitIntent() const
{
    const FGameplayTag WaitTag = FGameplayTag::RequestGameplayTag(TEXT("AI.Intent.Wait"), false);

    if (!WaitTag.IsValid())
    {
        UE_LOG(LogEnemyTurnDataSys, Warning,
            TEXT("[HasWaitIntent] AI.Intent.Wait tag not found"));
        return false;
    }

    for (const FEnemyIntent& Intent : Intents)
    {
        if (Intent.AbilityTag.MatchesTag(WaitTag))
        {
            UE_LOG(LogEnemyTurnDataSys, Verbose,
                TEXT("[HasWaitIntent] Wait intent found: %s"),
                *GetNameSafe(Intent.Actor.Get()));
            return true;
        }
    }

    return false;
}

//--------------------------------------------------------------------------
// Intent配列の管理
//--------------------------------------------------------------------------

void UEnemyTurnDataSubsystem::SaveIntents(const TArray<FEnemyIntent>& NewIntents)
{
    Intents = NewIntents;
    ++DataRevision;

    // INC-2025-0002: ログ強化 - 保存されるIntentsのAttack/Move/Wait件数を集計
    int32 AttackCount = 0;
    int32 MoveCount = 0;
    int32 WaitCount = 0;

    const FGameplayTag AttackTag = RogueGameplayTags::AI_Intent_Attack;
    const FGameplayTag MoveTag = RogueGameplayTags::AI_Intent_Move;
    const FGameplayTag WaitTag = RogueGameplayTags::AI_Intent_Wait;

    for (const FEnemyIntent& Intent : Intents)
    {
        if (Intent.AbilityTag.MatchesTag(AttackTag))
        {
            ++AttackCount;
        }
        else if (Intent.AbilityTag.MatchesTag(MoveTag))
        {
            ++MoveCount;
        }
        else if (Intent.AbilityTag.MatchesTag(WaitTag))
        {
            ++WaitCount;
        }
    }

    UE_LOG(LogEnemyTurnDataSys, Warning,
        TEXT("[SaveIntents] Saved %d intents (Attack=%d, Move=%d, Wait=%d, Revision=%d)"),
        Intents.Num(), AttackCount, MoveCount, WaitCount, DataRevision);
}

const TArray<FEnemyIntent>& UEnemyTurnDataSubsystem::GetIntents() const
{
    return Intents;
}

void UEnemyTurnDataSubsystem::ClearIntents()
{
    Intents.Reset();
}

void UEnemyTurnDataSubsystem::ClearAttackIntents()
{
    // ★★★ FIX (2025-11-12): 攻撃インテントのみをクリア（移動インテントは残す） ★★★
    // 攻撃した敵は移動フェーズで動かないようにするため
    const FGameplayTag AttackTag = FGameplayTag::RequestGameplayTag(TEXT("AI.Intent.Attack"), false);

    if (!AttackTag.IsValid())
    {
        UE_LOG(LogEnemyTurnDataSys, Warning,
            TEXT("[ClearAttackIntents] AI.Intent.Attack tag not found"));
        return;
    }

    const int32 OriginalCount = Intents.Num();

    // 攻撃インテントを除外
    Intents.RemoveAll([AttackTag](const FEnemyIntent& Intent) {
        return Intent.AbilityTag.MatchesTag(AttackTag);
    });

    const int32 RemovedCount = OriginalCount - Intents.Num();

    UE_LOG(LogEnemyTurnDataSys, Log,
        TEXT("[ClearAttackIntents] Cleared %d attack intents, %d intents remaining"),
        RemovedCount, Intents.Num());
}

void UEnemyTurnDataSubsystem::ConvertAttacksToWait()
{
    // ★★★ FIX (2025-11-12): 攻撃インテントをWaitインテントに変換 ★★★
    // 攻撃した敵は移動しないが、ConflictResolverで現在地を占有し続けるため
    const FGameplayTag AttackTag = FGameplayTag::RequestGameplayTag(TEXT("AI.Intent.Attack"), false);
    const FGameplayTag WaitTag = FGameplayTag::RequestGameplayTag(TEXT("AI.Intent.Wait"), false);

    if (!AttackTag.IsValid() || !WaitTag.IsValid())
    {
        UE_LOG(LogEnemyTurnDataSys, Warning,
            TEXT("[ConvertAttacksToWait] AI.Intent tags not found (Attack=%d, Wait=%d)"),
            AttackTag.IsValid(), WaitTag.IsValid());
        return;
    }

    int32 ConvertedCount = 0;

    // 攻撃インテントを Wait に変換
    for (FEnemyIntent& Intent : Intents)
    {
        if (Intent.AbilityTag.MatchesTag(AttackTag))
        {
            Intent.AbilityTag = WaitTag;
            Intent.NextCell = Intent.CurrentCell;  // 移動しない
            ConvertedCount++;
        }
    }

    UE_LOG(LogEnemyTurnDataSys, Log,
        TEXT("[ConvertAttacksToWait] Converted %d attack intents to wait, total intents=%d"),
        ConvertedCount, Intents.Num());
}

TArray<FEnemyIntent> UEnemyTurnDataSubsystem::GetIntentsCopy() const
{
    return Intents;
}

void UEnemyTurnDataSubsystem::SetIntents(const TArray<FEnemyIntent>& NewIntents)
{
    if (ensureAlwaysMsgf(GetWorld() && GetWorld()->GetNetMode() != NM_Client,
        TEXT("[EnemyTurnData] SetIntents must run on server")))
    {
        Intents = NewIntents;
        ++DataRevision;
        UE_LOG(LogEnemyTurnDataSys, Log,
            TEXT("[EnemyTurnData] SetIntents: %d intents (Revision=%d)"),
            Intents.Num(), DataRevision);
    }
}

bool UEnemyTurnDataSubsystem::HasIntents() const
{
    return Intents.Num() > 0;
}

// CodeRevision: INC-2025-1122-ADJ-ATTACK-R3 (Upgrade to attack only if adjacent to BOTH current AND target positions) (2025-11-22)
int32 UEnemyTurnDataSubsystem::UpgradeIntentsForAdjacency(const FIntPoint& PlayerCurrentCell, const FIntPoint& PlayerTargetCell, int32 AttackRange)
{
    // Use local variable to avoid needing RogueGameplayTags include here
    static const FGameplayTag AttackTag = FGameplayTag::RequestGameplayTag(FName("AI.Intent.Attack"));
    static const FGameplayTag MoveTag = FGameplayTag::RequestGameplayTag(FName("AI.Intent.Move"));

    int32 UpgradedCount = 0;

    for (FEnemyIntent& Intent : Intents)
    {
        // Skip if already an attack intent
        if (Intent.AbilityTag.MatchesTag(AttackTag))
        {
            continue;
        }

        // Only upgrade Move intents (not Wait)
        if (!Intent.AbilityTag.MatchesTag(MoveTag))
        {
            continue;
        }

        // Check Chebyshev distance from enemy's current position to BOTH player positions
        // Must be adjacent to current position (to attack now) AND target position (to still be adjacent after player moves)
        const int32 DistToCurrent = FMath::Max(
            FMath::Abs(Intent.CurrentCell.X - PlayerCurrentCell.X),
            FMath::Abs(Intent.CurrentCell.Y - PlayerCurrentCell.Y));
        const int32 DistToTarget = FMath::Max(
            FMath::Abs(Intent.CurrentCell.X - PlayerTargetCell.X),
            FMath::Abs(Intent.CurrentCell.Y - PlayerTargetCell.Y));

        if (DistToCurrent <= AttackRange && DistToTarget <= AttackRange)
        {
            // CodeRevision: INC-2025-1123-FIX-R6 (Prevent corner cutting attacks) (2025-11-23 06:00)
            // Check for corner cutting (diagonal blockage) to the target position.
            // Even if distance is 1, we can't attack through a wall corner.
            if (!IsAttackLineClear(Intent.CurrentCell, PlayerTargetCell))
            {
                 UE_LOG(LogEnemyTurnDataSys, Verbose,
                    TEXT("[UpgradeIntentsForAdjacency] Reject upgrade for (%d,%d): Corner blocked to target (%d,%d)"),
                    Intent.CurrentCell.X, Intent.CurrentCell.Y, PlayerTargetCell.X, PlayerTargetCell.Y);
                 continue;
            }

            // Upgrade to attack intent - enemy is adjacent to both positions
            Intent.AbilityTag = AttackTag;
            Intent.NextCell = Intent.CurrentCell; // Attack from current position
            UpgradedCount++;

            UE_LOG(LogEnemyTurnDataSys, Log,
                TEXT("[UpgradeIntentsForAdjacency] Upgraded enemy at (%d,%d) to ATTACK (DistCurrent=%d, DistTarget=%d, PlayerCurrent=(%d,%d), PlayerTarget=(%d,%d))"),
                Intent.CurrentCell.X, Intent.CurrentCell.Y, DistToCurrent, DistToTarget,
                PlayerCurrentCell.X, PlayerCurrentCell.Y, PlayerTargetCell.X, PlayerTargetCell.Y);
        }
    }

    if (UpgradedCount > 0)
    {
        UE_LOG(LogEnemyTurnDataSys, Warning,
            TEXT("[UpgradeIntentsForAdjacency] Upgraded %d intents to ATTACK (PlayerCurrent=(%d,%d), PlayerTarget=(%d,%d))"),
            UpgradedCount, PlayerCurrentCell.X, PlayerCurrentCell.Y, PlayerTargetCell.X, PlayerTargetCell.Y);
    }

    return UpgradedCount;
}

void UEnemyTurnDataSubsystem::GetIntentsForSlot(
    int32 TargetSlot,
    TArray<FEnemyIntent>& OutIntents) const
{
    OutIntents.Reset();
    OutIntents.Reserve(Intents.Num() / 2 + 4);

    for (const FEnemyIntent& Intent : Intents)
    {
        if (Intent.TimeSlot == TargetSlot)
        {
            OutIntents.Add(Intent);
        }
    }

    UE_LOG(LogEnemyTurnDataSys, Verbose,
        TEXT("[GetIntentsForSlot] Slot=%d → %d intents"),
        TargetSlot, OutIntents.Num());
}

//--------------------------------------------------------------------------
// EnemiesSorted配列の管理
//--------------------------------------------------------------------------

TArray<AActor*> UEnemyTurnDataSubsystem::GetEnemiesSortedCopy() const
{
    TArray<AActor*> Result;
    Result.Reserve(EnemiesSorted.Num());

    for (const TObjectPtr<AActor>& ActorPtr : EnemiesSorted)
    {
        Result.Add(ActorPtr.Get());
    }

    return Result;
}

void UEnemyTurnDataSubsystem::SetEnemiesSorted(const TArray<AActor*>& NewEnemies)
{
    if (ensureAlwaysMsgf(GetWorld() && GetWorld()->GetNetMode() != NM_Client,
        TEXT("[EnemyTurnData] SetEnemiesSorted must run on server")))
    {
        EnemiesSorted.Reset();
        EnemiesSorted.Reserve(NewEnemies.Num());

        for (AActor* Actor : NewEnemies)
        {
            EnemiesSorted.Add(Actor);
        }

        ++DataRevision;
        UE_LOG(LogEnemyTurnDataSys, Log,
            TEXT("[EnemyTurnData] SetEnemiesSorted: %d enemies (Revision=%d)"),
            EnemiesSorted.Num(), DataRevision);
    }
}

bool UEnemyTurnDataSubsystem::IsAttackLineClear(const FIntPoint& From, const FIntPoint& To) const
{
    // Orthogonal is always clear (assuming adjacency)
    if (From.X == To.X || From.Y == To.Y)
    {
        return true;
    }

    // Diagonal check
    FIntPoint Shoulder1(From.X, To.Y);
    FIntPoint Shoulder2(To.X, From.Y);

    UWorld* World = GetWorld();
    if (World)
    {
        if (UGridPathfindingSubsystem* PathFinder = World->GetSubsystem<UGridPathfindingSubsystem>())
        {
            // Check shoulders. We use IsCellWalkableIgnoringActor because units don't block LoS for melee (usually),
            // but walls do.
            bool bS1 = PathFinder->IsCellWalkableIgnoringActor(Shoulder1, nullptr);
            bool bS2 = PathFinder->IsCellWalkableIgnoringActor(Shoulder2, nullptr);
            
            // At least one shoulder must be walkable to attack diagonally
            if (!bS1 && !bS2)
            {
                UE_LOG(LogEnemyTurnDataSys, Verbose,
                    TEXT("[IsAttackLineClear] Blocked by corner: From(%d,%d) To(%d,%d) Shoulders (%d,%d)=%d, (%d,%d)=%d"),
                    From.X, From.Y, To.X, To.Y,
                    Shoulder1.X, Shoulder1.Y, bS1,
                    Shoulder2.X, Shoulder2.Y, bS2);
                return false;
            }
        }
    }
    
    return true;
}

//--------------------------------------------------------------------------
// Intent Fallback Generation
// CodeRevision: INC-2025-1122-R3 (Extract from GameTurnManagerBase) (2025-11-22)
//--------------------------------------------------------------------------

bool UEnemyTurnDataSubsystem::EnsureIntentsFallback(
    int32 TurnId,
    APawn* PlayerPawn,
    UGridPathfindingSubsystem* PathFinder,
    const TArray<AActor*>& InEnemyActors,
    TArray<FEnemyIntent>& OutIntents)
{
    // Already have intents - no fallback needed
    if (Intents.Num() > 0)
    {
        OutIntents = Intents;
        return true;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogEnemyTurnDataSys, Error,
            TEXT("[Turn %d] EnsureIntentsFallback: World is null"), TurnId);
        return false;
    }

    UE_LOG(LogEnemyTurnDataSys, Warning,
        TEXT("[Turn %d] EnsureIntentsFallback: No enemy intents detected, attempting fallback generation..."),
        TurnId);

    UEnemyAISubsystem* EnemyAISys = World->GetSubsystem<UEnemyAISubsystem>();
    if (!EnemyAISys || !IsValid(PathFinder) || !PlayerPawn)
    {
        UE_LOG(LogEnemyTurnDataSys, Error,
            TEXT("[Turn %d] Fallback failed: EnemyAISys=%d, PathFinder=%d, PlayerPawn=%d"),
            TurnId,
            EnemyAISys != nullptr,
            IsValid(PathFinder),
            PlayerPawn != nullptr);
        return false;
    }

    if (InEnemyActors.Num() == 0)
    {
        UE_LOG(LogEnemyTurnDataSys, Warning,
            TEXT("[Turn %d] Fallback: No enemy actors provided"), TurnId);
        return false;
    }

    // Get reliable player grid coordinates from GridOccupancySubsystem
    FIntPoint PlayerGrid = FIntPoint(-1, -1);
    if (UGridOccupancySubsystem* Occupancy = World->GetSubsystem<UGridOccupancySubsystem>())
    {
        PlayerGrid = Occupancy->GetCellOfActor(PlayerPawn);
    }

    // Update DistanceField for fallback
    if (UDistanceFieldSubsystem* DistanceField = World->GetSubsystem<UDistanceFieldSubsystem>())
    {
        DistanceField->UpdateDistanceField(PlayerGrid);
        UE_LOG(LogEnemyTurnDataSys, Log,
            TEXT("[Turn %d] Fallback: DistanceField updated for PlayerGrid=(%d,%d)"),
            TurnId, PlayerGrid.X, PlayerGrid.Y);
    }

    // Build observations
    TArray<FEnemyObservation> FallbackObservations;
    EnemyAISys->BuildObservations(InEnemyActors, PlayerGrid, PathFinder, FallbackObservations);
    Observations = FallbackObservations;

    UE_LOG(LogEnemyTurnDataSys, Warning,
        TEXT("[Turn %d] Fallback: Generated %d observations"),
        TurnId, FallbackObservations.Num());

    // Collect intents
    TArray<FEnemyIntent> FallbackIntents;
    EnemyAISys->CollectIntents(FallbackObservations, InEnemyActors, FallbackIntents);
    Intents = FallbackIntents;
    OutIntents = FallbackIntents;

    UE_LOG(LogEnemyTurnDataSys, Warning,
        TEXT("[Turn %d] Fallback: Generated %d intents"),
        TurnId, FallbackIntents.Num());

    return FallbackIntents.Num() > 0;
}

// CodeRevision: INC-2025-1122-SIMUL-R5 (Extract intent regeneration logic from ExecuteEnemyPhase) (2025-11-22)
bool UEnemyTurnDataSubsystem::RegenerateIntentsForPlayerPosition(int32 TurnId, const FIntPoint& PlayerTargetCell, TArray<FEnemyIntent>& OutIntents)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogEnemyTurnDataSys, Error, TEXT("[Turn %d] RegenerateIntents: World is null"), TurnId);
        return false;
    }

    UEnemyAISubsystem* EnemyAI = World->GetSubsystem<UEnemyAISubsystem>();
    UGridPathfindingSubsystem* PathFinder = World->GetSubsystem<UGridPathfindingSubsystem>();

    if (!EnemyAI || !PathFinder)
    {
        UE_LOG(LogEnemyTurnDataSys, Warning,
            TEXT("[Turn %d] RegenerateIntents: Missing subsystems (EnemyAI=%d, PathFinder=%d)"),
            TurnId, EnemyAI != nullptr, PathFinder != nullptr);
        return false;
    }

    // Rebuild enemy list
    // CodeRevision: INC-2025-1122-PERF-R4 (Use cached enemies instead of RebuildEnemyList)
    if (UUnitTurnStateSubsystem* UnitState = World->GetSubsystem<UUnitTurnStateSubsystem>())
    {
        TArray<AActor*> CachedEnemies;
        UnitState->CopyEnemiesTo(CachedEnemies);
        SyncEnemiesFromList(CachedEnemies);
    }
    else
    {
        // Fallback if UnitState is missing
        RebuildEnemyList(FName("Enemy"));
    }

    // Get enemy actors
    TArray<AActor*> EnemyActors = GetEnemiesSortedCopy();
    UE_LOG(LogEnemyTurnDataSys, Log,
        TEXT("[Turn %d] RegenerateIntents: Found %d enemies after RebuildEnemyList"),
        TurnId, EnemyActors.Num());

    if (EnemyActors.Num() == 0)
    {
        OutIntents.Empty();
        Intents.Empty();
        UE_LOG(LogEnemyTurnDataSys, Log, TEXT("[Turn %d] RegenerateIntents: No enemies to process"), TurnId);
        return true; // Not an error, just no enemies
    }

    // Build observations based on player's target position
    TArray<FEnemyObservation> FinalObservations;
    EnemyAI->BuildObservations(EnemyActors, PlayerTargetCell, PathFinder, FinalObservations);

    // Generate intents based on observations
    TArray<FEnemyIntent> FinalIntents;
    EnemyAI->CollectIntents(FinalObservations, EnemyActors, FinalIntents);

    // Count intents for logging
    int32 AttackCount = 0;
    int32 MoveCount = 0;
    int32 WaitCount = 0;
    static const FGameplayTag AttackTag = FGameplayTag::RequestGameplayTag(FName("AI.Intent.Attack"));
    static const FGameplayTag MoveTag = FGameplayTag::RequestGameplayTag(FName("AI.Intent.Move"));
    for (const FEnemyIntent& Intent : FinalIntents)
    {
        if (Intent.AbilityTag.MatchesTag(AttackTag)) AttackCount++;
        else if (Intent.AbilityTag.MatchesTag(MoveTag)) MoveCount++;
        else WaitCount++;
    }

    UE_LOG(LogEnemyTurnDataSys, Log,
        TEXT("[Turn %d] RegenerateIntents: Generated %d intents (Attack=%d, Move=%d, Wait=%d) for PlayerCell=(%d,%d)"),
        TurnId, FinalIntents.Num(), AttackCount, MoveCount, WaitCount, PlayerTargetCell.X, PlayerTargetCell.Y);

    // Store and output
    Intents = FinalIntents;
    OutIntents = FinalIntents;

    return true;
}
