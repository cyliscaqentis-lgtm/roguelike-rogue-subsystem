// =============================================================================
// AllyThinkerBase.cpp
// Copyright Epic Games, Inc. All Rights Reserved.
// =============================================================================

#include "AI/Ally/AllyThinkerBase.h"
#include "AI/Ally/AllyTurnDataSubsystem.h"
#include "Grid/GridPathfindingLibrary.h"
#include "Turn/TurnSystemTypes.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Kismet/GameplayStatics.h"

// ログカテゴリ定義
DEFINE_LOG_CATEGORY(LogAllyThinker);

//------------------------------------------------------------------------------
// 初期化
//------------------------------------------------------------------------------

UAllyThinkerBase::UAllyThinkerBase()
{
    PrimaryComponentTick.bCanEverTick = false;
}

//------------------------------------------------------------------------------
// エントリーポイント
//------------------------------------------------------------------------------

void UAllyThinkerBase::ComputeAllyIntent_Implementation(
    const FTurnContext& Context,
    EAllyCommand Command,
    FAllyIntent& OutIntent)
{
    switch (Command)
    {
    case EAllyCommand::Follow:
        ComputeFollowIntent(Context, OutIntent);
        break;

    case EAllyCommand::StayHere:
        ComputeStayIntent(Context, OutIntent);
        break;

    case EAllyCommand::FreeRoam:
        ComputeFreeRoamIntent(Context, OutIntent);
        break;

    case EAllyCommand::NoAbility:
        ComputeNoAbilityIntent(Context, OutIntent);
        break;

    case EAllyCommand::Auto:
        ComputeAutoIntent(Context, OutIntent);
        break;

    default:
        ComputeNoAbilityIntent(Context, OutIntent);
        break;
    }
}

//------------------------------------------------------------------------------
// 個別フック（Blueprint実装 - 既定動作は空）
//------------------------------------------------------------------------------

void UAllyThinkerBase::ComputeFollowIntent_Implementation(const FTurnContext& Context, FAllyIntent& OutIntent)
{
    // Blueprint側で実装
}

void UAllyThinkerBase::ComputeStayIntent_Implementation(const FTurnContext& Context, FAllyIntent& OutIntent)
{
    // Blueprint側で実装
}

void UAllyThinkerBase::ComputeFreeRoamIntent_Implementation(const FTurnContext& Context, FAllyIntent& OutIntent)
{
    // Blueprint側で実装
}

void UAllyThinkerBase::ComputeNoAbilityIntent_Implementation(const FTurnContext& Context, FAllyIntent& OutIntent)
{
    // Blueprint側で実装
}

void UAllyThinkerBase::ComputeAutoIntent_Implementation(const FTurnContext& Context, FAllyIntent& OutIntent)
{
    // Blueprint側で実装
}

//------------------------------------------------------------------------------
// 部品API（C++実装 - 10年変わらない）
//------------------------------------------------------------------------------

AActor* UAllyThinkerBase::GetPlayerActor() const
{
    return UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
}

int32 UAllyThinkerBase::GetDistanceToPlayerInTiles() const
{
    const AActor* SelfActor = GetOwner();
    const AActor* Player = GetPlayerActor();
    if (!SelfActor || !Player)
    {
        return INT_MAX;
    }

    const float Dist = FVector::Dist2D(SelfActor->GetActorLocation(), Player->GetActorLocation());
    return static_cast<int32>(FMath::RoundToInt(Dist / TileSizeInUnits));
}

TArray<AActor*> UAllyThinkerBase::GetVisibleEnemies() const
{
    // 最小実装（Blueprint実装想定）
    // TODO: GridPathfindingLibrary::ComputeFOV と連携
    return TArray<AActor*>();
}

FGameplayTag UAllyThinkerBase::GetAttackAbilityForRange(int32 DistanceInTiles) const
{
    // 既定は無タグ（Blueprint実装想定）
    return FGameplayTag();
}

UAbilitySystemComponent* UAllyThinkerBase::GetOwnerAbilitySystemComponent() const
{
    if (AActor* Owner = GetOwner())
    {
        return UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Owner);
    }
    return nullptr;
}
