// Copyright Epic Games, Inc. All Rights Reserved.

#include "EnemyTurnDataSubsystem.h"
#include "UObject/UnrealType.h"
#include "Kismet/GameplayStatics.h"
#include "GameplayTagsManager.h"

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

    int32 RegisteredCount = 0;
    int32 AlreadyRegisteredCount = 0;

    for (AActor* Enemy : FoundEnemies)
    {
        if (!Enemy)
        {
            continue;
        }

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

    UE_LOG(LogEnemyTurnDataSys, Log,
        TEXT("[RebuildEnemyList] Registered %d new enemies, %d already registered"),
        RegisteredCount, AlreadyRegisteredCount);

    // ────────────────────────────────────────────────────────────────────
    // Step 3: ソート済み配列を再構成
    // ────────────────────────────────────────────────────────────────────

    RebuildSortedArray();

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
