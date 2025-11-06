// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConflictResolverSubsystem.h"
#include "GameplayTagsManager.h"
#include "Engine/World.h"

void UConflictResolverSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    ReservationTable.Reset();

    UE_LOG(LogTemp, Log, TEXT("[ConflictResolver] Initialized"));
}

void UConflictResolverSubsystem::Deinitialize()
{
    Super::Deinitialize();

    UE_LOG(LogTemp, Log, TEXT("[ConflictResolver] Deinitialized. Total Reservations: %d"), ReservationTable.Num());
}

void UConflictResolverSubsystem::ClearReservations()
{
    ReservationTable.Reset();
}

void UConflictResolverSubsystem::AddReservation(const FReservationEntry& Entry)
{
    TPair<int32, FIntPoint> Key = TPair<int32, FIntPoint>(Entry.TimeSlot, Entry.Cell);
    ReservationTable.FindOrAdd(Key).Add(Entry);
}

int32 UConflictResolverSubsystem::GetActionTier(const FGameplayTag& AbilityTag) const
{
    // ★★★ v2.2 第17条: 行動種別の優先度（エラー対策版） ★★★

    // ★★★ タグの存在チェック付きで取得 ★★★
    static const FGameplayTag AttackTag = FGameplayTag::RequestGameplayTag(FName("AI.Intent.Attack"), false);
    static const FGameplayTag DashTag = FGameplayTag::RequestGameplayTag(FName("AI.Intent.Dash"), false);
    static const FGameplayTag MoveTag = FGameplayTag::RequestGameplayTag(FName("AI.Intent.Move"), false);
    static const FGameplayTag WaitTag = FGameplayTag::RequestGameplayTag(FName("AI.Intent.Wait"), false);

    // ★★★ タグが無効な場合の警告（初回のみ） ★★★
    static bool bWarningShown = false;
    if (!bWarningShown)
    {
        if (!AttackTag.IsValid())
            UE_LOG(LogTemp, Warning, TEXT("[ConflictResolver] Tag 'AI.Intent.Attack' not found. Please add to GameplayTags."));
        if (!DashTag.IsValid())
            UE_LOG(LogTemp, Warning, TEXT("[ConflictResolver] Tag 'AI.Intent.Dash' not found. Please add to GameplayTags."));
        if (!MoveTag.IsValid())
            UE_LOG(LogTemp, Warning, TEXT("[ConflictResolver] Tag 'AI.Intent.Move' not found. Please add to GameplayTags."));
        if (!WaitTag.IsValid())
            UE_LOG(LogTemp, Warning, TEXT("[ConflictResolver] Tag 'AI.Intent.Wait' not found. Please add to GameplayTags."));

        bWarningShown = true;
    }

    // ★★★ タグのマッチング（Validチェック付き） ★★★
    if (AttackTag.IsValid() && AbilityTag.MatchesTag(AttackTag))
    {
        return 3;
    }
    if (DashTag.IsValid() && AbilityTag.MatchesTag(DashTag))
    {
        return 2;
    }
    if (MoveTag.IsValid() && AbilityTag.MatchesTag(MoveTag))
    {
        return 1;
    }

    return 0;  // Wait または不明なタグ
}

TArray<FResolvedAction> UConflictResolverSubsystem::ResolveAllConflicts()
{
    TArray<FResolvedAction> Results;

    // ★★★ ReservationTableを使用 ★★★
    for (const auto& Pair : ReservationTable)
    {
        const TArray<FReservationEntry>& Entries = Pair.Value;
        for (const FReservationEntry& Entry : Entries)
        {
            FResolvedAction Action;
            Action.SourceActor = Entry.Actor;
            Action.ActorID = Entry.ActorID;
            Action.CurrentCell = Entry.CurrentCell;  // ★ 追加：CurrentCellを設定
            Action.NextCell = Entry.Cell;
            Action.FinalAbilityTag = Entry.AbilityTag;
            Action.AllowedDashSteps = 0;
            Action.TimeSlot = Entry.TimeSlot;  // ★ 追加：TimeSlotも設定
            Action.GenerationOrder = Entry.GenerationOrder;  // ★ 追加：GenerationOrderも設定

            Results.Add(Action);
        }
    }

    // ★★★ タグ一貫性チェック ★★★
    for (const FResolvedAction& Action : Results)
    {
        ensureMsgf(Action.FinalAbilityTag.IsValid(),
            TEXT("[ConflictResolver] FinalAbilityTag must be valid for %s"),
            *GetNameSafe(Action.SourceActor.Get()));
    }

    return Results;
}

TArray<FResolvedAction> UConflictResolverSubsystem::ResolveWithTripleBucket(const TArray<FReservationEntry>& Applicants)
{
    TArray<FResolvedAction> Results;

    // ① 行動種別でバケット化
    TMap<int32, TArray<FReservationEntry>> TierBuckets;
    for (const FReservationEntry& Entry : Applicants)
    {
        int32 Tier = GetActionTier(Entry.AbilityTag);
        TierBuckets.FindOrAdd(Tier).Add(Entry);
    }

    // ② 最高Tierを取得
    int32 MaxTier = 0;
    for (const auto& BucketPair : TierBuckets)
    {
        MaxTier = FMath::Max(MaxTier, BucketPair.Key);
    }

    TArray<FReservationEntry>& WinnerBucket = TierBuckets[MaxTier];

    // ③ 同Tier内は優先度→距離短縮→StableIDでソート
    if (WinnerBucket.Num() > 1)
    {
        WinnerBucket.StableSort([](const FReservationEntry& A, const FReservationEntry& B)
            {
                if (A.BasePriority != B.BasePriority)
                    return A.BasePriority > B.BasePriority;
                if (A.DistanceReduction != B.DistanceReduction)
                    return A.DistanceReduction > B.DistanceReduction;
                return A.ActorID < B.ActorID;
            });
    }

    // ④ 勝者確定
    FResolvedAction Winner;
    Winner.SourceActor = WinnerBucket[0].Actor;
    Winner.ActorID = WinnerBucket[0].ActorID;
    Winner.CurrentCell = WinnerBucket[0].CurrentCell;  // ★ 追加
    Winner.NextCell = WinnerBucket[0].Cell;
    Winner.FinalAbilityTag = WinnerBucket[0].AbilityTag;
    Winner.bIsWait = false;
    Winner.TimeSlot = WinnerBucket[0].TimeSlot;  // ★ 追加
    Winner.GenerationOrder = WinnerBucket[0].GenerationOrder;  // ★ 追加
    Winner.ResolutionReason = TEXT("Winner");
    Results.Add(Winner);

    // ⑤ 敗者のフォールバック
    for (int32 i = 1; i < WinnerBucket.Num(); ++i)
    {
        FResolvedAction Fallback = TryFallbackMove(WinnerBucket[i]);
        Results.Add(Fallback);
    }

    return Results;
}

bool UConflictResolverSubsystem::DetectAndAllowCycle(const TArray<FReservationEntry>& Applicants, TArray<FStableActorID>& OutCycle)
{
    // TODO: v2.2 サイクル検出（Phase 2後半で実装）
    // 現時点ではfalseを返す
    return false;
}

FResolvedAction UConflictResolverSubsystem::TryFallbackMove(const FReservationEntry& LoserEntry)
{
    // TODO: v2.2 フォールバック候補探索（Phase 2後半で実装）
    // 現時点ではWait降格
    return CreateWaitAction(LoserEntry);
}

FResolvedAction UConflictResolverSubsystem::CreateWaitAction(const FReservationEntry& Entry)
{
    // ★★★ Waitタグの取得（エラー対策版） ★★★
    static const FGameplayTag WaitTag = FGameplayTag::RequestGameplayTag(FName("AI.Intent.Wait"), false);

    FResolvedAction Wait;
    Wait.SourceActor = Entry.Actor;
    Wait.ActorID = Entry.ActorID;
    Wait.CurrentCell = Entry.CurrentCell;  // ★ 追加
    Wait.NextCell = FIntPoint(-1, -1);  // 移動なし
    Wait.FinalAbilityTag = WaitTag.IsValid() ? WaitTag : FGameplayTag();  // ★★★ Validチェック
    Wait.bIsWait = true;
    Wait.TimeSlot = Entry.TimeSlot;  // ★ 追加
    Wait.GenerationOrder = Entry.GenerationOrder;  // ★ 追加
    Wait.ResolutionReason = TEXT("Wait_Fallback");

    return Wait;
}
