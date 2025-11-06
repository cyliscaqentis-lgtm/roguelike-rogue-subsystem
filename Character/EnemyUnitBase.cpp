// EnemyUnitBase.cpp
// Location: Rogue/Character/EnemyUnitBase.cpp

#include "EnemyUnitBase.h"
#include "Character/LyraPawnExtensionComponent.h"
#include "Character/LyraPawnData.h"
#include "AbilitySystem/LyraAbilitySystemComponent.h"
#include "Player/LyraPlayerState.h"  
#include "TimerManager.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Utility/RogueGameplayTags.h"  // Ability/Phase/Gate等の集中定義
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogEnemyUnit, Log, All);

// 取得コストを避けるため、静的リクエストでキャッシュ
static const FGameplayTag TAG_Faction_Enemy = FGameplayTag::RequestGameplayTag(TEXT("Faction.Enemy"));

AEnemyUnitBase::AEnemyUnitBase(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
}

void AEnemyUnitBase::PossessedBy(AController* NewController)
{
    UE_LOG(LogEnemyUnit, Log, TEXT("[PossessedBy] ========== START: %s =========="), *GetName());

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // ★ステップ1: PawnData を設定
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    ULyraPawnExtensionComponent* PawnExt = FindComponentByClass<ULyraPawnExtensionComponent>();
    if (!PawnExt)
    {
        UE_LOG(LogEnemyUnit, Error, TEXT("[PossessedBy] ❌ PawnExtension not found: %s"), *GetName());
        return;
    }

    if (EnemyPawnData && !PawnExt->GetPawnData<ULyraPawnData>())
    {
        PawnExt->SetPawnData(EnemyPawnData);
        UE_LOG(LogEnemyUnit, Log, TEXT("[PossessedBy] ✅ SetPawnData: %s"), *EnemyPawnData->GetName());
    }

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // ★ステップ2: Super を呼ぶ
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    Super::PossessedBy(NewController);

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // ★ステップ3: InitState を進める（ASC は自動初期化）
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    PawnExt->HandleControllerChanged();
    PawnExt->CheckDefaultInitialization();

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // ★ステップ4: Team 同期
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    RefreshTeamFromController();

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // ★診断ログ + Avatar 設定（追加）
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // PS 取得（単一宣言に統一）
    APlayerState* PS = GetPlayerState<APlayerState>();
    if (!PS)
    {
        UE_LOG(LogEnemyUnit, Warning, TEXT("[PossessedBy] PlayerState not found on %s"), *GetName());
        return;
    }

    if (ALyraPlayerState* LyraPS = Cast<ALyraPlayerState>(PS))
    {
        UE_LOG(LogEnemyUnit, Log, TEXT("[PossessedBy] ✅ PlayerState: %s"), *LyraPS->GetName());

        if (ULyraAbilitySystemComponent* PS_ASC = LyraPS->GetLyraAbilitySystemComponent())
        {
            UE_LOG(LogEnemyUnit, Log, TEXT("[PossessedBy] ✅ PlayerState ASC found"));
            UE_LOG(LogEnemyUnit, Log, TEXT("[PossessedBy]    - Owner: %s"), *GetNameSafe(PS_ASC->GetOwnerActor()));
            UE_LOG(LogEnemyUnit, Log, TEXT("[PossessedBy]    - Avatar (Before): %s"), *GetNameSafe(PS_ASC->GetAvatarActor()));

            // ASC初期化は Controller 側 / OnRep_PlayerState に任せる
            // ★ PlayerState上ASCパターンでは、サーバ側で Owner=PS / Avatar=このPawn を明示初期化する
            UAbilitySystemComponent* ASC = nullptr;
            if (const IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(LyraPS))
            {
                ASC = ASI->GetAbilitySystemComponent();
            }
            if (!ASC)
            {
                ASC = LyraPS->FindComponentByClass<UAbilitySystemComponent>();
            }
            if (ASC)
            {
                ASC->InitAbilityActorInfo(LyraPS, this);
                UE_LOG(LogEnemyUnit, Log, TEXT("[PossessedBy] InitAbilityActorInfo: Owner=%s, Avatar=%s"),
                    *GetNameSafe(LyraPS), *GetNameSafe(this));
                if (ASC->GetAvatarActor_Direct() != this)
                {
                    UE_LOG(LogEnemyUnit, Warning, TEXT("[PossessedBy] Avatar mismatch: ASC has %s"),
                        *GetNameSafe(ASC->GetAvatarActor_Direct()));
                }
            }

            UE_LOG(LogEnemyUnit, Log, TEXT("[PossessedBy]    - Avatar (After): %s"), *GetNameSafe(PS_ASC->GetAvatarActor()));
            UE_LOG(LogEnemyUnit, Log, TEXT("[PossessedBy]    - Abilities: %d"), PS_ASC->GetActivatableAbilities().Num());
            UE_LOG(LogEnemyUnit, Log, TEXT("[PossessedBy]    - AttributeSets: %d"), PS_ASC->GetSpawnedAttributes().Num());
        }
        else
        {
            UE_LOG(LogEnemyUnit, Error, TEXT("[PossessedBy] ❌ PlayerState ASC is NULL"));
        }
    }
    else
    {
        UE_LOG(LogEnemyUnit, Error, TEXT("[PossessedBy] ❌ PlayerState NOT found"));
    }

    UE_LOG(LogEnemyUnit, Log, TEXT("[PossessedBy] ========== END: %s =========="), *GetName());
    
    // Team 設定の直後にタグを保証（ASCがこのタイミングで確実に存在）
    ApplyEnemyGameplayTags();
}

void AEnemyUnitBase::BeginPlay()
{
    Super::BeginPlay();
    // サーバ側で可能なら即付与（ASCが既に存在するケース）
    ApplyEnemyGameplayTags();

    if (HasAuthority())
    {
        // 次の Tick で GameplayReady チェック開始
        GetWorldTimerManager().SetTimerForNextTick(this, &AEnemyUnitBase::OnGameplayReady);
    }
}

void AEnemyUnitBase::OnRep_Controller()
{
    Super::OnRep_Controller();
    // クライアント側での視覚化/UI用に、タグが欠落していたら保険で要求
    // ※ 実際の付与はサーバのみ（二重付与防止）
}

void AEnemyUnitBase::ApplyEnemyGameplayTags()
{
    if (bEnemyTagsApplied)
    {
        return;
    }
    if (!HasAuthority())
    {
        // タグのソースオブトゥルースはサーバ。ここでは何もしない。
        return;
    }

    // Pawn/Controller どちらにASCが乗っていても拾えるユーティリティ
    UAbilitySystemComponent* ASC =
        UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(this);

    if (!ASC)
    {
        // ASC生成が一瞬遅いプロジェクト構成に備え、ワンショット遅延で再試行
        FTimerHandle Handle;
        GetWorldTimerManager().SetTimer(
            Handle,
            [this]() { ApplyEnemyGameplayTags(); },
            0.0f, false);
        return;
    }

    // 既に同タグがあるなら何もしない（冪等）
    if (ASC->HasMatchingGameplayTag(TAG_Faction_Enemy))
    {
        bEnemyTagsApplied = true;
        return;
    }

    FGameplayTagContainer EnemyTags;
    EnemyTags.AddTag(TAG_Faction_Enemy);
    ASC->AddLooseGameplayTags(EnemyTags); // ★ サーバで付与 → ASCがレプリケート

    bEnemyTagsApplied = true;
    UE_LOG(LogTemp, Log, TEXT("[EnemyUnitBase] Added %s to %s"),
        *TAG_Faction_Enemy.ToString(), *GetName());
}

void AEnemyUnitBase::OnGameplayReady()
{
    // ★ UnitBase::GetAbilitySystemComponent() を使用
    // これで常にPlayerStateのASCが返される
    ULyraAbilitySystemComponent* CurrentASC = Cast<ULyraAbilitySystemComponent>(GetAbilitySystemComponent());

    const bool bReady =
        CurrentASC &&
        CurrentASC->GetActivatableAbilities().Num() > 0 &&
        CurrentASC->GetSpawnedAttributes().Num() > 0;

    if (!bReady)
    {
        ++RetryCount;
        if (RetryCount < MaxRetryCount)
        {
            FTimerHandle RetryHandle;
            GetWorldTimerManager().SetTimer(RetryHandle, this, &AEnemyUnitBase::OnGameplayReady, 0.1f, false);
            return;
        }
        else
        {
            UE_LOG(LogEnemyUnit, Error, TEXT("[GameplayReady] ❌ Timeout for %s (RetryCount=%d)"),
                *GetName(), RetryCount);
            UE_LOG(LogEnemyUnit, Error, TEXT("[GameplayReady]    ASC=%s, Abilities=%d, AttrSets=%d"),
                CurrentASC ? TEXT("Valid") : TEXT("NULL"),
                CurrentASC ? CurrentASC->GetActivatableAbilities().Num() : 0,
                CurrentASC ? CurrentASC->GetSpawnedAttributes().Num() : 0);
            return;
        }
    }

#if !UE_BUILD_SHIPPING
    UE_LOG(LogEnemyUnit, Log, TEXT("[GameplayReady] ✅ %s: Abilities=%d, AttrSets=%d, RetryCount=%d"),
        *GetName(),
        CurrentASC->GetActivatableAbilities().Num(),
        CurrentASC->GetSpawnedAttributes().Num(),
        RetryCount);
#endif
}

