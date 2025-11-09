// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/PlayerControllerBase.h"
#include "Turn/GameTurnManagerBase.h"
// #include "Turn/TurnManagerSubsystem.h"  // ★★★ 統合完了により削除 ★★★
#include "Grid/GridPathfindingLibrary.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Camera/PlayerCameraManager.h"
#include "Utility/RogueGameplayTags.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"  // ★★★ TActorIterator用 ★★★
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"

//------------------------------------------------------------------------------
// Constructor
//------------------------------------------------------------------------------

APlayerControllerBase::APlayerControllerBase()
{
    // Tick を常に有効化
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = true;
    SetTickableWhenPaused(false);
    bReplicates = false;

    // GameplayTagsの初期化
    // ★ タグ整合: InputTag.Move → Ability.Move（サーバー側と統一）
    MoveInputTag = FGameplayTag::RequestGameplayTag(TEXT("Ability.Move"));
    TurnInputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Turn"));

    // デフォルト値の初期化
    AxisThreshold = 0.5f;
    DeadzoneThreshold = 0.1f;
    LastTurnDirection = FVector2D::ZeroVector;
    CachedInputDirection = FVector2D::ZeroVector;
    ServerLastTurnDirectionQuantized = FIntPoint(0, 0);

    //==========================================================================
    // ★★★ 削除: サーバ冪等化プロパティ（TurnManagerに移行）
    //==========================================================================
    // LastMoveCmdServerTime = -FLT_MAX;
    // LastAcceptedTurnId = INDEX_NONE;
    // LastAcceptedWindowId = INDEX_NONE;
}


//------------------------------------------------------------------------------
// Lifecycle
//------------------------------------------------------------------------------

void APlayerControllerBase::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // ✅ Tickでは検索しない（BeginPlayとタイマーで処理）
    if (!CachedTurnManager || !IsValid(CachedTurnManager))
    {
        return;
    }

    //==========================================================================
    // 初回同期
    //==========================================================================
    if (!bPrevInitSynced)
    {
        bPrevWaitingForPlayerInput = CachedTurnManager->WaitingForPlayerInput;
        CurrentInputWindowId = CachedTurnManager->GetCurrentInputWindowId();
        bPrevInitSynced = true;
        UE_LOG(LogTemp, Warning, TEXT("[Client] Initial sync: WaitingForPlayerInput=%d, WindowId=%d"),
            bPrevWaitingForPlayerInput, CurrentInputWindowId);
    }

    //==========================================================================
    // ★★★ 削除: 入力ウィンドウの立ち上がり検出（サーバー側が管理）
    //==========================================================================
    // const bool bCurrentWaiting = CachedTurnManager->WaitingForPlayerInput;
    // if (!bPrevWaitingForPlayerInput && bCurrentWaiting)
    // {
    //     OnInputWindowOpened();
    // }

    //==========================================================================
    // ★★★ CRITICAL FIX: 堅牢なラッチリセット（立ち上がりエッジ + WindowId変化検出）
    //==========================================================================
    
    // 現在値を先に取り出す
    const bool bNow = CachedTurnManager->WaitingForPlayerInput;
    const int32 NewWindowId = CachedTurnManager->GetCurrentInputWindowId();  // ★ 新しい値

    // ★ 状態遷移を詳細にログ可視化
    UE_LOG(LogTemp, Verbose, TEXT("[Client_Tick] WPI: Prev=%d, Now=%d, Sent=%d, WinId=%d->%d"),
        bPrevWaitingForPlayerInput, bNow, bSentThisInputWindow, CurrentInputWindowId, NewWindowId);

    // ★ 立ち上がり（false→true）: ラッチをリセット
    if (!bPrevWaitingForPlayerInput && bNow)
    {
        bSentThisInputWindow = false;
        LastProcessedWindowId = INDEX_NONE;
        
        UE_LOG(LogTemp, Warning, TEXT("[Client_Tick] ★ INPUT WINDOW DETECTED: reset latch (WinId=%d)"),
            NewWindowId);
    }
    
    // ★ WindowId変化検出（安全策）
    if (CurrentInputWindowId != NewWindowId)  // ★ 前の値 vs 新しい値
    {
        bSentThisInputWindow = false;
        LastProcessedWindowId = INDEX_NONE;
        CurrentInputWindowId = NewWindowId;  // ★ ここで更新
        
        // ★ 削除：ウィンドウ変更は稀。エラー時だけでOK
        // UE_LOG(LogTemp, Warning, TEXT("[Client_Tick] ★ WINDOW ID CHANGED: reset latch (%d->%d)"),
        //     CurrentInputWindowId, NewWindowId);
    }
    
    // ★ 立ち下がり（true→false）: クリーンアップ
    if (bPrevWaitingForPlayerInput && !bNow)
    {
        bSentThisInputWindow = false;
        UE_LOG(LogTemp, Verbose, TEXT("[Client] Window CLOSE -> cleanup latch"));
    }

    // ★ WindowIdを毎フレーム同期（最後に実行して常に最新に）
    CurrentInputWindowId = NewWindowId;

    // 最後に前回値を更新（順序が重要！）
    bPrevWaitingForPlayerInput = bNow;
}




//------------------------------------------------------------------------------
// ★★★ 入力ウィンドウの原子的更新処理（最小パッチ A）
//------------------------------------------------------------------------------




//------------------------------------------------------------------------------
// BeginPlay & OnPossess
//------------------------------------------------------------------------------

void APlayerControllerBase::BeginPlay()
{
    Super::BeginPlay();

    UE_LOG(LogTemp, Log, TEXT("[PlayerController] BeginPlay started"));

    // ★★★ 最優先: Tick を強制有効化
    SetActorTickEnabled(true);
    UE_LOG(LogTemp, Warning, TEXT("[PlayerController] BeginPlay: Tick enabled"));

    // ★★★ Tick最適化: BeginPlayで確実に取得を試みる
    EnsureTurnManagerCached();

    // ★★★ 取得できない場合はタイマーで再試行（Tickを汚さない）
    if (!CachedTurnManager)
    {
        FTimerHandle RetryHandle;
        GetWorld()->GetTimerManager().SetTimer(
            RetryHandle,
            this,
            &APlayerControllerBase::EnsureTurnManagerCached,
            0.1f, // 0.1秒ごと
            true  // ループ
        );
    }

    // GameplayTagsの検証
    if (!MoveInputTag.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("[PlayerController] MoveInputTag (Ability.Move) is INVALID!"));
    }

    if (!TurnInputTag.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("[PlayerController] TurnInputTag (InputTag.Turn) is INVALID!"));
    }

    UE_LOG(LogTemp, Log, TEXT("[PlayerController] BeginPlay completed"));
}

//------------------------------------------------------------------------------
// ★★★ Tick最適化: TurnManager取得ヘルパー（2025-11-09）
//------------------------------------------------------------------------------
void APlayerControllerBase::EnsureTurnManagerCached()
{
    if (CachedTurnManager && IsValid(CachedTurnManager))
    {
        // ✅ 既に取得済みならタイマーをクリア
        if (UWorld* World = GetWorld())
        {
            World->GetTimerManager().ClearAllTimersForObject(this);
        }
        return;
    }

    // ★★★ 直接GameTurnManagerBaseを検索（統合・最適化）
    if (UWorld* World = GetWorld())
    {
        for (TActorIterator<AGameTurnManagerBase> It(World); It; ++It)
        {
            CachedTurnManager = *It;
            if (CachedTurnManager)
            {
                PathFinder = CachedTurnManager->GetCachedPathFinder();
                UE_LOG(LogTemp, Log, TEXT("[Client] TurnManager cached successfully: %s"),
                    *CachedTurnManager->GetName());

                // タイマーをクリア
                World->GetTimerManager().ClearAllTimersForObject(this);
                break;
            }
        }
    }
}

void APlayerControllerBase::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);

    UE_LOG(LogTemp, Log, TEXT("[PlayerController] OnPossess called for: %s"),
        InPawn ? *InPawn->GetName() : TEXT("NULL"));

    //==========================================================================
    // ★ カメラをプレイヤーに切替（確実に）
    //==========================================================================
    SetViewTargetWithBlend(InPawn, 0.0f);
    bAutoManageActiveCameraTarget = true;

    //==========================================================================
    // ★ TurnManagerにPossess通知（入力窓を開き直すトリガー）
    //==========================================================================
    if (CachedTurnManager)
    {
        CachedTurnManager->NotifyPlayerPossessed(InPawn);
    }

    //==========================================================================
    // ★★★ 削除: InputGuardのヒステリシス設定（3タグシステムで不要）
    //==========================================================================
    // if (UTurnInputGuard* Guard = GetWorld()->GetSubsystem<UTurnInputGuard>())
    // {
    //     Guard->SetThresholds(0.5f, 0.2f);
    //     UE_LOG(LogTemp, Log, TEXT("[PlayerController] InputGuard thresholds set"));
    // }

    // EnhancedInput初期化（Possess後に確実に実行）
    InitializeEnhancedInput();
}



void APlayerControllerBase::InitializeEnhancedInput()
{
    UE_LOG(LogTemp, Log, TEXT("[PlayerController] InitializeEnhancedInput started"));

    // EnhancedInput MappingContextの追加
    if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
        ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
    {
        if (IMC_TBS)
        {
            FModifyContextOptions Options;
            Options.bForceImmediately = false;
            Options.bIgnoreAllPressedKeysUntilRelease = false;
            Options.bNotifyUserSettings = false;

            Subsystem->AddMappingContext(IMC_TBS, 10, Options);
            UE_LOG(LogTemp, Log, TEXT("[PlayerController] IMC_TBS added successfully"));
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("[PlayerController] IMC_TBS is NULL"));
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[PlayerController] EnhancedInputLocalPlayerSubsystem not found"));
    }

    UE_LOG(LogTemp, Log, TEXT("[PlayerController] InitializeEnhancedInput completed"));
}

void APlayerControllerBase::SetupInputComponent()
{
    Super::SetupInputComponent();

    UE_LOG(LogTemp, Log, TEXT("[PlayerController] SetupInputComponent started"));

    if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent))
    {
        // IA_Moveのバインド（3つのイベント）
        if (IA_Move)
        {
            EnhancedInput->BindAction(IA_Move, ETriggerEvent::Triggered, this,
                &APlayerControllerBase::Input_Move_Triggered);
            EnhancedInput->BindAction(IA_Move, ETriggerEvent::Canceled, this,
                &APlayerControllerBase::Input_Move_Canceled);
            EnhancedInput->BindAction(IA_Move, ETriggerEvent::Completed, this,
                &APlayerControllerBase::Input_Move_Completed);

            UE_LOG(LogTemp, Log, TEXT("[PlayerController] IA_Move bound (Triggered/Canceled/Completed)"));
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("[PlayerController] IA_Move is NULL"));
        }

        // IA_TurnFacingのバインド（2つのイベント）
        if (IA_TurnFacing)
        {
            EnhancedInput->BindAction(IA_TurnFacing, ETriggerEvent::Started, this,
                &APlayerControllerBase::Input_TurnFacing_Started);
            EnhancedInput->BindAction(IA_TurnFacing, ETriggerEvent::Triggered, this,
                &APlayerControllerBase::Input_TurnFacing_Triggered);

            UE_LOG(LogTemp, Log, TEXT("[PlayerController] IA_TurnFacing bound (Started/Triggered)"));
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("[PlayerController] IA_TurnFacing is NULL"));
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[PlayerController] InputComponent is not EnhancedInputComponent"));
    }

    UE_LOG(LogTemp, Log, TEXT("[PlayerController] SetupInputComponent completed"));
}

//------------------------------------------------------------------------------
// Input Handlers - IA_Move（最小パッチ B 統合）
//------------------------------------------------------------------------------

void APlayerControllerBase::Input_Move_Triggered(const FInputActionValue& Value)
{
    //==========================================================================
    // Step 1: TurnManager検証
    //==========================================================================
    if (!IsValid(CachedTurnManager))
    {
        UE_LOG(LogTemp, Warning, TEXT("[Client] InputMoveTriggered: TurnManager invalid"));
        return;
    }

    //==========================================================================
    // Step 2: 入力ウィンドウが開いているか確認
    //==========================================================================
    if (!CachedTurnManager->WaitingForPlayerInput)
    {
        UE_LOG(LogTemp, Verbose, TEXT("[Client] Input blocked: Not in input window"));
        return;
    }

    //==========================================================================
    // ★ Step 3: 送信済みチェック（ラッチ確認）
    //==========================================================================
    if (bSentThisInputWindow)
    {
        UE_LOG(LogTemp, Verbose, TEXT("[Client] Input blocked: Already sent in this window (bSentThisInputWindow=true)"));
        return;  // ★ CRITICAL: ここで早期returnすることを可視化
    }

    //=== Step 2: 入力値を直接使用（修正なし） ===
    const FVector2D RawInput = Value.Get<FVector2D>();
    
    // ★ 削除：Raw input使用はフォールバック処理なのでエラー時のみ
    // UE_LOG(LogTemp, Warning, TEXT("[INPUTFIX] Removed - using raw input directly: (%.2f, %.2f)"), RawInput.X, RawInput.Y);

    //=== Step 3: カメラ相対方向計算 ===
    FVector Direction = CalculateCameraRelativeDirection(RawInput);

    //=== Step 4: デッドゾーン ===
    if (Direction.Size() < DeadzoneThreshold) 
    {
        UE_LOG(LogTemp, Verbose, TEXT("[Client] Input ignored: deadzone (%.2f < %.2f)"),
            Direction.Size(), DeadzoneThreshold);
        return;
    }

    Direction.Normalize();
    CachedInputDirection = FVector2D(Direction.X, Direction.Y);

    //=== Step 5: 8方向量子化 ===
    FIntPoint GridOffset = Quantize8Way(CachedInputDirection, AxisThreshold);

    //=== Step 6: コマンド作成 ===
    FPlayerCommand Command;
    Command.CommandTag = MoveInputTag;
    Command.Direction = FVector(GridOffset.X, GridOffset.Y, 0.0f);
    Command.TargetActor = GetPawn();
    Command.TargetCell = GetCurrentGridCell() + GridOffset;

    // ★ TurnManagerから現在のTurnIndexを直接取得（推論ではなく実値）
    Command.TurnId = CachedTurnManager->GetCurrentTurnIndex();
    Command.WindowId = CachedTurnManager->GetCurrentInputWindowId();

    UE_LOG(LogTemp, Log, TEXT("[Client] Command created: TurnId=%d (from TurnManager) WindowId=%d"),
        Command.TurnId, Command.WindowId);

    //==========================================================================
    // ★★★ Geminiが指摘したフェーズII：入力時の即座回転 ★★★
    //==========================================================================
    if (APawn* ControlledPawn = GetPawn())
    {
        // 現在位置とターゲット位置から方向ベクトルを計算
        FVector CurrentLocation = ControlledPawn->GetActorLocation();
        FVector TargetDirection = FVector(GridOffset.X, GridOffset.Y, 0.0f);
        
        if (!TargetDirection.IsNearlyZero())
        {
            // 方向ベクトルから回転を計算
            FRotator TargetRotation = TargetDirection.Rotation();
            
            // ユニットを即座に回転
            ControlledPawn->SetActorRotation(TargetRotation);
            
            UE_LOG(LogTemp, Log,
                TEXT("[Client] ✅ ROTATION: Input direction=(%d,%d) → Yaw=%.1f (Instant rotation applied)"),
                GridOffset.X, GridOffset.Y, TargetRotation.Yaw);
        }
    }

    //=== Step 7: サーバー送信 ===
    Server_SubmitCommand(Command);
    
    bSentThisInputWindow = true;
}





void APlayerControllerBase::Input_Move_Canceled(const FInputActionValue& Value)
{
    UE_LOG(LogTemp, Log, TEXT("[PlayerController] Input_Move_Canceled"));

    //==========================================================================
    // ★★★ 削除: TurnInputGuardへのリリース通知（3タグシステムで不要）
    //==========================================================================
    // if (UWorld* World = GetWorld())
    // {
    //     if (UTurnInputGuard* InputGuard = World->GetSubsystem<UTurnInputGuard>())
    //     {
    //         InputGuard->MarkReleased();
    //     }
    // }
}




void APlayerControllerBase::Input_Move_Completed(const FInputActionValue& Value)
{
    // 同一ウィンドウでの複数呼び出しを防止
    if (LastProcessedWindowId == CurrentInputWindowId)
    {
        UE_LOG(LogTemp, Verbose, TEXT("[PlayerController] InputMoveCompleted ignored (duplicate)"));
        return;  // 同じウィンドウでの重複を無視
    }
    
    LastProcessedWindowId = CurrentInputWindowId;

    UE_LOG(LogTemp, Log, TEXT("[PlayerController] Input_Move_Completed (WindowId=%d)"), 
        CurrentInputWindowId);
}



//------------------------------------------------------------------------------
// Input Handlers - IA_TurnFacing
//------------------------------------------------------------------------------

void APlayerControllerBase::Input_TurnFacing_Started(const FInputActionValue& Value)
{
    if (!IsValid(CachedTurnManager))
    {
        UE_LOG(LogTemp, Warning, TEXT("[PlayerController] Input_TurnFacing_Started: TurnManager invalid"));
        return;
    }

    const FVector2D InputAxis = Value.Get<FVector2D>();

    UE_LOG(LogTemp, Verbose, TEXT("[PlayerController] Input_TurnFacing_Started: InputAxis=(%.2f, %.2f)"),
        InputAxis.X, InputAxis.Y);

    FVector Direction = CalculateCameraRelativeDirection(InputAxis);

    if (Direction.Size() < DeadzoneThreshold)
    {
        UE_LOG(LogTemp, Verbose, TEXT("[PlayerController] Input ignored: deadzone"));
        return;
    }

    Direction.Normalize();
    CachedInputDirection = FVector2D(Direction.X, Direction.Y);

    UE_LOG(LogTemp, Log, TEXT("[PlayerController] TurnFacing direction cached: (%.2f, %.2f)"),
        CachedInputDirection.X, CachedInputDirection.Y);
}

void APlayerControllerBase::Input_TurnFacing_Triggered(const FInputActionValue& Value)
{
    // ■ Step 1: 現在の入力値を直接取得（Started キャッシュ不使用）
    const FVector2D Raw = Value.Get<FVector2D>();
    
    // ■ Step 2: カメラ相対方向に変換
    FVector Dir = CalculateCameraRelativeDirection(Raw);
    if (Dir.Size() < DeadzoneThreshold) 
    {
        return; // デッドゾーン内は送信しない
    }
    Dir.Normalize();

    // ■ Step 3: 8 方向量子化
    const FIntPoint Quant = Quantize8Way(FVector2D(Dir.X, Dir.Y), AxisThreshold);
    if (Quant.X == 0 && Quant.Y == 0) 
    {
        return; // 量子化結果が (0,0) なら無視
    }

    // ■ Step 4: 同一方向連投の抑制（帯域節約・RPC ロス対策）
    if (Quant == ServerLastTurnDirectionQuantized) 
    {
        return; // 直前と同一方向なら送信スキップ
    }

    // ■ Step 5: キャッシュ更新 & RPC 送信
    ServerLastTurnDirectionQuantized = Quant;
    Server_TurnFacing(FVector2D(Quant.X, Quant.Y)); // 現在値ベースで送信

    UE_LOG(LogTemp, Verbose, TEXT("[TurnFacing] Triggered: Quant=(%d,%d)"), Quant.X, Quant.Y);
}

//------------------------------------------------------------------------------
// Utility Functions
//------------------------------------------------------------------------------

FIntPoint APlayerControllerBase::Quantize8Way(const FVector2D& Direction, float Threshold) const
{
    float Sx = 0.0f;
    float Sy = 0.0f;

    if (FMath::Abs(Direction.X) >= Threshold)
    {
        Sx = FMath::Sign(Direction.X);
    }

    if (FMath::Abs(Direction.Y) >= Threshold)
    {
        Sy = FMath::Sign(Direction.Y);
    }

    FIntPoint Result(FMath::TruncToInt(Sx), FMath::TruncToInt(Sy));

    UE_LOG(LogTemp, Verbose, TEXT("[PlayerController] Quantize8Way: Input=(%.2f,%.2f) -> Output=(%d,%d)"),
        Direction.X, Direction.Y, Result.X, Result.Y);

    return Result;
}

FVector APlayerControllerBase::CalculateCameraRelativeDirection(const FVector2D& InputValue) const
{
    APlayerCameraManager* CameraMgr = PlayerCameraManager;
    
    if (!IsValid(CameraMgr))
    {
        // ⚠️ 一度だけ Warning を出す（ログスパム防止）
        static bool bLoggedOnce = false;
        if (!bLoggedOnce)
        {
            UE_LOG(LogTemp, Warning, TEXT("[TurnFacing] PlayerCameraManager is null, falling back to Pawn Forward"));
            bLoggedOnce = true;
        }
        
        // ■ フォールバック：Pawn の Forward/Right から算出
        if (APawn* P = GetPawn())
        {
            FVector Forward = P->GetActorForwardVector();
            FVector Right = P->GetActorRightVector();
            return (Forward * InputValue.Y + Right * InputValue.X).GetSafeNormal();
        }
        
        return FVector::ZeroVector; // 最終フォールバック（通常は到達しない）
    }

    // ■ 通常処理（既存）
    FRotator CameraRotation = CameraMgr->GetCameraRotation();
    CameraRotation.Pitch = 0.0f;
    CameraRotation.Roll = 0.0f;
    
    FVector Forward = FRotationMatrix(CameraRotation).GetScaledAxis(EAxis::X);
    FVector Right = FRotationMatrix(CameraRotation).GetScaledAxis(EAxis::Y);
    
    return (Forward * InputValue.Y + Right * InputValue.X);
}

FIntPoint APlayerControllerBase::GetCurrentGridCell() const
{
    if (!PathFinder)
    {
        UE_LOG(LogTemp, Error, TEXT("[PlayerController] GetCurrentGridCell: PathFinder is NULL"));
        return FIntPoint(0, 0);
    }

    APawn* ControlledPawn = GetPawn();
    if (!ControlledPawn)
    {
        UE_LOG(LogTemp, Error, TEXT("[PlayerController] GetCurrentGridCell: Controlled pawn is NULL"));
        return FIntPoint(0, 0);
    }

    FVector WorldLocation = ControlledPawn->GetActorLocation();
    FIntPoint GridCell = PathFinder->WorldToGrid(WorldLocation);

    UE_LOG(LogTemp, Verbose, TEXT("[PlayerController] GetCurrentGridCell: World=(%.2f,%.2f,%.2f) -> Grid=(%d,%d)"),
        WorldLocation.X, WorldLocation.Y, WorldLocation.Z,
        GridCell.X, GridCell.Y);

    return GridCell;
}

//------------------------------------------------------------------------------
// ★★★ 8近傍チェック（最小パッチ D）
//------------------------------------------------------------------------------

bool APlayerControllerBase::IsAdjacent(const FIntPoint& FromCell, const FIntPoint& ToCell) const
{
    const int32 Dx = FMath::Abs(FromCell.X - ToCell.X);
    const int32 Dy = FMath::Abs(FromCell.Y - ToCell.Y);

    // ★チェビシェフ距離: 最大差が1以下 かつ 同一セルでない
    const bool bIsAdjacent8 = (Dx <= 1 && Dy <= 1) && (Dx + Dy > 0);

    UE_LOG(LogTemp, Verbose, TEXT("[Server] IsAdjacent8: (%d,%d)->(%d,%d) = %s"),
        FromCell.X, FromCell.Y, ToCell.X, ToCell.Y,
        bIsAdjacent8 ? TEXT("TRUE") : TEXT("FALSE"));

    return bIsAdjacent8;
}

FIntPoint APlayerControllerBase::ServerGetCurrentCell() const
{
    return GetCurrentGridCell();
}

//------------------------------------------------------------------------------
// RPC Implementations
//------------------------------------------------------------------------------

void APlayerControllerBase::Server_SubmitCommand_Implementation(const FPlayerCommand& CommandIn)
{
    //==========================================================================
    // (1) 診断ログ最優先出力
    //==========================================================================
    UE_LOG(LogTemp, Warning, TEXT("[Server] ★ SubmitCommand RPC RECEIVED"));
    UE_LOG(LogTemp, Warning, TEXT("[Server] WindowId=%d, TurnId=%d, Tag=%s, Dir=(%.1f, %.1f)"),
        CommandIn.WindowId, CommandIn.TurnId, *CommandIn.CommandTag.ToString(),
        CommandIn.Direction.X, CommandIn.Direction.Y);
    
    UE_LOG(LogTemp, Error, TEXT("[Server] ========== RPC RECEIVED =========="));
    UE_LOG(LogTemp, Error, TEXT("[Server] TurnId received: %d"), CommandIn.TurnId);
    UE_LOG(LogTemp, Error, TEXT("[Server] WindowId received: %d"), CommandIn.WindowId);

    //=========================================================================
    // 検証: TurnManager
    //=========================================================================
    if (!CachedTurnManager || !IsValid(CachedTurnManager))
    {
        UE_LOG(LogTemp, Error, TEXT("[Server] TurnManager is invalid or null"));
        return;
    }

    //=========================================================================
    // 検証: WindowId/TurnId の整合性（過去/未来コマンドを拒否）
    //=========================================================================
    const int32 NowTurn  = CachedTurnManager->GetCurrentTurnIndex();
    const int32 NowWinId = CachedTurnManager->GetCurrentInputWindowId();
    if (CommandIn.TurnId != NowTurn || CommandIn.WindowId != NowWinId)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Server] REJECT: stale command (Win %d!=%d, Turn %d!=%d)"),
            CommandIn.WindowId, NowWinId, CommandIn.TurnId, NowTurn);
        return;
    }

    //=========================================================================
    // 検証: 二重鍵（WaitingForPlayerInput && Gate_Input_Open）
    //=========================================================================
    if (!CachedTurnManager->IsInputOpen_Server())
    {
        bool bGateOpen = false;
        if (const IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(GetPawn()))
        {
            if (UAbilitySystemComponent* ASC = ASI->GetAbilitySystemComponent())
            {
                bGateOpen = ASC->HasMatchingGameplayTag(RogueGameplayTags::Gate_Input_Open);
            }
        }
        UE_LOG(LogTemp, Warning, TEXT("[Server] REJECT: input gate not open (WPI=%s, Gate=%s)"),
            CachedTurnManager->WaitingForPlayerInput ? TEXT("true") : TEXT("false"),
            bGateOpen ? TEXT("open") : TEXT("closed"));
        return;
    }

    //=========================================================================
    // 入口一本化：TurnManagerのみが状態を動かす
    //=========================================================================
    CachedTurnManager->OnPlayerCommandAccepted(CommandIn);
}

//------------------------------------------------------------------------------
// ★★★ Server_TurnFacing修正（最小パッチ C）
//------------------------------------------------------------------------------

void APlayerControllerBase::Server_TurnFacing_Implementation(FVector2D Direction)
{
    // ■ 仕様確定：回転はターン非消費 → Barrier / WindowId / TurnManager を一切関与させない

    if (!IsValid(GetPawn())) 
    {
        return;
    }

    // ■ Step 1: Yaw に変換
    const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X));

    // ■ Step 2: ControlRotation を設定（ネットワーク平滑化＆自動レプリケーション）
    //          SetControlRotation は Character/Pawn で自動レプリケートされる
    SetControlRotation(FRotator(0.f, Yaw, 0.f));

    // ■ Step 3: 任意：即座にビジュアルを同期したい場合（補助的）
    //          SetActorRotation は環境によっては SetControlRotation と競合する可能性があり
    //          ⚠️ 必要に応じて片方へ統一、またはスムージング Lerp を使用
    if (APawn* P = GetPawn())
    {
        P->SetActorRotation(FRotator(0.f, Yaw, 0.f));
    }

    // ■ Step 4: ログ（Verbose は常時出力されないため低コスト）
    UE_LOG(LogTemp, Verbose, TEXT("[TurnFacing_Server] Yaw=%.1f (Non-Consuming, Barrier-free)"), Yaw);
}







//------------------------------------------------------------------------------
// Debug Commands (Exec Functions)
//------------------------------------------------------------------------------

void APlayerControllerBase::GridSmokeTest()
{
    UE_LOG(LogTemp, Warning, TEXT("[PlayerController] GridSmokeTest command received"));

    if (UWorld* World = GetWorld())
    {
        bool bFound = false;
        for (TActorIterator<AGridPathfindingLibrary> It(World); It; ++It)
        {
            AGridPathfindingLibrary* PathLib = *It;
            if (PathLib)
            {
                UE_LOG(LogTemp, Warning, TEXT("[PlayerController] Calling GridSmokeTest on: %s"), *PathLib->GetName());
                PathLib->GridSmokeTest();
                bFound = true;
                return;
            }
        }

        if (!bFound)
        {
            UE_LOG(LogTemp, Error, TEXT("[PlayerController] GridPathfindingLibrary not found in level"));
            UE_LOG(LogTemp, Error, TEXT("[PlayerController] Make sure BP_GridPathfindingLibrary is placed in the level"));
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[PlayerController] World is NULL"));
    }
}
