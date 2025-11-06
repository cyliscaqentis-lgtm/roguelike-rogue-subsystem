// LyraTBSAIController.cpp
#include "Character/LyraTBSAIController.h"
#include "GenericTeamAgentInterface.h"
#include "Player/LyraPlayerState.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"

ALyraTBSAIController::ALyraTBSAIController()
{
    // デフォルトはNoTeam（255）
    TeamId = FGenericTeamId::NoTeam;

    // ★★★ bWantsPlayerState を true に設定 ★★★
    bWantsPlayerState = true;

    UE_LOG(LogTemp, Log, TEXT("[LyraTBSAIController] Constructor: bWantsPlayerState=true"));
}

void ALyraTBSAIController::BeginPlay()
{
    Super::BeginPlay();

    // TeamId を早期確定
    APlayerState* PS = GetPlayerState<APlayerState>(); // 明示テンプレート化
    SetGenericTeamId(FGenericTeamId(2));
    UE_LOG(LogTemp, Verbose, TEXT("[LyraTBSAIController] %s BeginPlay: TeamId=%d"),
        *GetName(), GetGenericTeamId().GetId());
}

void ALyraTBSAIController::OnRep_PlayerState()
{
    Super::OnRep_PlayerState();

    APawn* P = GetPawn();
    APlayerState* PS = GetPlayerState<APlayerState>(); // <--- Added template argument
    if (!P || !PS) { return; }

    if (const IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(PS))
    {
        if (UAbilitySystemComponent* ASC = ASI->GetAbilitySystemComponent())
        {
            ASC->InitAbilityActorInfo(PS, P);
            UE_LOG(LogTemp, Log, TEXT("[AIController] Client-side ASC initialized for %s"), *GetNameSafe(P));
        }
    }
    else if (UAbilitySystemComponent* ASC = PS->FindComponentByClass<UAbilitySystemComponent>())
    {
        ASC->InitAbilityActorInfo(PS, P);
        UE_LOG(LogTemp, Log, TEXT("[AIController] Client-side ASC initialized (fallback) for %s"), *GetNameSafe(P));
    }
}

void ALyraTBSAIController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);

    // ★★★ PlayerStateが未作成の場合、手動で作成 ★★★
    if (!PlayerState && GetWorld())
    {
        // NewObjectでPlayerStateを直接作成
        ALyraPlayerState* NewPlayerState = NewObject<ALyraPlayerState>(
            GetWorld(),
            ALyraPlayerState::StaticClass(),
            NAME_None,
            RF_Transient
        );

        if (NewPlayerState)
        {
            // PlayerStateを登録
            NewPlayerState->RegisterPlayerWithSession(false);
            PlayerState = NewPlayerState;

            // 名前を設定
            NewPlayerState->SetPlayerName(FString::Printf(TEXT("Enemy_%s"), *InPawn->GetName()));

            UE_LOG(LogTemp, Warning, TEXT("[OnPossess] ✅ PlayerState created: %s"), *NewPlayerState->GetName());
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("[OnPossess] ❌ Failed to create PlayerState"));
        }
    }

    UE_LOG(LogTemp, Log, TEXT("[OnPossess] Pawn=%s, PlayerState=%s"),
        *InPawn->GetName(),
        PlayerState ? *PlayerState->GetName() : TEXT("NULL"));

    // TeamId の BEFORE/AFTER を出力し、未設定(255)なら敵チーム(2)を強制設定
    uint8 BeforeTeam = GetGenericTeamId().GetId();
    UE_LOG(LogTemp, Verbose, TEXT("[OnPossess] Pawn=%s, TeamId BEFORE SetGenericTeamId=%d"),
        *InPawn->GetName(), BeforeTeam);

    if (BeforeTeam == FGenericTeamId::NoTeam.GetId())
    {
        SetGenericTeamId(FGenericTeamId(2));
    }

    uint8 AfterTeam = GetGenericTeamId().GetId();
    UE_LOG(LogTemp, Verbose, TEXT("[OnPossess] Pawn=%s, TeamId AFTER SetGenericTeamId=%d"),
        *InPawn->GetName(), AfterTeam);
}

FGenericTeamId ALyraTBSAIController::GetGenericTeamId() const
{
    return TeamId;
}

void ALyraTBSAIController::SetGenericTeamId(const FGenericTeamId& NewTeamID)
{
    TeamId = NewTeamID;

    UE_LOG(LogTemp, Log, TEXT("[LyraTBSAIController] %s SetGenericTeamId: %d"),
        *GetName(), TeamId.GetId());
}
