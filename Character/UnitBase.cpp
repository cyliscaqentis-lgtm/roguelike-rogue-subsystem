// UnitBase.cpp
// Location: Rogue/Character/UnitBase.cpp

#include "UnitBase.h"
#include "AbilitySystem/LyraAbilitySystemComponent.h"
#include "Player/LyraPlayerState.h"
#include "GenericTeamAgentInterface.h"
#include "AIController.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Animation/AnimInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Character/UnitManager.h"

// ログカテゴリ定義
DEFINE_LOG_CATEGORY_STATIC(LogUnitBase, Log, All);

//------------------------------------------------------------------------------
// コンストラクタ
//------------------------------------------------------------------------------
AUnitBase::AUnitBase(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    PrimaryActorTick.bCanEverTick = true;
}

//------------------------------------------------------------------------------
// GetAbilitySystemComponent（PlayerStateのASCを返す）
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

    // ★★★ 診断：BeginPlay前の値を確認 ★★★
    UE_LOG(LogUnitBase, Error, 
        TEXT("[BeginPlay] %s: BEFORE - bSkipMoveAnimation=%d, PixelsPerSec=%.1f, TickEnabled=%d"),
        *GetName(), bSkipMoveAnimation, PixelsPerSec, PrimaryActorTick.bCanEverTick);

    SetActorTickEnabled(true);

    // ★★★ 強制：アニメーションスキップを無効化 ★★★
    bSkipMoveAnimation = false;
    UE_LOG(LogUnitBase, Error, 
        TEXT("[BeginPlay] %s: AFTER - FORCED bSkipMoveAnimation=FALSE, PixelsPerSec=%.1f, TickEnabled=%d"),
        *GetName(), PixelsPerSec, IsActorTickEnabled());

    // マテリアル初期化
    EnsureDynamicMaterial();
    ApplyTeamColor();
    UpdateValidSelectionColor();

    // AI Controller の MoveBlockDetection を有効化
    if (AAIController* AI = Cast<AAIController>(GetController()))
    {
        AI->SetMoveBlockDetection(true);
    }

    // Team初期同期
    RefreshTeamFromController();
}

//------------------------------------------------------------------------------
// Tick
//------------------------------------------------------------------------------
void AUnitBase::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    // ★★★ 診断：Tickが呼ばれているか確認（Moving時のみ） ★★★
    static int32 TickCount = 0;
    if (MoveStatus == EUnitMoveStatus::Moving)
    {
        if (TickCount == 0)
        {
            UE_LOG(LogUnitBase, Error, TEXT("[Tick] %s: Moving status detected, starting UpdateMove"), *GetName());
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
// Team同期
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
// MoveUnit（移動開始）
//------------------------------------------------------------------------------
void AUnitBase::MoveUnit(const TArray<FVector>& InPath)
{
    PathArray = InPath;

    // ★★★ 最優先：アニメーションスキップを絶対にfalseにする ★★★
    bSkipMoveAnimation = false;
    SetActorTickEnabled(true);
    
    // ★★★ 緊急診断：MoveUnit呼び出し時の状態を確認 ★★★
    UE_LOG(LogUnitBase, Error,
        TEXT("[MoveUnit] %s CALLED: PathNum=%d, bSkipMoveAnimation=%d (FORCED TO 0!), PixelsPerSec=%.1f, TickEnabled=%d"),
        *GetName(), PathArray.Num(), bSkipMoveAnimation, PixelsPerSec, IsActorTickEnabled());

    // 移動距離上限で切り詰め
    if (PathArray.Num() > CurrentMovementRange)
    {
        PathArray.SetNum(CurrentMovementRange);
    }

    // パスが空の場合は即座に完了
    if (PathArray.Num() == 0)
    {
        UE_LOG(LogUnitBase, Warning, TEXT("[MoveUnit] %s: Empty path!"), *GetName());
        MoveStatus = EUnitMoveStatus::Idle;

        // ★★★ デリゲート Broadcast（重要！） ★★★
        OnMoveFinished.Broadcast(this);
        return;
    }

    MoveCounter = 0;
    StartNextLeg();
}

//------------------------------------------------------------------------------
// StartNextLeg（次の移動区間を開始）
//------------------------------------------------------------------------------
void AUnitBase::StartNextLeg()
{
    // ★★★ パスが終了したら完了通知 ★★★
    if (!PathArray.IsValidIndex(MoveCounter))
    {
        MoveStatus = EUnitMoveStatus::Idle;

        // ★★★ デリゲート Broadcast（重要！） ★★★
        OnMoveFinished.Broadcast(this);

        UE_LOG(LogUnitBase, Log, TEXT("[StartNextLeg] %s: Movement completed!"), *GetName());
        return;
    }

    // 次の目標点へ移動開始
    LegStart = GetActorLocation();
    LegEnd = PathArray[MoveCounter];
    LegEnd.Z = LegStart.Z; // 2D 移動

    // 距離が0の場合は次のウェイポイントへスキップ
    const float Distance = FVector::Dist2D(LegStart, LegEnd);
    if (Distance < 0.1f)
    {
        ++MoveCounter;
        StartNextLeg(); // 再帰で次の区間へ
        return;
    }

    LegAlpha = 0.f;
    MoveStatus = EUnitMoveStatus::Moving;
    
    // ★★★ 最終防御：ここでもう一度falseを強制 ★★★
    bSkipMoveAnimation = false;

    // ★★★ 診断ログ：アニメーション状態を確認 ★★★
    UE_LOG(LogUnitBase, Warning,
        TEXT("[StartNextLeg] %s: bSkipMoveAnimation=%d (FORCED FALSE), Distance=%.1f, Start=%s, End=%s"),
        *GetName(), bSkipMoveAnimation, Distance, *LegStart.ToCompactString(), *LegEnd.ToCompactString());

    // アニメーションスキップなら即座に移動（このコードパスは絶対に実行されないはず）
    if (bSkipMoveAnimation)
    {
        UE_LOG(LogUnitBase, Error, 
            TEXT("[StartNextLeg] %s: ❌ INSTANT TELEPORT (bSkipMoveAnimation=TRUE) - THIS IS THE PROBLEM!"), 
            *GetName());
        SetActorLocation(LegEnd);
        ++MoveCounter;
        StartNextLeg(); // 再帰
    }
    else
    {
        // Verbose: 正常なアニメーション開始は冗長
        UE_LOG(LogUnitBase, Verbose, 
            TEXT("[StartNextLeg] %s: ✅ Smooth animation started (bSkipMoveAnimation=FALSE, Dist=%.1f, Speed=%.1f)"), 
            *GetName(), Distance, PixelsPerSec);
    }
}

//------------------------------------------------------------------------------
// UpdateMove（移動中の更新）
//------------------------------------------------------------------------------
void AUnitBase::UpdateMove(float DeltaSeconds)
{
    const float Dist = FVector::Dist2D(LegStart, LegEnd);
    const float Duration = FMath::Max(0.001f, Dist / PixelsPerSec);
    const float OldAlpha = LegAlpha;
    LegAlpha = FMath::Min(1.f, LegAlpha + DeltaSeconds / Duration);

    // 線形補間で移動
    const FVector OldLoc = GetActorLocation();
    const FVector NewLoc = FMath::Lerp(LegStart, LegEnd, LegAlpha);
    SetActorLocation(NewLoc, false);
    const FVector ActualLoc = GetActorLocation();

    // ★★★ 緊急診断：実際に位置が変わっているか確認 ★★★
    static int32 UpdateCount = 0;
    if (UpdateCount < 5)
    {
        UE_LOG(LogUnitBase, Error,
            TEXT("[UpdateMove] %s: Alpha %.3f→%.3f, OldLoc=%s, NewLoc=%s, ActualLoc=%s, Moved=%.2f"),
            *GetName(), OldAlpha, LegAlpha, 
            *OldLoc.ToCompactString(), *NewLoc.ToCompactString(), *ActualLoc.ToCompactString(),
            FVector::Dist(OldLoc, ActualLoc));
        UpdateCount++;
    }
    
    // ★★★ 診断ログ（最初のフレームのみ） ★★★
    if (LegAlpha < 0.1f)
    {
        UE_LOG(LogUnitBase, Log,
            TEXT("[UpdateMove] %s: Distance=%.1f, Duration=%.2fs, Speed=%.1f units/s"),
            *GetName(), Dist, Duration, PixelsPerSec);
    }

    // 到達判定
    const float DistToTarget = FVector::Dist2D(GetActorLocation(), LegEnd);
    if (DistToTarget <= DistanceSnap || LegAlpha >= 1.f)
    {
        UE_LOG(LogUnitBase, Log, TEXT("[UpdateMove] %s: Reached target"), *GetName());
        ++MoveCounter;
        StartNextLeg();
    }
}

//------------------------------------------------------------------------------
// ビジュアル制御
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

        // 初期値設定
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
        ? FLinearColor(1.f, 0.069f, 0.06f, 1.f)  // 赤（敵）
        : FLinearColor(0.062f, 0.085f, 1.f, 1.f); // 青（味方）

    DynMat->SetVectorParameterValue(Param_ValidSelection, ValidColor);
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
// カーソル反応
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
// タイル管理
//------------------------------------------------------------------------------
void AUnitBase::AdjustTile()
{
    // 古いタイルの Team をリセット
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

    // 現在位置のタイルを LineTrace で検出
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

        // 新しいタイルに Team を設定
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
// ユニット検出
//------------------------------------------------------------------------------
TArray<AUnitBase*> AUnitBase::GetAdjacentPlayers() const
{
    TArray<AUnitBase*> Result;

    // 8方向のオフセット
    static const FVector2D Directions[8] = {
        FVector2D(0, 1), FVector2D(1, 1), FVector2D(1, 0), FVector2D(1, -1),
        FVector2D(0, -1), FVector2D(-1, -1), FVector2D(-1, 0), FVector2D(-1, 1)
    };

    const FVector MyLocation = GetActorLocation();
    const float SearchRadius = 60.f;

    // 全ての UnitBase を取得
    TArray<AActor*> AllUnits;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), AUnitBase::StaticClass(), AllUnits);

    // 8方向をチェック
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
// SetStatVars - UnitManagerから呼ばれるステータス反映処理
//------------------------------------------------------------------------------
void AUnitBase::SetStatVars()
{
    // StatBlockから各メンバ変数へコピー（UnitManagerが使用）
    // 実装はUnitManagerの要求に合わせて調整してください
    Team = StatBlock.Team;
    CurrentMovementRange = StatBlock.CurrentTotalMovementRnage;
    
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
    
    UE_LOG(LogUnitBase, Log, TEXT("SetStatVars: Team=%d, Movement=%d"), Team, CurrentMovementRange);
}
