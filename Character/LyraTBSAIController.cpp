// LyraTBSAIController.cpp
#include "Character/LyraTBSAIController.h"
#include "GenericTeamAgentInterface.h"
#include "Player/LyraPlayerState.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"

DEFINE_LOG_CATEGORY(LogAIController);

ALyraTBSAIController::ALyraTBSAIController()
{
    // ★★★ AIもPlayerStateを持つ設計（GAS統合のため） ★★★
    bWantsPlayerState = true;

    // デフォルトはNoTeam（後でOnPossessで敵チームに設定）
    TeamId = FGenericTeamId::NoTeam;

    UE_LOG(LogAIController, Log, TEXT("[LyraTBSAIController] Constructor: bWantsPlayerState=true, TeamId=NoTeam"));
}

void ALyraTBSAIController::BeginPlay()
{
    Super::BeginPlay();

    UE_LOG(LogAIController, Verbose, TEXT("[LyraTBSAIController] %s BeginPlay: PlayerState=%s, TeamId=%d"),
        *GetName(),
        PlayerState ? *PlayerState->GetName() : TEXT("NULL"),
        GetGenericTeamId().GetId());
}

void ALyraTBSAIController::OnRep_PlayerState()
{
    Super::OnRep_PlayerState();

    // ★★★ クライアント側: PlayerStateレプリケーション完了時にASC初期化 ★★★
    InitializeAbilitySystemComponent();
}

void ALyraTBSAIController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);

    if (!InPawn)
    {
        UE_LOG(LogAIController, Warning, TEXT("[LyraTBSAIController] OnPossess called with null pawn"));
        return;
    }

    UE_LOG(LogAIController, Log, TEXT("[LyraTBSAIController] OnPossess: Pawn=%s, PlayerState=%s"),
        *InPawn->GetName(),
        PlayerState ? *PlayerState->GetName() : TEXT("NULL"));

    //==========================================================================
    // (1) TeamId設定: 未設定なら敵チーム(2)に設定
    //==========================================================================
    const uint8 BeforeTeam = GetGenericTeamId().GetId();

    if (BeforeTeam == FGenericTeamId::NoTeam.GetId())
    {
        SetGenericTeamId(FGenericTeamId(2)); // 敵チーム
        UE_LOG(LogAIController, Log, TEXT("[LyraTBSAIController] OnPossess: TeamId set from NoTeam(%d) to Enemy(2)"),
            BeforeTeam);
    }
    else
    {
        UE_LOG(LogAIController, Verbose, TEXT("[LyraTBSAIController] OnPossess: TeamId already set to %d"),
            BeforeTeam);
    }

    //==========================================================================
    // (2) サーバー側: ASC初期化
    //==========================================================================
    if (HasAuthority())
    {
        InitializeAbilitySystemComponent();
    }
}

FGenericTeamId ALyraTBSAIController::GetGenericTeamId() const
{
    return TeamId;
}

void ALyraTBSAIController::SetGenericTeamId(const FGenericTeamId& NewTeamID)
{
    if (TeamId == NewTeamID)
    {
        return; // 変更なし
    }

    const uint8 OldId = TeamId.GetId();
    TeamId = NewTeamID;

    UE_LOG(LogAIController, Log, TEXT("[LyraTBSAIController] %s SetGenericTeamId: %d -> %d"),
        *GetName(), OldId, TeamId.GetId());
}

//------------------------------------------------------------------------------
// Internal Helpers
//------------------------------------------------------------------------------

void ALyraTBSAIController::InitializeAbilitySystemComponent()
{
    APawn* P = GetPawn();
    APlayerState* PS = GetPlayerState<APlayerState>();

    if (!P || !PS)
    {
        UE_LOG(LogAIController, Warning, TEXT("[LyraTBSAIController] InitializeASC: Pawn=%s, PlayerState=%s - skipping"),
            P ? TEXT("Valid") : TEXT("NULL"),
            PS ? TEXT("Valid") : TEXT("NULL"));
        return;
    }

    // PlayerStateからASCを取得
    UAbilitySystemComponent* ASC = nullptr;

    // Method 1: IAbilitySystemInterface経由
    if (const IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(PS))
    {
        ASC = ASI->GetAbilitySystemComponent();
    }

    // Method 2: FindComponentByClass (Fallback)
    if (!ASC)
    {
        ASC = PS->FindComponentByClass<UAbilitySystemComponent>();
    }

    if (!ASC)
    {
        UE_LOG(LogAIController, Warning, TEXT("[LyraTBSAIController] InitializeASC: No ASC found on PlayerState %s"),
            *PS->GetName());
        return;
    }

    // ASC初期化
    ASC->InitAbilityActorInfo(PS, P);

    const TCHAR* SideStr = HasAuthority() ? TEXT("Server") : TEXT("Client");
    UE_LOG(LogAIController, Log, TEXT("[LyraTBSAIController] InitializeASC: %s-side ASC initialized for Pawn=%s, PlayerState=%s"),
        SideStr, *P->GetName(), *PS->GetName());
}
