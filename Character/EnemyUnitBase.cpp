// EnemyUnitBase.cpp
// Location: Rogue/Character/EnemyUnitBase.cpp

#include "EnemyUnitBase.h"
#include "Character/LyraPawnExtensionComponent.h"
#include "Character/LyraPawnData.h"
#include "Character/LyraHealthComponent.h"
#include "AbilitySystem/LyraAbilitySystemComponent.h"
#include "AbilitySystem/LyraAbilitySet.h"
#include "Player/LyraPlayerState.h"
#include "Character/LyraTBSAIController.h"
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
    EnemyThinkerComponent = ObjectInitializer.CreateDefaultSubobject<UEnemyThinkerBase>(this, TEXT("EnemyThinker"));

    // ★★★ ASCコンポーネントを作成（Pawn-owned） ★★★
    AbilitySystemComponent = ObjectInitializer.CreateDefaultSubobject<ULyraAbilitySystemComponent>(this, TEXT("AbilitySystemComponent"));
    AbilitySystemComponent->SetIsReplicated(true);
    AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

    // ★★★ 修正: コントローラーの自動スポーンを無効化 ★★★
    // UnitManagerがPawnData設定後に明示的にSpawnDefaultController()を呼び出す
    AutoPossessAI = EAutoPossessAI::Disabled;
    AIControllerClass = ALyraTBSAIController::StaticClass();
    bDeferredControllerSpawn = true;  // デフォルトで遅延スポーンを有効化

    UE_LOG(LogEnemyUnit, Log, TEXT("[Constructor] ASC created for enemy unit (deferred controller spawn enabled)"));
}

void AEnemyUnitBase::PostInitializeComponents()
{
    Super::PostInitializeComponents();

    // ★★★ 重要: PawnExtensionにAbilitySystem初期化コールバックを登録 ★★★
    // これがないとOnAbilitySystemInitialized()が呼ばれない！
    if (ULyraPawnExtensionComponent* PawnExt = FindComponentByClass<ULyraPawnExtensionComponent>())
    {
        PawnExt->OnAbilitySystemInitialized_RegisterAndCall(
            FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &ThisClass::OnAbilitySystemInitialized));
        PawnExt->OnAbilitySystemUninitialized_Register(
            FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &ThisClass::OnAbilitySystemUninitialized));

        UE_LOG(LogEnemyUnit, Log, TEXT("[PostInitializeComponents] ✅ ASC initialization callbacks registered"));
    }
    else
    {
        UE_LOG(LogEnemyUnit, Error, TEXT("[PostInitializeComponents] ❌ PawnExtension not found!"));
    }
}

void AEnemyUnitBase::PossessedBy(AController* NewController)
{
    UE_LOG(LogEnemyUnit, Log, TEXT("[PossessedBy] ========== START: %s =========="), *GetName());

    if (HasAuthority())
    {
        UE_LOG(LogEnemyUnit, Display, TEXT("[PossessedBy] EnemyPawnData=%s (before Super)"),
            EnemyPawnData ? *EnemyPawnData->GetName() : TEXT("NULL"));
    }

    ULyraPawnExtensionComponent* PawnExt = FindComponentByClass<ULyraPawnExtensionComponent>();
    if (HasAuthority() && PawnExt && EnemyPawnData && !PawnExt->GetPawnData<ULyraPawnData>())
    {
        PawnExt->SetPawnData(EnemyPawnData);
        UE_LOG(LogEnemyUnit, Log, TEXT("[PossessedBy] ✅ SetPawnData before Super: %s"), *EnemyPawnData->GetName());
    }

    Super::PossessedBy(NewController);

    if (!PawnExt)
    {
        UE_LOG(LogEnemyUnit, Error, TEXT("[PossessedBy] ❌ PawnExtension not found: %s"), *GetName());
        return;
    }

    if (AbilitySystemComponent)
    {
        PawnExt->InitializeAbilitySystem(AbilitySystemComponent, this);
        UE_LOG(LogEnemyUnit, Log, TEXT("[PossessedBy] ✅ ASC initialized via PawnExtension (Owner=%s)"),
            *GetNameSafe(this));
    }
    else
    {
        UE_LOG(LogEnemyUnit, Error, TEXT("[PossessedBy] ❌ AbilitySystemComponent is NULL"));
    }

    PawnExt->HandleControllerChanged();
    PawnExt->CheckDefaultInitialization();

    RefreshTeamFromController();
    // ★★★ 修正: GrantAbilitySetsIfNeeded() は OnAbilitySystemInitialized() で呼ばれる ★★★
    // ここでは呼ばない（二重付与を防ぐ）
    // ApplyEnemyGameplayTags() も OnAbilitySystemInitialized() で呼ばれる

    UE_LOG(LogEnemyUnit, Log, TEXT("[PossessedBy] ========== END: %s =========="), *GetName());
}

void AEnemyUnitBase::BeginPlay()
{
    Super::BeginPlay();

    // ★★★ 修正: bDeferredControllerSpawnがtrueの場合、コントローラースポーンをスキップ ★★★
    // UnitManagerがPawnData設定後に明示的にSpawnDefaultController()を呼び出す
    if (HasAuthority() && Controller == nullptr && !bDeferredControllerSpawn)
    {
        SpawnDefaultController();
    }

    if (ULyraPawnExtensionComponent* PawnExt = FindComponentByClass<ULyraPawnExtensionComponent>())
    {
        if (EnemyPawnData && HasAuthority() && !PawnExt->GetPawnData<ULyraPawnData>())
        {
            PawnExt->SetPawnData(EnemyPawnData);
            UE_LOG(LogEnemyUnit, Log, TEXT("[BeginPlay] ✅ SetPawnData: %s"), *EnemyPawnData->GetName());
        }

        PawnExt->CheckDefaultInitialization();
    }
    else
    {
        UE_LOG(LogEnemyUnit, Error, TEXT("[BeginPlay] ❌ PawnExtension not found on %s"), *GetName());
    }

    // ★★★ 修正: ApplyEnemyGameplayTags() は OnAbilitySystemInitialized() で呼ばれる ★★★

    if (HasAuthority())
    {
        // 次の Tick で GameplayReady チェック開始
        GetWorldTimerManager().SetTimerForNextTick(this, &AEnemyUnitBase::OnGameplayReady);
    }
}

void AEnemyUnitBase::OnRep_Controller()
{
    Super::OnRep_Controller();

    if (ULyraPawnExtensionComponent* PawnExt = FindComponentByClass<ULyraPawnExtensionComponent>())
    {
        PawnExt->HandleControllerChanged();
        PawnExt->CheckDefaultInitialization();
    }
}

void AEnemyUnitBase::OnAbilitySystemInitialized()
{
    ULyraAbilitySystemComponent* LyraASC = Cast<ULyraAbilitySystemComponent>(AbilitySystemComponent);
    if (!LyraASC)
    {
        UE_LOG(LogEnemyUnit, Error, TEXT("[OnAbilitySystemInitialized] ❌ ASC is NULL for %s"), *GetName());
        return;
    }

    // ★★★ 重要: AbilitySetを先に付与してからHealthComponentを初期化 ★★★
    // これにより HealthSet が ASC に確実に存在する状態で初期化できる
    GrantAbilitySetsIfNeeded();

    // デバッグ: AttributeSet が正しく付与されたか確認
    const TArray<UAttributeSet*>& AttrSets = LyraASC->GetSpawnedAttributes();
    UE_LOG(LogEnemyUnit, Log, TEXT("[OnAbilitySystemInitialized] ASC has %d AttributeSets"), AttrSets.Num());

    // HealthSet の存在確認
    bool bHasHealthSet = false;
    for (const UAttributeSet* AttrSet : AttrSets)
    {
        if (AttrSet && AttrSet->GetClass()->GetName().Contains(TEXT("Health")))
        {
            bHasHealthSet = true;
            UE_LOG(LogEnemyUnit, Log, TEXT("[OnAbilitySystemInitialized]   - Found HealthSet: %s"), *AttrSet->GetClass()->GetName());
            break;
        }
    }

    if (!bHasHealthSet)
    {
        UE_LOG(LogEnemyUnit, Error, TEXT("[OnAbilitySystemInitialized] ❌ HealthSet not found in AttributeSets!"));
    }

    // HealthComponentの初期化（HealthSet付与後）
    if (ULyraHealthComponent* HealthComp = FindComponentByClass<ULyraHealthComponent>())
    {
        if (bHasHealthSet)
        {
            HealthComp->InitializeWithAbilitySystem(LyraASC);
            UE_LOG(LogEnemyUnit, Log, TEXT("[OnAbilitySystemInitialized] ✅ HealthComponent initialized for %s"), *GetName());
        }
        else
        {
            UE_LOG(LogEnemyUnit, Error, TEXT("[OnAbilitySystemInitialized] ❌ Skipping HealthComponent init (no HealthSet)"));
        }
    }
    else
    {
        UE_LOG(LogEnemyUnit, Warning, TEXT("[OnAbilitySystemInitialized] ⚠️ HealthComponent not found for %s"), *GetName());
    }

    // 敵タグの適用
    ApplyEnemyGameplayTags();
}

void AEnemyUnitBase::OnAbilitySystemUninitialized()
{
    if (ULyraHealthComponent* HealthComp = FindComponentByClass<ULyraHealthComponent>())
    {
        HealthComp->UninitializeFromAbilitySystem();
        UE_LOG(LogEnemyUnit, Log, TEXT("[OnAbilitySystemUninitialized] ✅ HealthComponent uninitialized for %s"), *GetName());
    }
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
    // ★ Pawn-owned ASC をチェック
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

void AEnemyUnitBase::GrantAbilitySetsIfNeeded()
{
    if (!HasAuthority())
    {
        return;
    }

    if (bGrantedAbilitySets)
    {
        return;
    }

    if (!AbilitySystemComponent)
    {
        UE_LOG(LogEnemyUnit, Warning, TEXT("[GrantAbilitySets] ASC missing on %s"), *GetName());
        return;
    }

    if (!EnemyPawnData)
    {
        UE_LOG(LogEnemyUnit, Warning, TEXT("[GrantAbilitySets] EnemyPawnData missing on %s"), *GetName());
        return;
    }

    for (const ULyraAbilitySet* AbilitySet : EnemyPawnData->AbilitySets)
    {
        if (AbilitySet)
        {
            AbilitySet->GiveToAbilitySystem(AbilitySystemComponent, nullptr);
            UE_LOG(LogEnemyUnit, Log, TEXT("[GrantAbilitySets] AbilitySet granted: %s for %s"),
                *AbilitySet->GetName(), *GetName());
        }
    }

    UE_LOG(LogEnemyUnit, Log, TEXT("[GrantAbilitySets] ✅ Abilities=%d AttrSets=%d for %s"),
        AbilitySystemComponent->GetActivatableAbilities().Num(),
        AbilitySystemComponent->GetSpawnedAttributes().Num(),
        *GetName());

    bGrantedAbilitySets = true;
}

//------------------------------------------------------------------------------
// IAbilitySystemInterface Override
//------------------------------------------------------------------------------
UAbilitySystemComponent* AEnemyUnitBase::GetAbilitySystemComponent() const
{
    // ★★★ Pawn上のASCを返す（PlayerStateのASCではない） ★★★
    return AbilitySystemComponent;
}

void AEnemyUnitBase::SetEnemyPawnData(ULyraPawnData* InPawnData)
{
    if (InPawnData == nullptr)
    {
        UE_LOG(LogEnemyUnit, Warning, TEXT("[SetEnemyPawnData] InPawnData is NULL for %s"), *GetName());
        return;
    }

    if (EnemyPawnData == InPawnData)
    {
        return;
    }

    EnemyPawnData = InPawnData;
    UE_LOG(LogEnemyUnit, Log, TEXT("[SetEnemyPawnData] Assigned PawnData=%s to %s"),
        *EnemyPawnData->GetName(), *GetName());

    if (HasAuthority())
    {
        if (ULyraPawnExtensionComponent* PawnExt = FindComponentByClass<ULyraPawnExtensionComponent>())
        {
            if (!PawnExt->GetPawnData<ULyraPawnData>())
            {
                PawnExt->SetPawnData(EnemyPawnData);
                UE_LOG(LogEnemyUnit, Log, TEXT("[SetEnemyPawnData] Applied PawnData immediately on %s"),
                    *GetName());
            }
        }
    }
}
