// UnitBase.cpp
// Location: Rogue/Character/UnitBase.cpp

#include "UnitBase.h"
#include "AbilitySystem/LyraAbilitySystemComponent.h"
#include "AbilitySystem/LyraAbilitySet.h"
#include "Player/LyraPlayerState.h"
#include "GenericTeamAgentInterface.h"
#include "AIController.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Animation/AnimInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Character/UnitManager.h"
#include "Character/LyraPawnExtensionComponent.h"
#include "Character/LyraHealthComponent.h"
#include "Character/LyraPawnData.h"
#include "Character/UnitMovementComponent.h"
#include "Character/UnitUIComponent.h"
#include "Grid/GridOccupancySubsystem.h"
#include "Grid/GridPathfindingLibrary.h"

// 繝ｭ繧ｰ繧ｫ繝・ざ繝ｪ螳夂ｾｩ
DEFINE_LOG_CATEGORY_STATIC(LogUnitBase, Log, All);

//------------------------------------------------------------------------------
// 繧ｳ繝ｳ繧ｹ繝医Λ繧ｯ繧ｿ
//------------------------------------------------------------------------------
AUnitBase::AUnitBase(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    PrimaryActorTick.bCanEverTick = true;

    //==========================================================================
    // ★★★ 新規: Component初期化（2025-11-09リファクタリング） ★★★
    //==========================================================================

    // 移動処理Component
    MovementComp = CreateDefaultSubobject<UUnitMovementComponent>(TEXT("UnitMovementComp"));

    // UI更新Component
    UIComp = CreateDefaultSubobject<UUnitUIComponent>(TEXT("UnitUIComp"));
}

//------------------------------------------------------------------------------
// PostInitializeComponents - AbilitySystem初期化コールバック登録
//------------------------------------------------------------------------------
void AUnitBase::PostInitializeComponents()
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

        UE_LOG(LogUnitBase, Warning, TEXT("[PostInitializeComponents] ✅ ASC callbacks registered for %s"), *GetName());
    }
    else
    {
        UE_LOG(LogUnitBase, Error, TEXT("[PostInitializeComponents] ❌ PawnExtension not found on %s!"), *GetName());
    }
}

//------------------------------------------------------------------------------
// OnAbilitySystemInitialized - ASC初期化完了時のコールバック
//------------------------------------------------------------------------------
void AUnitBase::OnAbilitySystemInitialized()
{
    UE_LOG(LogUnitBase, Warning, TEXT("[OnAbilitySystemInitialized] ========== START: %s =========="), *GetName());

    UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
    if (!ASC)
    {
        UE_LOG(LogUnitBase, Error, TEXT("[OnAbilitySystemInitialized] ❌ ASC is NULL for %s"), *GetName());
        return;
    }

    ULyraAbilitySystemComponent* LyraASC = Cast<ULyraAbilitySystemComponent>(ASC);
    if (!LyraASC)
    {
        UE_LOG(LogUnitBase, Error, TEXT("[OnAbilitySystemInitialized] ❌ Not a LyraASC for %s"), *GetName());
        return;
    }

    // ★★★ 重要: AbilitySetを先に付与してからHealthComponentを初期化 ★★★
    GrantAbilitySetsIfNeeded();

    // デバッグ: AttributeSet が正しく付与されたか確認
    const TArray<UAttributeSet*>& AttrSets = LyraASC->GetSpawnedAttributes();
    UE_LOG(LogUnitBase, Warning, TEXT("[OnAbilitySystemInitialized] ASC has %d AttributeSets"), AttrSets.Num());

    // HealthSet の存在確認
    bool bHasHealthSet = false;
    for (const UAttributeSet* AttrSet : AttrSets)
    {
        if (AttrSet && AttrSet->GetClass()->GetName().Contains(TEXT("Health")))
        {
            bHasHealthSet = true;
            UE_LOG(LogUnitBase, Log, TEXT("[OnAbilitySystemInitialized]   - Found HealthSet: %s"), *AttrSet->GetClass()->GetName());
            break;
        }
    }

    if (!bHasHealthSet)
    {
        UE_LOG(LogUnitBase, Warning, TEXT("[OnAbilitySystemInitialized] ⚠️ HealthSet not found (may be okay for some units)"));
    }

    // HealthComponentの初期化（HealthSet付与後）
    if (ULyraHealthComponent* HealthComp = FindComponentByClass<ULyraHealthComponent>())
    {
        if (bHasHealthSet)
        {
            HealthComp->InitializeWithAbilitySystem(LyraASC);
            UE_LOG(LogUnitBase, Log, TEXT("[OnAbilitySystemInitialized] ✅ HealthComponent initialized for %s"), *GetName());
        }
        else
        {
            UE_LOG(LogUnitBase, Warning, TEXT("[OnAbilitySystemInitialized] ⚠️ Skipping HealthComponent init (no HealthSet)"));
        }
    }

    UE_LOG(LogUnitBase, Warning, TEXT("[OnAbilitySystemInitialized] ========== END: %s =========="), *GetName());
}

//------------------------------------------------------------------------------
// OnAbilitySystemUninitialized - ASC破棄時のコールバック
//------------------------------------------------------------------------------
void AUnitBase::OnAbilitySystemUninitialized()
{
    if (ULyraHealthComponent* HealthComp = FindComponentByClass<ULyraHealthComponent>())
    {
        HealthComp->UninitializeFromAbilitySystem();
        UE_LOG(LogUnitBase, Log, TEXT("[OnAbilitySystemUninitialized] ✅ HealthComponent uninitialized for %s"), *GetName());
    }
}

//------------------------------------------------------------------------------
// GrantAbilitySetsIfNeeded - PawnDataからAbilitySetを付与
//------------------------------------------------------------------------------
void AUnitBase::GrantAbilitySetsIfNeeded()
{
    if (!HasAuthority())
    {
        return;
    }

    if (bGrantedAbilitySets)
    {
        UE_LOG(LogUnitBase, Verbose, TEXT("[GrantAbilitySets] Already granted for %s"), *GetName());
        return;
    }

    UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
    if (!ASC)
    {
        UE_LOG(LogUnitBase, Warning, TEXT("[GrantAbilitySets] ASC missing on %s"), *GetName());
        return;
    }

    // ★★★ CRITICAL GUARD: Check if abilities are already granted by another system (2025-11-09) ★★★
    if (ASC->GetActivatableAbilities().Num() > 0)
    {
        UE_LOG(LogUnitBase, Warning,
            TEXT("[GrantAbilitySets] ⚠️ SKIP: ASC already has %d abilities (granted by PawnExtension/HeroComponent?) for %s"),
            ASC->GetActivatableAbilities().Num(), *GetName());
        bGrantedAbilitySets = true; // Mark as granted to prevent future attempts
        return;
    }

    // PawnDataをPawnExtensionから取得
    const ULyraPawnData* PawnData = nullptr;
    if (ULyraPawnExtensionComponent* PawnExt = FindComponentByClass<ULyraPawnExtensionComponent>())
    {
        PawnData = PawnExt->GetPawnData<ULyraPawnData>();
    }

    if (!PawnData)
    {
        UE_LOG(LogUnitBase, Warning, TEXT("[GrantAbilitySets] PawnData missing on %s (may be set later)"), *GetName());
        return;
    }

    // ASCをLyraASCにキャスト
    ULyraAbilitySystemComponent* LyraASC = Cast<ULyraAbilitySystemComponent>(ASC);
    if (!LyraASC)
    {
        UE_LOG(LogUnitBase, Error, TEXT("[GrantAbilitySets] ASC is not a LyraASC on %s"), *GetName());
        return;
    }

    // AbilitySetsを付与
    int32 AbilityCount = 0;
    for (const ULyraAbilitySet* AbilitySet : PawnData->AbilitySets)
    {
        if (AbilitySet)
        {
            AbilitySet->GiveToAbilitySystem(LyraASC, nullptr);
            AbilityCount++;
            UE_LOG(LogUnitBase, Log, TEXT("[GrantAbilitySets] AbilitySet granted: %s for %s"),
                *AbilitySet->GetName(), *GetName());
        }
    }

    UE_LOG(LogUnitBase, Warning, TEXT("[GrantAbilitySets] ✅ Abilities=%d AttrSets=%d for %s"),
        LyraASC->GetActivatableAbilities().Num(),
        LyraASC->GetSpawnedAttributes().Num(),
        *GetName());

    // デバッグ：Grantされた全Abilityをリスト表示
    const TArray<FGameplayAbilitySpec>& Abilities = LyraASC->GetActivatableAbilities();
    for (int32 i = 0; i < Abilities.Num(); ++i)
    {
        const FGameplayAbilitySpec& Spec = Abilities[i];
        if (Spec.Ability)
        {
            UE_LOG(LogUnitBase, Warning, TEXT("[GrantAbilitySets]   Ability[%d]: %s"), i, *Spec.Ability->GetName());
        }
    }

    bGrantedAbilitySets = true;
}

//------------------------------------------------------------------------------
// GetAbilitySystemComponent・・layerState縺ｮASC繧定ｿ斐☆・・
//------------------------------------------------------------------------------
UAbilitySystemComponent* AUnitBase::GetAbilitySystemComponent() const
{
    if (const ALyraPlayerState* PS = GetPlayerState<ALyraPlayerState>())
    {
        return PS->GetAbilitySystemComponent();
    }

    return Super::GetAbilitySystemComponent();
}

//------------------------------------------------------------------------------
// BeginPlay
//------------------------------------------------------------------------------
void AUnitBase::BeginPlay()
{
    Super::BeginPlay();

    // ★★★ 注意: ASC/Health初期化は OnAbilitySystemInitialized() で行われる ★★★
    // PostInitializeComponents() で登録したコールバックが自動的に呼ばれる

    // 笘・・笘・險ｺ譁ｭ・咤eginPlay蜑阪・蛟､繧堤｢ｺ隱・笘・・笘・
    UE_LOG(LogUnitBase, Verbose,
        TEXT("[BeginPlay] %s: BEFORE - bSkipMoveAnimation=%d, PixelsPerSec=%.1f, TickEnabled=%d"),
        *GetName(), bSkipMoveAnimation, PixelsPerSec, PrimaryActorTick.bCanEverTick);

    SetActorTickEnabled(true);

    // 笘・・笘・蠑ｷ蛻ｶ・壹い繝九Γ繝ｼ繧ｷ繝ｧ繝ｳ繧ｹ繧ｭ繝・・繧堤┌蜉ｹ蛹・笘・・笘・
    bSkipMoveAnimation = false;
    UE_LOG(LogUnitBase, Verbose,
        TEXT("[BeginPlay] %s: AFTER - FORCED bSkipMoveAnimation=FALSE, PixelsPerSec=%.1f, TickEnabled=%d"),
        *GetName(), PixelsPerSec, IsActorTickEnabled());

    // 繝槭ユ繝ｪ繧｢繝ｫ蛻晄悄蛹・
    EnsureDynamicMaterial();
    ApplyTeamColor();
    UpdateValidSelectionColor();
    if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
    {
        const float HalfHeight = GetCapsuleComponent()
            ? GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
            : 0.f;
        const float PlaneZ = GetActorLocation().Z - HalfHeight;

        MoveComp->SetPlaneConstraintEnabled(true);
        MoveComp->SetPlaneConstraintNormal(FVector::UpVector);
        MoveComp->SetPlaneConstraintOrigin(FVector(0.f, 0.f, PlaneZ));
        MoveComp->bSnapToPlaneAtStart = true;
        MoveComp->GravityScale = 0.f;
        MoveComp->SetMovementMode(MOVE_Flying);
    }


    // AI Controller 縺ｮ MoveBlockDetection 繧呈怏蜉ｹ蛹・
    if (AAIController* AI = Cast<AAIController>(GetController()))
    {
        AI->SetMoveBlockDetection(true);
    }

    // Team蛻晄悄蜷梧悄
    RefreshTeamFromController();
}

//------------------------------------------------------------------------------
// Tick
//------------------------------------------------------------------------------
void AUnitBase::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    // 笘・・笘・險ｺ譁ｭ・啜ick縺悟他縺ｰ繧後※縺・ｋ縺狗｢ｺ隱搾ｼ・oving譎ゅ・縺ｿ・・笘・・笘・
    static int32 TickCount = 0;
    if (MoveStatus == EUnitMoveStatus::Moving)
    {
        if (TickCount == 0)
        {
            UE_LOG(LogUnitBase, Verbose, TEXT("[Tick] %s: Moving status detected, starting UpdateMove"), *GetName());
        }
        TickCount++;
        UpdateMove(DeltaSeconds);
    }
    else
    {
        TickCount = 0;
    }
}

//------------------------------------------------------------------------------
// Team蜷梧悄
//------------------------------------------------------------------------------
void AUnitBase::OnRep_Controller()
{
    Super::OnRep_Controller();
    RefreshTeamFromController();
}

void AUnitBase::RefreshTeamFromController()
{
    if (const IGenericTeamAgentInterface* TeamAgent = Cast<IGenericTeamAgentInterface>(GetController()))
    {
        const int32 NewTeam = TeamAgent->GetGenericTeamId().GetId();

        if (Team != NewTeam)
        {
            Team = NewTeam;
            ApplyTeamColor();
            UpdateValidSelectionColor();
        }
    }
}

//------------------------------------------------------------------------------
// MoveUnit・育ｧｻ蜍暮幕蟋具ｼ・
//------------------------------------------------------------------------------
void AUnitBase::MoveUnit(const TArray<FVector>& InPath)
{
    PathArray = InPath;

    // 笘・・笘・譛蜆ｪ蜈茨ｼ壹い繝九Γ繝ｼ繧ｷ繝ｧ繝ｳ繧ｹ繧ｭ繝・・繧堤ｵｶ蟇ｾ縺ｫfalse縺ｫ縺吶ｋ 笘・・笘・
    bSkipMoveAnimation = false;
    SetActorTickEnabled(true);
    
    // 笘・・笘・邱頑･險ｺ譁ｭ・哺oveUnit蜻ｼ縺ｳ蜃ｺ縺玲凾縺ｮ迥ｶ諷九ｒ遒ｺ隱・笘・・笘・
    UE_LOG(LogUnitBase, Log,
        TEXT("[MoveUnit] %s CALLED: PathNum=%d, bSkipMoveAnimation=%d (FORCED TO 0!), PixelsPerSec=%.1f, TickEnabled=%d"),
        *GetName(), PathArray.Num(), bSkipMoveAnimation, PixelsPerSec, IsActorTickEnabled());

    // 遘ｻ蜍戊ｷ晞屬荳企剞縺ｧ蛻・ｊ隧ｰ繧・
    if (PathArray.Num() > CurrentMovementRange)
    {
        PathArray.SetNum(CurrentMovementRange);
    }

    // 繝代せ縺檎ｩｺ縺ｮ蝣ｴ蜷医・蜊ｳ蠎ｧ縺ｫ螳御ｺ・
    if (PathArray.Num() == 0)
    {
        UE_LOG(LogUnitBase, Warning, TEXT("[MoveUnit] %s: Empty path!"), *GetName());
        MoveStatus = EUnitMoveStatus::Idle;
        CurrentVelocity = FVector::ZeroVector;

        // ✅ Tickを無効化
        SetActorTickEnabled(false);

        UE_LOG(LogUnitBase, Verbose,
            TEXT("[MoveComplete] %s finished MoveUnit path"), *GetName());

        UE_LOG(LogUnitBase, Log,
            TEXT("[MoveComplete] 笘・・笘・Unit %s MOVE ANIMATION COMPLETED! (Broadcast OnMoveFinished)"),
            *GetName());

        // ★★★ 2025-11-10: 移動完了後にGridOccupancySubsystemを更新（プレイヤー・敵共通） ★★★
        if (UWorld* World = GetWorld())
        {
            if (AGridPathfindingLibrary* PathFinder = Cast<AGridPathfindingLibrary>(
                UGameplayStatics::GetActorOfClass(World, AGridPathfindingLibrary::StaticClass())))
            {
                if (UGridOccupancySubsystem* OccSys = World->GetSubsystem<UGridOccupancySubsystem>())
                {
                    const FIntPoint CurrentCell = PathFinder->WorldToGrid(GetActorLocation());
                    bool bUpdateSuccess = OccSys->UpdateActorCell(this, CurrentCell);
                    if (bUpdateSuccess)
                    {
                        UE_LOG(LogUnitBase, Log,
                            TEXT("[MoveComplete] GridOccupancy updated: Actor=%s Cell=(%d,%d)"),
                            *GetName(), CurrentCell.X, CurrentCell.Y);
                    }
                    else
                    {
                        UE_LOG(LogUnitBase, Error,
                            TEXT("[MoveComplete] CRITICAL: GridOccupancy update FAILED for %s to (%d,%d) - cell occupied!"),
                            *GetName(), CurrentCell.X, CurrentCell.Y);
                    }
                }
            }
        }

        // 笘・・笘・繝・Μ繧ｲ繝ｼ繝・Broadcast・磯㍾隕・ｼ・ｼ・笘・・笘・
        OnMoveFinished.Broadcast(this);
        return;
    }

    MoveCounter = 0;
    StartNextLeg();
}

//------------------------------------------------------------------------------
// StartNextLeg・域ｬ｡縺ｮ遘ｻ蜍募玄髢薙ｒ髢句ｧ具ｼ・
//------------------------------------------------------------------------------
void AUnitBase::StartNextLeg()
{
    // 笘・・笘・繝代せ縺檎ｵゆｺ・＠縺溘ｉ螳御ｺ・夂衍 笘・・笘・
    if (!PathArray.IsValidIndex(MoveCounter))
    {
        MoveStatus = EUnitMoveStatus::Idle;
        CurrentVelocity = FVector::ZeroVector;

        // ✅ 移動完了時にTickを無効化
        SetActorTickEnabled(false);

        UE_LOG(LogUnitBase, Verbose,
            TEXT("[MoveComplete] %s finished MoveUnit path"), *GetName());

        UE_LOG(LogUnitBase, Log,
            TEXT("[MoveComplete] 笘・・笘・Unit %s MOVE ANIMATION COMPLETED! (Broadcast OnMoveFinished)"),
            *GetName());

        // ★★★ 2025-11-10: 移動完了後にGridOccupancySubsystemを更新（プレイヤー・敵共通） ★★★
        if (UWorld* World = GetWorld())
        {
            if (AGridPathfindingLibrary* PathFinder = Cast<AGridPathfindingLibrary>(
                UGameplayStatics::GetActorOfClass(World, AGridPathfindingLibrary::StaticClass())))
            {
                if (UGridOccupancySubsystem* OccSys = World->GetSubsystem<UGridOccupancySubsystem>())
                {
                    const FIntPoint CurrentCell = PathFinder->WorldToGrid(GetActorLocation());
                    bool bUpdateSuccess = OccSys->UpdateActorCell(this, CurrentCell);
                    if (bUpdateSuccess)
                    {
                        UE_LOG(LogUnitBase, Log,
                            TEXT("[MoveComplete] GridOccupancy updated: Actor=%s Cell=(%d,%d)"),
                            *GetName(), CurrentCell.X, CurrentCell.Y);
                    }
                    else
                    {
                        UE_LOG(LogUnitBase, Error,
                            TEXT("[MoveComplete] CRITICAL: GridOccupancy update FAILED for %s to (%d,%d) - cell occupied!"),
                            *GetName(), CurrentCell.X, CurrentCell.Y);
                    }
                }
            }
        }

        // 笘・・笘・繝・Μ繧ｲ繝ｼ繝・Broadcast・磯㍾隕・ｼ・ｼ・笘・・笘・
        OnMoveFinished.Broadcast(this);

        UE_LOG(LogUnitBase, Log, TEXT("[StartNextLeg] %s: Movement completed!"), *GetName());
        return;
    }

    // 谺｡縺ｮ逶ｮ讓咏せ縺ｸ遘ｻ蜍暮幕蟋・
    LegStart = GetActorLocation();
    LegEnd = PathArray[MoveCounter];
    LegEnd.Z = LegStart.Z; // 2D 遘ｻ蜍・

    // 霍晞屬縺・縺ｮ蝣ｴ蜷医・谺｡縺ｮ繧ｦ繧ｧ繧､繝昴う繝ｳ繝医∈繧ｹ繧ｭ繝・・
    const float Distance = FVector::Dist2D(LegStart, LegEnd);
    if (Distance < 0.1f)
    {
        ++MoveCounter;
        StartNextLeg(); // 蜀榊ｸｰ縺ｧ谺｡縺ｮ蛹ｺ髢薙∈
        return;
    }

    LegAlpha = 0.f;
    MoveStatus = EUnitMoveStatus::Moving;
    
    // 笘・・笘・譛邨る亟蠕｡・壹％縺薙〒繧ゅ≧荳蠎ｦfalse繧貞ｼｷ蛻ｶ 笘・・笘・
    bSkipMoveAnimation = false;

    // 笘・・笘・險ｺ譁ｭ繝ｭ繧ｰ・壹い繝九Γ繝ｼ繧ｷ繝ｧ繝ｳ迥ｶ諷九ｒ遒ｺ隱・笘・・笘・
    UE_LOG(LogUnitBase, Warning,
        TEXT("[StartNextLeg] %s: bSkipMoveAnimation=%d (FORCED FALSE), Distance=%.1f, Start=%s, End=%s"),
        *GetName(), bSkipMoveAnimation, Distance, *LegStart.ToCompactString(), *LegEnd.ToCompactString());

    // 繧｢繝九Γ繝ｼ繧ｷ繝ｧ繝ｳ繧ｹ繧ｭ繝・・縺ｪ繧牙叉蠎ｧ縺ｫ遘ｻ蜍包ｼ医％縺ｮ繧ｳ繝ｼ繝峨ヱ繧ｹ縺ｯ邨ｶ蟇ｾ縺ｫ螳溯｡後＆繧後↑縺・・縺夲ｼ・
    if (bSkipMoveAnimation)
    {
        UE_LOG(LogUnitBase, Warning,
            TEXT("[StartNextLeg] %s: 笶・INSTANT TELEPORT (bSkipMoveAnimation=TRUE) - THIS SHOULD NOT HAPPEN!"),
            *GetName());
        SetActorLocation(LegEnd);
        ++MoveCounter;
        StartNextLeg(); // 蜀榊ｸｰ
    }
    else
    {
        // Verbose: 豁｣蟶ｸ縺ｪ繧｢繝九Γ繝ｼ繧ｷ繝ｧ繝ｳ髢句ｧ九・蜀鈴聞
        UE_LOG(LogUnitBase, Verbose,
            TEXT("[StartNextLeg] %s: 笨・Smooth animation started (bSkipMoveAnimation=FALSE, Dist=%.1f, Speed=%.1f)"),
            *GetName(), Distance, PixelsPerSec);
    }
}

//------------------------------------------------------------------------------
// UpdateMove・育ｧｻ蜍穂ｸｭ縺ｮ譖ｴ譁ｰ・・
//------------------------------------------------------------------------------
void AUnitBase::UpdateMove(float DeltaSeconds)
{
    const float Dist = FVector::Dist2D(LegStart, LegEnd);
    const float Duration = FMath::Max(0.001f, Dist / PixelsPerSec);
    const float OldAlpha = LegAlpha;
    LegAlpha = FMath::Min(1.f, LegAlpha + DeltaSeconds / Duration);

    const FVector OldLoc = GetActorLocation();
    const FVector NewLoc = FMath::Lerp(LegStart, LegEnd, LegAlpha);

    // 速度と向きを計算して更新
    if (DeltaSeconds > 0.f)
    {
        CurrentVelocity = (NewLoc - OldLoc) / DeltaSeconds;

        // 停止している場合は向きを変えない
        if (!CurrentVelocity.IsNearlyZero())
        {
            // Z軸の傾きを無視した水平な向きを計算
            const FRotator NewRotation = FRotator(0.f, CurrentVelocity.Rotation().Yaw, 0.f);
            SetActorRotation(NewRotation);
        }
    }
    else
    {
        CurrentVelocity = FVector::ZeroVector;
    }

    SetActorLocation(NewLoc, false);
    const FVector ActualLoc = GetActorLocation();

    // 笘・・笘・邱頑･險ｺ譁ｭ・壼ｮ滄圀縺ｫ菴咲ｽｮ縺悟､峨ｏ縺｣縺ｦ縺・ｋ縺狗｢ｺ隱・笘・・笘・
    static int32 UpdateCount = 0;
    if (UpdateCount < 5)
    {
        const FString OldLocStr = OldLoc.ToCompactString();
        const FString NewLocStr = NewLoc.ToCompactString();
        const FString ActualLocStr = ActualLoc.ToCompactString();

        UE_LOG(LogUnitBase, Verbose,
            TEXT("[UpdateMove] %s: Alpha %.3f -> %.3f, OldLoc=%s, NewLoc=%s, ActualLoc=%s, Moved=%.2f"),
            *GetName(), OldAlpha, LegAlpha,
            *OldLocStr, *NewLocStr, *ActualLocStr,
            FVector::Dist(OldLoc, ActualLoc));
        UpdateCount++;
    }

    // 笘・・笘・險ｺ譁ｭ繝ｭ繧ｰ・域怙蛻昴・繝輔Ξ繝ｼ繝縺ｮ縺ｿ・・笘・・笘・
    if (LegAlpha < 0.1f)
    {
        UE_LOG(LogUnitBase, Verbose,
            TEXT("[UpdateMove] %s: Distance=%.1f, Duration=%.2fs, Speed=%.1f units/s"),
            *GetName(), Dist, Duration, PixelsPerSec);
    }

    // 蛻ｰ驕泌愛螳・
    const float DistToTarget = FVector::Dist2D(GetActorLocation(), LegEnd);

    // 笘・・笘・Sparky險ｺ譁ｭ・壼芦驕泌愛螳壹・隧ｳ邏ｰ繝ｭ繧ｰ 笘・・笘・
    UE_LOG(LogUnitBase, Verbose,
        TEXT("[UpdateMove] 笘・%s: DistToTarget=%.2f, DistanceSnap=%.2f, LegAlpha=%.3f, Reached=%d"),
        *GetName(), DistToTarget, DistanceSnap, LegAlpha,
        (DistToTarget <= DistanceSnap || LegAlpha >= 1.f) ? 1 : 0);

    if (DistToTarget <= DistanceSnap || LegAlpha >= 1.f)
    {
        UE_LOG(LogUnitBase, Log,
            TEXT("[UpdateMove] 笘・・笘・%s: TARGET REACHED! Calling StartNextLeg (MoveCounter=%d, PathArray.Num=%d)"),
            *GetName(), MoveCounter, PathArray.Num());
        ++MoveCounter;
        StartNextLeg();
    }
}

//------------------------------------------------------------------------------
// 繝薙ず繝･繧｢繝ｫ蛻ｶ蠕｡
//------------------------------------------------------------------------------
void AUnitBase::EnsureDynamicMaterial()
{
    if (DynMat) return;

    USkeletalMeshComponent* MeshComp = GetMesh();
    if (!MeshComp)
    {
        UE_LOG(LogUnitBase, Warning, TEXT("[EnsureDynamicMaterial] No MeshComponent!"));
        return;
    }

    UMaterialInterface* BaseMat = MeshComp->GetMaterial(0);
    if (BaseMat)
    {
        DynMat = UMaterialInstanceDynamic::Create(BaseMat, this);
        MeshComp->SetMaterial(0, DynMat);

        // 蛻晄悄蛟､險ｭ螳・
        DynMat->SetScalarParameterValue(Param_Selected, 0.f);
        DynMat->SetScalarParameterValue(Param_Highlight, 0.f);
    }
}

void AUnitBase::ApplyTeamColor()
{
    if (!DynMat) return;

    const FLinearColor Color = (Team == 0) ? Team0Color : TeamNColor;
    DynMat->SetVectorParameterValue(Param_BodyColor, Color);
}

void AUnitBase::UpdateValidSelectionColor()
{
    if (!DynMat) return;

    const FLinearColor ValidColor = (Team > 0)
        ? FLinearColor(1.f, 0.069f, 0.06f, 1.f)  // 襍､・域雰・・
        : FLinearColor(0.062f, 0.085f, 1.f, 1.f); // 髱抵ｼ亥袖譁ｹ・・

    DynMat->SetVectorParameterValue(Param_ValidSelection, ValidColor);
}

void AUnitBase::ApplyMovementSpeedFromStats()
{
    const float StatSpeed =
        (StatBlock.CurrentSpeed > KINDA_SMALL_NUMBER) ? StatBlock.CurrentSpeed :
        (StatBlock.MaxSpeed > KINDA_SMALL_NUMBER ? StatBlock.MaxSpeed : PixelsPerSec);

    const float TargetSpeed = FMath::Clamp(StatSpeed, MinPixelsPerSec, MaxPixelsPerSec);
    if (!FMath::IsNearlyEqual(TargetSpeed, PixelsPerSec))
    {
        UE_LOG(LogUnitBase, Display,
            TEXT("[MovementSpeed] %s: PixelsPerSec %.1f -> %.1f (Stat=%.1f)"),
            *GetName(), PixelsPerSec, TargetSpeed, StatSpeed);
    }

    PixelsPerSec = TargetSpeed;
}

void AUnitBase::SetSelected(bool bNewSelected)
{
    EnsureDynamicMaterial();
    if (DynMat)
    {
        DynMat->SetScalarParameterValue(Param_Selected, bNewSelected ? 1.f : 0.f);
    }
}

void AUnitBase::SetHighlighted(bool bNewHighlighted)
{
    EnsureDynamicMaterial();
    if (DynMat)
    {
        DynMat->SetScalarParameterValue(Param_Highlight, bNewHighlighted ? 1.f : 0.f);
    }
}

//------------------------------------------------------------------------------
// 繧ｫ繝ｼ繧ｽ繝ｫ蜿榊ｿ・
//------------------------------------------------------------------------------
void AUnitBase::NotifyActorBeginCursorOver()
{
    SetHighlighted(true);
    UpdateValidSelectionColor();
    Super::NotifyActorBeginCursorOver();
}

void AUnitBase::NotifyActorEndCursorOver()
{
    SetHighlighted(false);
    Super::NotifyActorEndCursorOver();
}

//------------------------------------------------------------------------------
// 繧ｿ繧､繝ｫ邂｡逅・
//------------------------------------------------------------------------------
void AUnitBase::AdjustTile()
{
    // 蜿､縺・ち繧､繝ｫ縺ｮ Team 繧偵Μ繧ｻ繝・ヨ
    if (TileIAmOn)
    {
        if (UClass* TileClass = TileIAmOn->GetClass())
        {
            if (FIntProperty* TeamProp = FindFProperty<FIntProperty>(TileClass, TEXT("Team")))
            {
                int32 ResetValue = -1;
                TeamProp->SetPropertyValue_InContainer(TileIAmOn, ResetValue);
            }
        }
    }

    // 迴ｾ蝨ｨ菴咲ｽｮ縺ｮ繧ｿ繧､繝ｫ繧・LineTrace 縺ｧ讀懷・
    FVector Start = GetActorLocation() + FVector(0, 0, 25);
    FVector End = GetActorLocation() - FVector(0, 0, 250);

    FHitResult HitResult;
    FCollisionObjectQueryParams ObjectQueryParams;
    ObjectQueryParams.AddObjectTypesToQuery(ECC_GameTraceChannel1);

    FCollisionQueryParams QueryParams;
    QueryParams.bTraceComplex = false;

    bool bHit = GetWorld()->LineTraceSingleByObjectType(
        HitResult, Start, End, ObjectQueryParams, QueryParams
    );

    if (bHit && HitResult.GetActor())
    {
        TileIAmOn = HitResult.GetActor();

        // 譁ｰ縺励＞繧ｿ繧､繝ｫ縺ｫ Team 繧定ｨｭ螳・
        if (UClass* TileClass = TileIAmOn->GetClass())
        {
            if (FIntProperty* TeamProp = FindFProperty<FIntProperty>(TileClass, TEXT("Team")))
            {
                TeamProp->SetPropertyValue_InContainer(TileIAmOn, Team);
            }
        }
    }
}

//------------------------------------------------------------------------------
// 繝ｦ繝九ャ繝域､懷・
//------------------------------------------------------------------------------
TArray<AUnitBase*> AUnitBase::GetAdjacentPlayers() const
{
    TArray<AUnitBase*> Result;

    // 8譁ｹ蜷代・繧ｪ繝輔そ繝・ヨ
    static const FVector2D Directions[8] = {
        FVector2D(0, 1), FVector2D(1, 1), FVector2D(1, 0), FVector2D(1, -1),
        FVector2D(0, -1), FVector2D(-1, -1), FVector2D(-1, 0), FVector2D(-1, 1)
    };

    const FVector MyLocation = GetActorLocation();
    const float SearchRadius = 60.f;

    // 蜈ｨ縺ｦ縺ｮ UnitBase 繧貞叙蠕・
    TArray<AActor*> AllUnits;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), AUnitBase::StaticClass(), AllUnits);

    // 8譁ｹ蜷代ｒ繝√ぉ繝・け
    for (const FVector2D& Dir : Directions)
    {
        FVector AdjacentPos(
            MyLocation.X + (Dir.X * GridSize),
            MyLocation.Y + (Dir.Y * -GridSize),
            MyLocation.Z
        );

        for (AActor* Actor : AllUnits)
        {
            if (Actor == this) continue;

            AUnitBase* Unit = Cast<AUnitBase>(Actor);
            if (!Unit || Unit->Team != 0) continue;

            const float Dist = FVector::Dist2D(Unit->GetActorLocation(), AdjacentPos);
            if (Dist < SearchRadius)
            {
                Result.AddUnique(Unit);
            }
        }
    }

    return Result;
}

//------------------------------------------------------------------------------
// SetStatVars - UnitManager縺九ｉ蜻ｼ縺ｰ繧後ｋ繧ｹ繝・・繧ｿ繧ｹ蜿肴丐蜃ｦ逅・
//------------------------------------------------------------------------------
void AUnitBase::SetStatVars()
{
    // StatBlock縺九ｉ蜷・Γ繝ｳ繝仙､画焚縺ｸ繧ｳ繝斐・・・nitManager縺御ｽｿ逕ｨ・・
    // 螳溯｣・・UnitManager縺ｮ隕∵ｱゅ↓蜷医ｏ縺帙※隱ｿ謨ｴ縺励※縺上□縺輔＞
    Team = StatBlock.Team;
    CurrentMovementRange = FMath::Max(1, StatBlock.CurrentTotalMovementRange);
    
    MeleeBaseAttack = StatBlock.MeleeBaseAttack;
    RangedBaseAttack = StatBlock.RangedBaseAttack;
    MagicBaseAttack = StatBlock.MagicBaseAttack;
    
    MeleeBaseDamage = StatBlock.MeleeBaseDamage;
    RangedBaseDamage = StatBlock.RangedBaseDamage;
    MagicBaseDamage = StatBlock.MagicBaseDamage;
    
    BaseDamageAbsorb = StatBlock.BaseDamageAbsorb;
    BaseDamageAvoidance = StatBlock.BaseDamageAvoidance;
    BaseMagicResist = StatBlock.BaseMagicResist;
    BaseMagicPenetration = StatBlock.BaseMagicPenetration;
    
    CurrentDamageAbsorb = StatBlock.CurrentDamageAbsorb;
    CurrentDamageAvoidance = StatBlock.CurrentDamageAvoidance;
    CurrentMagicResist = StatBlock.CurrentMagicResist;
    CurrentMagicPenetration = StatBlock.CurrentMagicPenetration;

    ApplyMovementSpeedFromStats();

    UE_LOG(LogUnitBase, Log, TEXT("SetStatVars: Team=%d, MovementRange=%d"), Team, CurrentMovementRange);
}

//==============================================================================
// ネットワーク同期
//==============================================================================

void AUnitBase::Multicast_SetRotation_Implementation(FRotator NewRotation)
{
    const FRotator OldRotation = GetActorRotation();
    SetActorRotation(NewRotation);
    const FRotator ActualRotation = GetActorRotation();

    UE_LOG(LogUnitBase, Warning, TEXT("[UnitBase] Multicast_SetRotation: %s | Old Yaw=%.1f → Requested Yaw=%.1f → Actual Yaw=%.1f | MoveStatus=%d"),
        *GetName(), OldRotation.Yaw, NewRotation.Yaw, ActualRotation.Yaw, (int32)MoveStatus);
}
