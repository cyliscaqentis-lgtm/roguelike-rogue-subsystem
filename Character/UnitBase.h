// UnitBase.h
// Location: Rogue/Character/UnitBase.h

#pragma once

#include "CoreMinimal.h"
#include "Character/LyraCharacter.h"
#include "Character/UnitStatBlock.h"
#include "GenericTeamAgentInterface.h"
#include "UnitBase.generated.h"

// ===== 前方宣言 =====
class UAbilitySystemComponent;
class ULyraAbilitySystemComponent;
class ALyraPlayerState;
class UUnitMovementComponent;
class UUnitUIComponent;

UENUM(BlueprintType)
enum class EUnitMoveStatus : uint8
{
    Idle,
    Moving
};

// ★★★ C++専用デリゲート（UCLASS の前に宣言） ★★★
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMoveFinished, AUnitBase*);

/**
 * TBS専用のユニット基底クラス
 * - PlayerStateのASCを常に返すことで、初期化順序とキャッシュの問題を回避
 * - プレイヤーとAIの両方で使用可能
 */
UCLASS()
class LYRAGAME_API AUnitBase : public ALyraCharacter
{
    GENERATED_BODY()

public:
    AUnitBase(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━E
    // ★ IGenericTeamAgentInterface (Lyra Damage Calculation)
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━E
    virtual FGenericTeamId GetGenericTeamId() const override
    {
        return FGenericTeamId(Team);
    }

    virtual void SetGenericTeamId(const FGenericTeamId& NewTeamID) override
    {
        Team = NewTeamID.GetId();
    }

    //==========================================================================
    // ★★★ 新規Component参照（2025-11-09リファクタリング） ★★★
    //==========================================================================

    /** 移動処理Component */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Unit|Components")
    TObjectPtr<UUnitMovementComponent> MovementComp = nullptr;

    /** UI更新Component */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Unit|Components")
    TObjectPtr<UUnitUIComponent> UIComp = nullptr;

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // ★ TBS専用: PlayerStateのASCを常に返す（重要！）
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    /**
     * IAbilitySystemInterface オーバーライド
     * 理由: ALyraHeroComponentがないため、PlayerStateのASCを手動で返す必要がある
     * 効果: Blueprint側で Pawn.GetAbilitySystemComponent() が正しく動作する
     */
    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

    // ===== Blueprint 呼び出し可能 API =====
    UFUNCTION(BlueprintCallable, Category = "Unit|Movement")
    void MoveUnit(const TArray<FVector>& InPath);

    UFUNCTION(BlueprintCallable, Category = "Unit|Visual")
    void SetSelected(bool bSelected);

    UFUNCTION(BlueprintCallable, Category = "Unit|Visual")
    void SetHighlighted(bool bHighlighted);

    UFUNCTION(BlueprintCallable, Category = "Unit|Tile")
    void AdjustTile();

    UFUNCTION(BlueprintCallable, Category = "Unit|Detection")
    TArray<AUnitBase*> GetAdjacentPlayers() const;

    // ===== UnitManager統合用API =====
    /** StatBlockから各ステータス変数へコピー */
    UFUNCTION(BlueprintCallable, Category = "Unit|Stats")
    void SetStatVars();

    /** UnitManagerが書き込むステータスブロック */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit|Stats")
    FUnitStatBlock StatBlock;

    // ★★★ 移動完了デリゲート（C++専用・UPROPERTYなし） ★★★
    FOnMoveFinished OnMoveFinished;

    // ===== チーム設定（読み取り専用：Controller/PSから同期） =====
    UPROPERTY(BlueprintReadOnly, Category = "Unit|Team")
    int32 Team = 0;

    // ===== グリッド設定 =====
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit|Grid")
    float GridSize = 100.f;

    // ===== 移動設定 =====
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit|Movement")
    float PixelsPerSec = 300.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit|Movement")
    float MinPixelsPerSec = 150.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit|Movement")
    float MaxPixelsPerSec = 1200.f;

    // ★★★ 重要：Blueprintでtrueに設定されていても、C++で強制的にfalseにします ★★★
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit|Movement", meta = (AllowPrivateAccess = "true"))
    bool bSkipMoveAnimation = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit|Movement")
    int32 CurrentMovementRange = 999;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit|Movement")
    float DistanceSnap = 7.f;

    UPROPERTY(BlueprintReadOnly, Category = "Unit|Movement")
    FIntPoint GridPosition = FIntPoint(0, 0);

    UPROPERTY(BlueprintReadOnly, Category = "Unit|Movement")
    FVector2D MoveDir2D = FVector2D::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "Unit|Movement")
    FVector CurrentVelocity;

    // ===== 戦闘パラメータ（基本値） =====
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit|Combat")
    float AttackRange = 150.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit|Combat|Base")
    int32 MeleeBaseAttack = 10;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit|Combat|Base")
    int32 RangedBaseAttack = 8;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit|Combat|Base")
    int32 MagicBaseAttack = 12;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit|Combat|Base")
    float MeleeBaseDamage = 10.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit|Combat|Base")
    float RangedBaseDamage = 8.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit|Combat|Base")
    float MagicBaseDamage = 12.f;

    // ===== 防御パラメータ（基本値） =====
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit|Combat|Defense|Base")
    float BaseDamageAbsorb = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit|Combat|Defense|Base")
    float BaseDamageAvoidance = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit|Combat|Defense|Base")
    float BaseMagicResist = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit|Combat|Defense|Base")
    float BaseMagicPenetration = 0.f;

    // ===== 防御パラメータ（現在値） =====
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit|Combat|Defense|Current")
    float CurrentDamageAbsorb = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit|Combat|Defense|Current")
    float CurrentDamageAvoidance = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit|Combat|Defense|Current")
    float CurrentMagicResist = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit|Combat|Defense|Current")
    float CurrentMagicPenetration = 0.f;

    // ===== 視界システム =====
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit|Vision")
    float MaxSightRange = 500.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit|Vision")
    float CurrentSightRange = 500.f;

    UPROPERTY(BlueprintReadOnly, Category = "Unit|Vision")
    int32 NumberOfThoseWhoCanSeeMe = 0;

    // ===== 回転制御 =====
    UPROPERTY(BlueprintReadWrite, Category = "Unit|Rotation")
    float FacingYaw = 0.f;

    UPROPERTY(BlueprintReadWrite, Category = "Unit|Rotation")
    float DesiredYaw = 0.f;

    UPROPERTY(BlueprintReadWrite, Category = "Unit|Rotation")
    float DeltaYaw = 0.f;

    // ===== ビジュアル設定 =====
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit|Visual")
    FName Param_Selected = TEXT("Selected");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit|Visual")
    FName Param_Highlight = TEXT("Highlighted");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit|Visual")
    FName Param_BodyColor = TEXT("BodyColor");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit|Visual")
    FName Param_ValidSelection = TEXT("ValidSelection");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit|Visual")
    FLinearColor Team0Color = FLinearColor(0.23f, 0.635f, 0.245f, 1.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit|Visual")
    FLinearColor TeamNColor = FLinearColor(0.61f, 0.245f, 0.198f, 1.f);

protected:
    virtual void PostInitializeComponents() override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void NotifyActorBeginCursorOver() override;
    virtual void NotifyActorEndCursorOver() override;

    // ★★★ AbilitySystem初期化コールバック（2025-11-09追加） ★★★
    virtual void OnAbilitySystemInitialized();
    virtual void OnAbilitySystemUninitialized();

    // ✅ Team同期（Controller/PSからの反映）
    virtual void OnRep_Controller() override;
    void RefreshTeamFromController();

    // 移動制御
    void StartNextLeg();
    void UpdateMove(float DeltaSeconds);

    // ビジュアル制御
    void EnsureDynamicMaterial();
    void ApplyTeamColor();
    void UpdateValidSelectionColor();
    void ApplyMovementSpeedFromStats();

    // ★★★ AbilitySet付与（2025-11-09追加） ★★★
    void GrantAbilitySetsIfNeeded();
    bool bGrantedAbilitySets = false;

protected:
    // 移動状態
    UPROPERTY()
    TArray<FVector> PathArray;

    int32 MoveCounter = 0;
    EUnitMoveStatus MoveStatus = EUnitMoveStatus::Idle;
    FVector LegStart{}, LegEnd{};
    float LegAlpha = 0.f;

    // ビジュアル状態
    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> DynMat = nullptr;

    // Tile 管理
    UPROPERTY(BlueprintReadOnly, Category = "Unit|Tile")
    TObjectPtr<AActor> TileIAmOn = nullptr;
};
