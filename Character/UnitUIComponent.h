// ============================================================================
// UnitUIComponent.h
// ユニットUI更新Component
// UnitBaseから分離（2025-11-09）
// ============================================================================

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "UnitUIComponent.generated.h"

class UUserWidget;
class AUnitBase;

/**
 * ユニットのUI更新を担当するComponent
 * 責務: ヘルスバー、ダメージ数値、ステータスアイコン
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class LYRAGAME_API UUnitUIComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// ========== Component Lifecycle ==========
	UUnitUIComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ========== Health Display ==========

	/**
	 * ヘルスバーを更新
	 * @param CurrentHP 現在のHP
	 * @param MaxHP 最大HP
	 */
	UFUNCTION(BlueprintCallable, Category = "Unit|UI")
	void UpdateHealthBar(float CurrentHP, float MaxHP);

	/**
	 * ヘルスバーを表示/非表示
	 * @param bVisible 表示するか
	 */
	UFUNCTION(BlueprintCallable, Category = "Unit|UI")
	void SetHealthBarVisible(bool bVisible);

	// ========== Damage Numbers ==========

	/**
	 * ダメージ数値を表示
	 * @param Damage ダメージ量
	 */
	UFUNCTION(BlueprintCallable, Category = "Unit|UI")
	void ShowDamageNumber(int32 Damage);

	/**
	 * 回復数値を表示
	 * @param HealAmount 回復量
	 */
	UFUNCTION(BlueprintCallable, Category = "Unit|UI")
	void ShowHealNumber(int32 HealAmount);

	/**
	 * テキストポップアップを表示
	 * @param Text 表示するテキスト
	 * @param Color 色
	 */
	UFUNCTION(BlueprintCallable, Category = "Unit|UI")
	void ShowTextPopup(const FText& Text, FLinearColor Color);

	// ========== Status Effects ==========

	/**
	 * ステータスアイコンを更新
	 * @param StatusEffects ステータスエフェクトタグ配列
	 */
	UFUNCTION(BlueprintCallable, Category = "Unit|UI")
	void UpdateStatusIcons(const TArray<FGameplayTag>& StatusEffects);

	/**
	 * ステータスアイコンをクリア
	 */
	UFUNCTION(BlueprintCallable, Category = "Unit|UI")
	void ClearStatusIcons();

	// ========== Selection Indicator ==========

	/**
	 * 選択インジケータを表示/非表示
	 * @param bSelected 選択されているか
	 */
	UFUNCTION(BlueprintCallable, Category = "Unit|UI")
	void SetSelectionIndicator(bool bSelected);

	/**
	 * ホバーインジケータを表示/非表示
	 * @param bHovered ホバーされているか
	 */
	UFUNCTION(BlueprintCallable, Category = "Unit|UI")
	void SetHoverIndicator(bool bHovered);

	// ========== Widget Management ==========

	/**
	 * ヘルスバーウィジェットを取得
	 */
	UFUNCTION(BlueprintPure, Category = "Unit|UI")
	UUserWidget* GetHealthBarWidget() const { return HealthBarWidget; }

	/**
	 * ヘルスバーウィジェットを設定
	 * @param NewWidget 新しいウィジェット
	 */
	UFUNCTION(BlueprintCallable, Category = "Unit|UI")
	void SetHealthBarWidget(UUserWidget* NewWidget);

protected:
	// ========== Widget References ==========

	/** ヘルスバーウィジェット */
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> HealthBarWidget = nullptr;

	/** ステータスアイコンウィジェット */
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> StatusIconsWidget = nullptr;

	/** 選択インジケータウィジェット */
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> SelectionIndicatorWidget = nullptr;

	// ========== Configuration ==========

	/** ヘルスバーウィジェットクラス */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit|UI")
	TSubclassOf<UUserWidget> HealthBarWidgetClass;

	/** ダメージ数値ウィジェットクラス */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit|UI")
	TSubclassOf<UUserWidget> DamageNumberWidgetClass;

	/** ステータスアイコンウィジェットクラス */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit|UI")
	TSubclassOf<UUserWidget> StatusIconsWidgetClass;

	/** ヘルスバーのワールドオフセット */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit|UI")
	FVector HealthBarOffset = FVector(0, 0, 100);

	/** ダメージ数値の表示時間 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit|UI")
	float DamageNumberDuration = 1.5f;

	/** 現在のヘルスバーの可視性 */
	UPROPERTY(Transient)
	bool bHealthBarVisible = true;

	// ========== Internal Methods ==========

	/**
	 * ウィジェットを初期化
	 */
	void InitializeWidgets();

	/**
	 * ウィジェットをクリーンアップ
	 */
	void CleanupWidgets();

	/**
	 * オーナーUnitを取得
	 */
	AUnitBase* GetOwnerUnit() const;

	/**
	 * Blueprint実装可能なヘルスバー更新
	 * @param CurrentHP 現在のHP
	 * @param MaxHP 最大HP
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Unit|UI", meta = (DisplayName = "On Health Bar Updated"))
	void BP_OnHealthBarUpdated(float CurrentHP, float MaxHP);

	/**
	 * Blueprint実装可能なダメージ表示
	 * @param Damage ダメージ量
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Unit|UI", meta = (DisplayName = "On Damage Shown"))
	void BP_OnDamageShown(int32 Damage);
};
