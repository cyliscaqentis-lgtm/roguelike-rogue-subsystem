// ============================================================================
// UnitUIComponent.cpp
// ユニットUI更新Component実装
// UnitBaseから分離（2025-11-09）
// ============================================================================

#include "Character/UnitUIComponent.h"
#include "Character/UnitBase.h"
#include "Blueprint/UserWidget.h"
#include "Components/WidgetComponent.h"
#include "../Utility/ProjectDiagnostics.h"

//------------------------------------------------------------------------------
// Component Lifecycle
//------------------------------------------------------------------------------

UUnitUIComponent::UUnitUIComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	HealthBarOffset = FVector(0, 0, 100);
	DamageNumberDuration = 1.5f;
	bHealthBarVisible = true;
}

void UUnitUIComponent::BeginPlay()
{
	Super::BeginPlay();

	InitializeWidgets();
}

void UUnitUIComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CleanupWidgets();

	Super::EndPlay(EndPlayReason);
}

//------------------------------------------------------------------------------
// Health Display
//------------------------------------------------------------------------------

void UUnitUIComponent::UpdateHealthBar(float CurrentHP, float MaxHP)
{
	if (!bHealthBarVisible)
	{
		return;
	}

	// Blueprint実装を呼び出し
	BP_OnHealthBarUpdated(CurrentHP, MaxHP);

	// TODO: ウィジェット更新の実装
	UE_LOG(LogTemp, Verbose, TEXT("[UnitUIComponent] Health updated: %.1f / %.1f"), CurrentHP, MaxHP);
}

void UUnitUIComponent::SetHealthBarVisible(bool bVisible)
{
	bHealthBarVisible = bVisible;

	if (HealthBarWidget)
	{
		HealthBarWidget->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
	}

	UE_LOG(LogTemp, Log, TEXT("[UnitUIComponent] Health bar visibility set to %s"),
		bVisible ? TEXT("Visible") : TEXT("Hidden"));
}

//------------------------------------------------------------------------------
// Damage Numbers
//------------------------------------------------------------------------------

void UUnitUIComponent::ShowDamageNumber(int32 Damage)
{
	if (Damage <= 0)
	{
		return;
	}

	// Blueprint実装を呼び出し
	BP_OnDamageShown(Damage);

	// TODO: ダメージ数値ウィジェットの生成と表示
	UE_LOG(LogTemp, Log, TEXT("[UnitUIComponent] Showing damage: %d"), Damage);
}

void UUnitUIComponent::ShowHealNumber(int32 HealAmount)
{
	if (HealAmount <= 0)
	{
		return;
	}

	// 回復は緑色で表示
	const FText HealText = FText::AsNumber(HealAmount);
	ShowTextPopup(HealText, FLinearColor::Green);

	UE_LOG(LogTemp, Log, TEXT("[UnitUIComponent] Showing heal: %d"), HealAmount);
}

void UUnitUIComponent::ShowTextPopup(const FText& Text, FLinearColor Color)
{
	// TODO: テキストポップアップの実装
	UE_LOG(LogTemp, Log, TEXT("[UnitUIComponent] Showing popup: %s"), *Text.ToString());
}

//------------------------------------------------------------------------------
// Status Effects
//------------------------------------------------------------------------------

void UUnitUIComponent::UpdateStatusIcons(const TArray<FGameplayTag>& StatusEffects)
{
	// TODO: ステータスアイコンウィジェットの更新
	UE_LOG(LogTemp, Verbose, TEXT("[UnitUIComponent] Status icons updated: %d effects"), StatusEffects.Num());
}

void UUnitUIComponent::ClearStatusIcons()
{
	// TODO: ステータスアイコンのクリア
	UE_LOG(LogTemp, Verbose, TEXT("[UnitUIComponent] Status icons cleared"));
}

//------------------------------------------------------------------------------
// Selection Indicator
//------------------------------------------------------------------------------

void UUnitUIComponent::SetSelectionIndicator(bool bSelected)
{
	// TODO: 選択インジケータの表示/非表示
	UE_LOG(LogTemp, Verbose, TEXT("[UnitUIComponent] Selection indicator: %s"),
		bSelected ? TEXT("Selected") : TEXT("Deselected"));
}

void UUnitUIComponent::SetHoverIndicator(bool bHovered)
{
	// TODO: ホバーインジケータの表示/非表示
	UE_LOG(LogTemp, Verbose, TEXT("[UnitUIComponent] Hover indicator: %s"),
		bHovered ? TEXT("Hovered") : TEXT("Not hovered"));
}

//------------------------------------------------------------------------------
// Widget Management
//------------------------------------------------------------------------------

void UUnitUIComponent::SetHealthBarWidget(UUserWidget* NewWidget)
{
	if (HealthBarWidget && HealthBarWidget != NewWidget)
	{
		HealthBarWidget->RemoveFromParent();
	}

	HealthBarWidget = NewWidget;

	if (HealthBarWidget)
	{
		HealthBarWidget->SetVisibility(bHealthBarVisible ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
	}

	UE_LOG(LogTemp, Log, TEXT("[UnitUIComponent] Health bar widget set"));
}

//------------------------------------------------------------------------------
// Internal Methods
//------------------------------------------------------------------------------

void UUnitUIComponent::InitializeWidgets()
{
	// TODO: ウィジェットの初期化
	// HealthBarWidgetClassが設定されている場合、ウィジェットを生成
	if (HealthBarWidgetClass)
	{
		// ウィジェット生成のロジック
		UE_LOG(LogTemp, Log, TEXT("[UnitUIComponent] Initializing widgets"));
	}
}

void UUnitUIComponent::CleanupWidgets()
{
	if (HealthBarWidget)
	{
		HealthBarWidget->RemoveFromParent();
		HealthBarWidget = nullptr;
	}

	if (StatusIconsWidget)
	{
		StatusIconsWidget->RemoveFromParent();
		StatusIconsWidget = nullptr;
	}

	if (SelectionIndicatorWidget)
	{
		SelectionIndicatorWidget->RemoveFromParent();
		SelectionIndicatorWidget = nullptr;
	}

	UE_LOG(LogTemp, Log, TEXT("[UnitUIComponent] Widgets cleaned up"));
}

AUnitBase* UUnitUIComponent::GetOwnerUnit() const
{
	return Cast<AUnitBase>(GetOwner());
}
