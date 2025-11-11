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
#include "Character/UnitManager.h"  // ★★★ UnitManager用 ★★★
#include "Character/UnitBase.h"    // ★★★ UnitBase用 ★★★
#include "Camera/LyraCameraComponent.h"  // ★★★ カメラ切り替え用 ★★★
#include "Camera/LyraCameraMode.h"  // ★★★ カメラモード型定義用 ★★★
#include "Character/LyraPawnExtensionComponent.h"  // ★★★ PawnData取得用 ★★★
#include "Character/LyraPawnData.h"  // ★★★ カメラモード確認用 ★★★

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
    // ★★★ 修正: サーバー側が期待する InputTag.Move を使用（2025-11-09）
    MoveInputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Move"));
    TurnInputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Turn"));

    // デフォルト値の初期化
    AxisThreshold = 0.5f;
    DeadzoneThreshold = 0.5f;  // ★★★ 修正 (2025-11-11): "ちょい押し"誤移動防止のため0.1→0.5に変更
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

    // ★★★ カメラYaw診断（OnPossess直後1回のみ）
    static bool bDiagnosticDone = false;
    if (!bDiagnosticDone && GetPawn() && PlayerCameraManager)
    {
        const float CameraYaw = PlayerCameraManager->GetCameraRotation().Yaw;
        const float ControlYaw = GetControlRotation().Yaw;
        const float PawnYaw = GetPawn()->GetActorRotation().Yaw;

        UE_LOG(LogTemp, Warning, TEXT("[Camera Diagnostic] CameraYaw=%.1f, ControlYaw=%.1f, PawnYaw=%.1f"),
            CameraYaw, ControlYaw, PawnYaw);

        bDiagnosticDone = true;
    }

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
    
    // ★★★ 立ち下がり（true→false）: サーバーが受理してウィンドウを閉じた (2025-11-10) ★★★
    if (bPrevWaitingForPlayerInput && !bNow)
    {
        bSentThisInputWindow = false;

        // ★★★ CRITICAL FIX: ここで処理済みマーキング（サーバーが実際に受理した時点）★★★
        // Input_Move_Completed()ではなく、サーバーの状態遷移に従ってマーキング
        LastProcessedWindowId = CurrentInputWindowId;

        UE_LOG(LogTemp, Verbose, TEXT("[Client] Window CLOSE -> cleanup latch, mark WindowId=%d as processed"),
            CurrentInputWindowId);
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
    // ★★★ プレイヤー配置と敵スポーンのトリガー（2025-11-09）
    //==========================================================================
    if (InPawn && InPawn->GetName().Contains(TEXT("PlayerUnit")))
    {
        // UnitManagerを直接取得
        if (UWorld* World = GetWorld())
        {
            for (TActorIterator<AUnitManager> It(World); It; ++It)
            {
                if (AUnitManager* UnitMgr = *It)
                {
                    if (AUnitBase* PlayerUnit = Cast<AUnitBase>(InPawn))
                    {
                        UE_LOG(LogTemp, Log, TEXT("[PlayerController] Calling OnTBSCharacterPossessed for: %s"), *InPawn->GetName());
                        UnitMgr->OnTBSCharacterPossessed(PlayerUnit);
                    }
                    break;
                }
            }
        }
    }

    //==========================================================================
    // ★★★ カメラをPlayerUnitに切り替え（2025-11-09）
    // Lyraのカメラシステムを利用してPawnDataで設定されたカメラモードを適用
    //==========================================================================

    // ViewTargetをPlayerUnitに設定（即座に切り替え）
    SetViewTargetWithBlend(InPawn, 0.0f);

    // ★★★ カメラYaw値の初期化（2025-11-10）
    // OnPossess時のPawn Yawを記録（この値がカメラの固定Yawとなる）
    if (InPawn)
    {
        FixedCameraYaw = InPawn->GetActorRotation().Yaw;
        UE_LOG(LogTemp, Warning, TEXT("[PlayerController] FixedCameraYaw initialized to %.1f"), FixedCameraYaw);
    }

    // ★★★ 初回ControlRotationを強制設定（2025-11-10）
    // カメラを固定Yaw値に設定
    if (InPawn)
    {
        const FRotator YawOnly(0.f, FixedCameraYaw, 0.f);
        SetControlRotation(YawOnly);
        UE_LOG(LogTemp, Warning, TEXT("[PlayerController] Initial ControlRotation set to Yaw=%.1f"), YawOnly.Yaw);
    }

    // ★★★ Lyraの自動管理を有効化（PawnDataのDefaultCameraMode適用のため）
    bAutoManageActiveCameraTarget = true;

    // ★★★ カメラモードを明示的に強制適用（2025-11-09）
    // TBSプロジェクトではLyraHeroComponentを使用しないため、
    // DetermineCameraModeDelegate を手動で設定する必要がある
    if (InPawn)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PlayerController] Camera setup: ViewTarget=%s, bAutoManage=%d"),
            *InPawn->GetName(), bAutoManageActiveCameraTarget);

        // PawnDataのDefaultCameraMode確認と強制適用
        if (AUnitBase* PlayerUnit = Cast<AUnitBase>(InPawn))
        {
            if (ULyraPawnExtensionComponent* Ext = PlayerUnit->FindComponentByClass<ULyraPawnExtensionComponent>())
            {
                if (const ULyraPawnData* PawnData = Ext->GetPawnData<ULyraPawnData>())
                {
                    UE_LOG(LogTemp, Warning, TEXT("[PlayerController] PawnData found: %s, DefaultCameraMode=%s"),
                        *PawnData->GetName(), *GetNameSafe(PawnData->DefaultCameraMode));

                    // ★★★ DetermineCameraModeDelegate を設定（LyraHeroComponentの代替）
                    if (PawnData->DefaultCameraMode)
                    {
                        if (ULyraCameraComponent* CameraComp = PlayerUnit->FindComponentByClass<ULyraCameraComponent>())
                        {
                            // カメラモードを返すデリゲートを設定
                            TSubclassOf<ULyraCameraMode> CameraMode = PawnData->DefaultCameraMode;
                            CameraComp->DetermineCameraModeDelegate.BindLambda([CameraMode]()
                            {
                                return CameraMode;
                            });

                            UE_LOG(LogTemp, Warning, TEXT("[PlayerController] DetermineCameraModeDelegate bound to: %s"),
                                *GetNameSafe(PawnData->DefaultCameraMode));
                        }
                        else
                        {
                            UE_LOG(LogTemp, Error, TEXT("[PlayerController] NO LyraCameraComponent on PlayerUnit! Cannot set camera mode!"));
                        }
                    }
                }
                else
                {
                    UE_LOG(LogTemp, Error, TEXT("[PlayerController] NO PawnData on PlayerUnit! Camera mode won't apply!"));
                }
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("[PlayerController] NO PawnExtensionComponent on PlayerUnit!"));
            }
        }
    }

    //==========================================================================
    // ★ TurnManagerにPossess通知（入力窓を開き直すトリガー）
    //==========================================================================
    if (CachedTurnManager)
    {
        CachedTurnManager->NotifyPlayerPossessed(InPawn);
        UE_LOG(LogTemp, Log, TEXT("[PlayerController] NotifyPlayerPossessed sent"));
    }

    // EnhancedInput初期化（Possess後に確実に実行）
    InitializeEnhancedInput();

    // ★★★ 最終固定：InitializeEnhancedInput後にControlRotationを再固定（2025-11-10）
    // SetIgnoreLookInput(true)でLook入力を遮断しても、IMC追加時に既に入力が入っている可能性があるため
    if (InPawn)
    {
        const FRotator YawOnly(0.f, FixedCameraYaw, 0.f);
        SetControlRotation(YawOnly);
        UE_LOG(LogTemp, Warning, TEXT("[PlayerController] Final ControlRotation re-fixed to Yaw=%.1f (post-IMC)"), YawOnly.Yaw);
    }
}

void APlayerControllerBase::UpdateRotation(float DeltaTime)
{
    // ★★★ TBSではControlRotationを固定（2025-11-10）
    // マウス/右スティック等からのAddYawInput/AddPitchInputによる変化を遮断
    // Super::UpdateRotation(DeltaTime); // 呼ばない

    // ★★★ 修正: Pawnの向きに追従せず、初期Yaw値を維持（2025-11-10）
    // プレイヤー歩行時にPawnが回転してもカメラは固定される
    if (FixedCameraYaw != TNumericLimits<float>::Max())
    {
        SetControlRotation(FRotator(0.f, FixedCameraYaw, 0.f));
    }
}

void APlayerControllerBase::AddYawInput(float Val)
{
    // ★★★ 診断ログ：誰がYaw入力を送っているか確認（2025-11-09）
    if (FMath::Abs(Val) > KINDA_SMALL_NUMBER)
    {
        UE_LOG(LogTemp, Warning, TEXT("[AddYawInput] Val=%.3f (Source: IMC Look/Orbit binding)"), Val);
    }

    // ★★★ TBSでは無視（SetIgnoreLookInput(true)で既に遮断されているはずだが、念のため）
    // Super::AddYawInput(Val); // 呼ばない
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

    // ★★★ Look/Orbit入力を無効化（2025-11-09）
    // TBSではControlRotationを固定するため、マウス/右スティックからの視点回転を遮断
    SetIgnoreLookInput(true);
    UE_LOG(LogTemp, Warning, TEXT("[PlayerController] SetIgnoreLookInput(true) - Look inputs disabled for TBS"));

    UE_LOG(LogTemp, Log, TEXT("[PlayerController] InitializeEnhancedInput completed"));
}

void APlayerControllerBase::SetupInputComponent()
{
    Super::SetupInputComponent();

    UE_LOG(LogTemp, Log, TEXT("[PlayerController] SetupInputComponent started"));

    if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent))
    {
        // IA_Moveのバインド（4つのイベント）
        // ★★★ 2025-11-09: Started も追加（Triggered が走らないケースの救済）
        if (IA_Move)
        {
            EnhancedInput->BindAction(IA_Move, ETriggerEvent::Started, this,
                &APlayerControllerBase::Input_Move_Triggered);
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
    UE_LOG(LogTemp, Warning, TEXT("[Client] Input_Move_Triggered fired"));

    //==========================================================================
    // Step 1: TurnManager検証
    //==========================================================================
    if (!IsValid(CachedTurnManager))
    {
        UE_LOG(LogTemp, Warning, TEXT("[Client] InputMoveTriggered: TurnManager invalid"));
        return;
    }

    //==========================================================================
    // Step 2: 入力ウィンドウチェック（参考ログのみ・ブロックしない）
    // ★★★ 2025-11-09: サーバ側で最終判定するため、クライアントでは遮断しない
    //==========================================================================
    bool bGateOpenClient = false;
    if (const IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(GetPawn()))
    {
        if (UAbilitySystemComponent* ASC = ASI->GetAbilitySystemComponent())
        {
            bGateOpenClient = ASC->HasMatchingGameplayTag(RogueGameplayTags::Gate_Input_Open);
        }
    }
    UE_LOG(LogTemp, Log, TEXT("[Client] WaitingForPlayerInput=%d, Gate(open?)=%d (参考値・送信は継続)"),
        CachedTurnManager->WaitingForPlayerInput, bGateOpenClient);

    //==========================================================================
    // ★ Step 3: 送信済みチェック（クライアント側の多重送信防止）
    //==========================================================================
    if (bSentThisInputWindow)
    {
        UE_LOG(LogTemp, Verbose, TEXT("[Client] Command already sent for this window, ignoring"));
        return;
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
        // ★★★ DISABLED: プレイヤー回転を無効化（カメラ視点変化を防止） (2025-11-09) ★★★
        // TBSゲームでは固定視点が望ましい。プレイヤーを回転させるとカメラも追従して視点が変わってしまう。
        // 以前の実装:
        //   FVector TargetDirection = FVector(GridOffset.X, GridOffset.Y, 0.0f);
        //   ControlledPawn->SetActorRotation(TargetDirection.Rotation());
        //
        // もし向きを変えたい場合は、GA_MoveBase側でアニメーション中のみ回転させるか、
        // またはカメラを親の回転に追従させない設定にする必要がある。
    }

    //=== Step 7: サーバー送信 ===
    Server_SubmitCommand(Command);

    // ★★★ CRITICAL FIX (2025-11-10): ACK方式に変更 ★★★
    // 送信直後にラッチを立てない。サーバーからのACK（Client_ConfirmCommandAccepted）
    // 受信時にラッチを確定する。これにより、拒否RPC遅延/ロス時でも入力が詰まらない。
    //
    // 削除: bSentThisInputWindow = true;
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
    // ★★★ CRITICAL FIX (2025-11-10): Completedでは処理済みマーキングしない ★★★
    // Input_Move_Completed()は単なる入力イベントの終了（キーを離した等）であり、
    // サーバーの受理とは無関係。処理済みマーキングはWaitingForPlayerInputの
    // 立ち下がり（サーバーが実際に受理してウィンドウを閉じた時）で行う。
    //
    // 旧実装の問題：
    // 1. 壁衝突でREJECT → Client_NotifyMoveRejected() → ラッチリセット
    // 2. 直後にCompleted発火 → LastProcessedWindowId再設定
    // 3. 次の入力がCompleted重複ガードに引っかかる
    //
    // 削除：LastProcessedWindowId = CurrentInputWindowId;

    UE_LOG(LogTemp, Verbose, TEXT("[PlayerController] Input_Move_Completed (no-op, WindowId=%d)"),
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
    UE_LOG(LogTemp, Warning, TEXT("[Server] Client sent WindowId=%d, TurnId=%d, Tag=%s, Dir=(%.1f, %.1f)"),
        CommandIn.WindowId, CommandIn.TurnId, *CommandIn.CommandTag.ToString(),
        CommandIn.Direction.X, CommandIn.Direction.Y);

    //=========================================================================
    // 検証: TurnManager
    //=========================================================================
    if (!CachedTurnManager || !IsValid(CachedTurnManager))
    {
        UE_LOG(LogTemp, Error, TEXT("[Server] TurnManager is invalid or null"));
        return;
    }

    //=========================================================================
    // ★ WindowId検証（重複検出）
    //=========================================================================
    const int32 CurrentWindowId = CachedTurnManager->GetCurrentInputWindowId();

    if (CommandIn.WindowId != CurrentWindowId)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Server] REJECT: WindowId mismatch (Client=%d, Server=%d)"),
            CommandIn.WindowId, CurrentWindowId);
        return;
    }

    // クライアントから送られたコマンドをそのまま使用（上書きしない）
    const FPlayerCommand& Command = CommandIn;

    UE_LOG(LogTemp, Log, TEXT("[Server] WindowId validated: %d"), Command.WindowId);

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

        // ★★★ CRITICAL FIX (2025-11-10): クライアントに拒否を通知してラッチリセット ★★★
        Client_NotifyMoveRejected();

        return;
    }

    //=========================================================================
    // 入口一本化：TurnManagerのみが状態を動かす
    // ★★★ 2025-11-09: 上書き後のCommandを渡す
    //=========================================================================
    CachedTurnManager->OnPlayerCommandAccepted(Command);
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

//==============================================================================
// ★★★ Client RPC: 移動コマンド拒否通知 (2025-11-09) ★★★
//==============================================================================
void APlayerControllerBase::Client_NotifyMoveRejected_Implementation()
{
    UE_LOG(LogTemp, Warning, TEXT("[Client] ★★★ MOVE REJECTED RPC RECEIVED ★★★"));
    UE_LOG(LogTemp, Warning, TEXT("[Client] Resetting bSentThisInputWindow: %d -> false, LastProcessedWindowId: %d -> INDEX_NONE"),
        bSentThisInputWindow, LastProcessedWindowId);

    // ★★★ CRITICAL FIX (2025-11-10): 両方のラッチをリセット ★★★
    // bSentThisInputWindow: 同一ウィンドウ内での送信制御
    // LastProcessedWindowId: Input_Move_Completed()の重複防止ガードをリセット
    bSentThisInputWindow = false;
    LastProcessedWindowId = INDEX_NONE;

    UE_LOG(LogTemp, Warning, TEXT("[Client] Both latches reset complete. Player can retry input."));
}

//==============================================================================
// ★★★ Client RPC: コマンド受理確認（ACK）(2025-11-10) ★★★
//==============================================================================
void APlayerControllerBase::Client_ConfirmCommandAccepted_Implementation(int32 WindowId)
{
    // 同一ウィンドウのACKのみ有効（古いACKを無視）
    if (WindowId != CurrentInputWindowId)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Client] ACK IGNORED: WindowId mismatch (ACK=%d, Current=%d)"),
            WindowId, CurrentInputWindowId);
        return;
    }

    // ★★★ サーバー確定後にラッチを確定 ★★★
    bSentThisInputWindow = true;
    LastProcessedWindowId = WindowId;

    UE_LOG(LogTemp, Log, TEXT("[Client] ★★★ COMMAND ACCEPTED ACK (WindowId=%d) -> Latches confirmed ★★★"),
        WindowId);
    UE_LOG(LogTemp, Log, TEXT("[Client]   bSentThisInputWindow: false -> true"));
    UE_LOG(LogTemp, Log, TEXT("[Client]   LastProcessedWindowId: %d -> %d"),
        LastProcessedWindowId == WindowId ? -1 : LastProcessedWindowId, WindowId);
}

//==============================================================================
// ★★★ Client RPC: 回転のみ適用（ターン不消費） (2025-11-10) ★★★
//==============================================================================
void APlayerControllerBase::Client_ApplyFacingNoTurn_Implementation(int32 WindowId, FVector2D Direction)
{
    // 同一ウィンドウのみ有効（古いRPCを無視）
    if (WindowId != CurrentInputWindowId)
    {
        UE_LOG(LogTemp, Verbose, TEXT("[Client] FacingNoTurn IGNORED: WindowId mismatch (RPC=%d, Current=%d)"),
            WindowId, CurrentInputWindowId);
        return;
    }

    // ★★★ ラッチは変更しない（入力継続可能） ★★★
    // bSentThisInputWindow / LastProcessedWindowId は触らない

    // プレイヤーの向きを変更
    if (APawn* ControlledPawn = GetPawn())
    {
        const float Yaw = FMath::Atan2(Direction.Y, Direction.X) * 180.f / PI;
        FRotator NewRotation = ControlledPawn->GetActorRotation();
        NewRotation.Yaw = Yaw;
        ControlledPawn->SetActorRotation(NewRotation);

        UE_LOG(LogTemp, Log, TEXT("[Client] ★ FACING ONLY (No Turn): Direction=(%.1f,%.1f), Yaw=%.1f"),
            Direction.X, Direction.Y, Yaw);
    }

    // 任意：フェイシング専用のフィードバック（アニメーション、サウンド等）
}
