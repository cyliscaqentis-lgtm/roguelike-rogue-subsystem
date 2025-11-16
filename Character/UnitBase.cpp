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
// CodeRevision: INC-2025-00030-R2 (Migrate to UGridPathfindingSubsystem) (2025-11-17 00:40)
#include "Character/UnitMovementComponent.h"
#include "Character/UnitUIComponent.h"
#include "Grid/GridOccupancySubsystem.h"
#include "Grid/GridPathfindingSubsystem.h"
#include "TimerManager.h"

// Log category
DEFINE_LOG_CATEGORY_STATIC(LogUnitBase, Log, All);

//------------------------------------------------------------------------------
// Constructor
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
// GetAbilitySystemComponent - Return PlayerState's ASC
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

    // Debug: Log initial state before BeginPlay modifications
    SetActorTickEnabled(true);

    // Force bSkipMoveAnimation to false (animation should always be enabled)
    bSkipMoveAnimation = false;

    // Initialize dynamic material
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


    // Enable MoveBlockDetection for AI Controller
    if (AAIController* AI = Cast<AAIController>(GetController()))
    {
        AI->SetMoveBlockDetection(true);
    }

    // Refresh team from controller
    RefreshTeamFromController();
}

//------------------------------------------------------------------------------
// Tick - 移動処理はMovementCompに委譲（2025-11-16リファクタリング）
//------------------------------------------------------------------------------
void AUnitBase::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    // ★★★ 移動処理はMovementCompが独自のTickで処理するため、ここでは何もしない ★★★
}

//------------------------------------------------------------------------------
// Team synchronization
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
// MoveUnit - Delegate to MovementComponent (2025-11-16 refactoring)
// CodeRevision: INC-2025-00015-R1 (Simplified to delegate to UUnitMovementComponent) (2025-11-16 13:55)
//------------------------------------------------------------------------------
void AUnitBase::MoveUnit(const TArray<FVector>& InPath)
{
    if (!MovementComp)
    {
        UE_LOG(LogUnitBase, Error, TEXT("[MoveUnit] %s: MovementComp is NULL!"), *GetName());
        return;
    }

    UE_LOG(LogUnitBase, Log,
        TEXT("[MoveUnit] %s: Delegating to MovementComponent (PathNum=%d)"),
        *GetName(), InPath.Num());

    // ★★★ Movement processing fully delegated to MovementComponent ★★★
    MovementComp->MoveUnit(InPath);
}


//------------------------------------------------------------------------------
// ★★★ Removed (2025-11-16 refactoring): StartNextLeg() and UpdateMove() ★★★
// Movement processing completely migrated to UUnitMovementComponent
// CodeRevision: INC-2025-00015-R1 (Removed 171 lines of duplicate logic) (2025-11-16 13:55)
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// Ensure dynamic material is initialized
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

        // Initialize team color
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
        ? FLinearColor(1.f, 0.069f, 0.06f, 1.f)  // Red (enemy)
        : FLinearColor(0.062f, 0.085f, 1.f, 1.f); // Blue (player)

    DynMat->SetVectorParameterValue(Param_ValidSelection, ValidColor);
}

//------------------------------------------------------------------------------
// Cursor interaction
//------------------------------------------------------------------------------
void AUnitBase::NotifyActorBeginCursorOver()
{
    UpdateValidSelectionColor();
    Super::NotifyActorBeginCursorOver();
}

void AUnitBase::NotifyActorEndCursorOver()
{
    Super::NotifyActorEndCursorOver();
}

//------------------------------------------------------------------------------
// Adjust tile
//------------------------------------------------------------------------------
void AUnitBase::AdjustTile()
{
    // Reset previous tile's Team property
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

    // Find tile below unit using LineTrace
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

        // Set Team property on new tile
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
// Get adjacent players
//------------------------------------------------------------------------------
TArray<AUnitBase*> AUnitBase::GetAdjacentPlayers() const
{
    TArray<AUnitBase*> Result;

    // 8-directional search
    static const FVector2D Directions[8] = {
        FVector2D(0, 1), FVector2D(1, 1), FVector2D(1, 0), FVector2D(1, -1),
        FVector2D(0, -1), FVector2D(-1, -1), FVector2D(-1, 0), FVector2D(-1, 1)
    };

    const FVector MyLocation = GetActorLocation();
    const float SearchRadius = 60.f;

    // Get all UnitBase actors
    TArray<AActor*> AllUnits;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), AUnitBase::StaticClass(), AllUnits);

    // Search in 8 directions
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
// SetStatVars - Copy stats from StatBlock (called by UnitManager)
//------------------------------------------------------------------------------
void AUnitBase::SetStatVars()
{
    // Copy stats from StatBlock to member variables
    // NOTE: This function is called by UnitManager after StatBlock is updated
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

    if (MovementComp)
    {
        MovementComp->UpdateSpeedFromStats(StatBlock);
    }

    UE_LOG(LogUnitBase, Log, TEXT("SetStatVars: Team=%d, MovementRange=%d"), Team, CurrentMovementRange);
}
