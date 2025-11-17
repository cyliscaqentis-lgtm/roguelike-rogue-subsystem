// Copyright Epic Games, Inc. All Rights Reserved.

// PlayerControllerBase.cpp
#include "Player/PlayerControllerBase.h"
#include "Turn/GameTurnManagerBase.h"
// #include "Turn/TurnManagerSubsystem.h"  // ★★★ 統合完了により削除 ★★★
// CodeRevision: INC-2025-00030-R2 (Migrate to UGridPathfindingSubsystem) (2025-11-17 00:40)
#include "Grid/GridPathfindingSubsystem.h"
// CodeRevision: INC-2025-00032-R1 (Add TurnFlowCoordinator include for GetCurrentTurnIndex() replacement) (2025-01-XX XX:XX)
#include "Turn/TurnFlowCoordinator.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Camera/PlayerCameraManager.h"
#include "Utility/RogueGameplayTags.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"
#include "Character/UnitManager.h"
#include "Character/UnitBase.h"
#include "Camera/LyraCameraComponent.h"
#include "Camera/LyraCameraMode.h"
#include "Character/LyraPawnExtensionComponent.h"
#include "Character/LyraPawnData.h"

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

    MoveInputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Move"));
    TurnInputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Turn"));

    AxisThreshold = 0.5f;
    DeadzoneThreshold = 0.5f;
    LastTurnDirection = FVector2D::ZeroVector;
    CachedInputDirection = FVector2D::ZeroVector;
    ServerLastTurnDirectionQuantized = FIntPoint(0, 0);
}


//------------------------------------------------------------------------------
// Lifecycle
//------------------------------------------------------------------------------

void APlayerControllerBase::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

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

    if (!CachedTurnManager || !IsValid(CachedTurnManager))
    {
        return;
    }

    if (!bPrevInitSynced)
    {
        bPrevWaitingForPlayerInput = CachedTurnManager->WaitingForPlayerInput;
        CurrentInputWindowId = CachedTurnManager->GetCurrentInputWindowId();
        bPrevInitSynced = true;
        UE_LOG(LogTemp, Warning, TEXT("[Client] Initial sync: WaitingForPlayerInput=%d, WindowId=%d"),
            bPrevWaitingForPlayerInput, CurrentInputWindowId);
    }

    const bool bNow = CachedTurnManager->WaitingForPlayerInput;
    const int32 NewWindowId = CachedTurnManager->GetCurrentInputWindowId();
    UE_LOG(LogTemp, Verbose, TEXT("[Client_Tick] WPI: Prev=%d, Now=%d, Sent=%d, WinId=%d->%d"),
        bPrevWaitingForPlayerInput, bNow, bSentThisInputWindow, CurrentInputWindowId, NewWindowId);

    if (!bPrevWaitingForPlayerInput && bNow)
    {
        bSentThisInputWindow = false;
        LastProcessedWindowId = INDEX_NONE;
        
        UE_LOG(LogTemp, Warning, TEXT("[Client_Tick] ★ INPUT WINDOW DETECTED: reset latch (WinId=%d)"),
            NewWindowId);
    }
    
    if (CurrentInputWindowId != NewWindowId)
    {
        bSentThisInputWindow = false;
        LastProcessedWindowId = INDEX_NONE;
        CurrentInputWindowId = NewWindowId;
    }
    
    if (bPrevWaitingForPlayerInput && !bNow)
    {
        bSentThisInputWindow = false;
        LastProcessedWindowId = CurrentInputWindowId;

        UE_LOG(LogTemp, Verbose, TEXT("[Client] Window CLOSE -> cleanup latch, mark WindowId=%d as processed"),
            CurrentInputWindowId);
    }

    CurrentInputWindowId = NewWindowId;
    bPrevWaitingForPlayerInput = bNow;
}








//------------------------------------------------------------------------------
// BeginPlay & OnPossess
//------------------------------------------------------------------------------

void APlayerControllerBase::BeginPlay()
{
    Super::BeginPlay();

    UE_LOG(LogTemp, Log, TEXT("[PlayerController] BeginPlay started"));

    SetActorTickEnabled(true);
    UE_LOG(LogTemp, Warning, TEXT("[PlayerController] BeginPlay: Tick enabled"));

    EnsureTurnManagerCached();

    if (!CachedTurnManager)
    {
        FTimerHandle RetryHandle;
        GetWorld()->GetTimerManager().SetTimer(
            RetryHandle,
            this,
            &APlayerControllerBase::EnsureTurnManagerCached,
            0.1f,
            true
        );
    }

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

void APlayerControllerBase::EnsureTurnManagerCached()
{
    if (CachedTurnManager && IsValid(CachedTurnManager))
    {
        if (UWorld* World = GetWorld())
        {
            World->GetTimerManager().ClearAllTimersForObject(this);
        }
        return;
    }

    if (UWorld* World = GetWorld())
    {
        for (TActorIterator<AGameTurnManagerBase> It(World); It; ++It)
        {
            CachedTurnManager = *It;
            if (CachedTurnManager)
            {
                // CodeRevision: INC-2025-00032-R1 (Remove GetCachedPathFinder() - use GetGridPathfindingSubsystem() instead) (2025-01-XX XX:XX)
                PathFinder = CachedTurnManager->GetGridPathfindingSubsystem();
                UE_LOG(LogTemp, Log, TEXT("[Client] TurnManager cached successfully: %s"),
                    *CachedTurnManager->GetName());

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

    SetViewTargetWithBlend(InPawn, 0.0f);

    if (InPawn)
    {
        FixedCameraYaw = InPawn->GetActorRotation().Yaw;
        UE_LOG(LogTemp, Warning, TEXT("[PlayerController] FixedCameraYaw initialized to %.1f"), FixedCameraYaw);
    }

    if (InPawn)
    {
        const FRotator YawOnly(0.f, FixedCameraYaw, 0.f);
        SetControlRotation(YawOnly);
        UE_LOG(LogTemp, Warning, TEXT("[PlayerController] Initial ControlRotation set to Yaw=%.1f"), YawOnly.Yaw);
    }

    bAutoManageActiveCameraTarget = true;

    if (InPawn)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PlayerController] Camera setup: ViewTarget=%s, bAutoManage=%d"),
            *InPawn->GetName(), bAutoManageActiveCameraTarget);

        if (AUnitBase* PlayerUnit = Cast<AUnitBase>(InPawn))
        {
            if (ULyraPawnExtensionComponent* Ext = PlayerUnit->FindComponentByClass<ULyraPawnExtensionComponent>())
            {
                if (const ULyraPawnData* PawnData = Ext->GetPawnData<ULyraPawnData>())
                {
                    UE_LOG(LogTemp, Warning, TEXT("[PlayerController] PawnData found: %s, DefaultCameraMode=%s"),
                        *PawnData->GetName(), *GetNameSafe(PawnData->DefaultCameraMode));

                    if (PawnData->DefaultCameraMode)
                    {
                        if (ULyraCameraComponent* CameraComp = PlayerUnit->FindComponentByClass<ULyraCameraComponent>())
                        {
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

    if (CachedTurnManager)
    {
        CachedTurnManager->NotifyPlayerPossessed(InPawn);
        UE_LOG(LogTemp, Log, TEXT("[PlayerController] NotifyPlayerPossessed sent"));
    }

    InitializeEnhancedInput();

    if (InPawn)
    {
        const FRotator YawOnly(0.f, FixedCameraYaw, 0.f);
        SetControlRotation(YawOnly);
        UE_LOG(LogTemp, Warning, TEXT("[PlayerController] Final ControlRotation re-fixed to Yaw=%.1f (post-IMC)"), YawOnly.Yaw);
    }
}

void APlayerControllerBase::UpdateRotation(float DeltaTime)
{
    if (FixedCameraYaw != TNumericLimits<float>::Max())
    {
        SetControlRotation(FRotator(0.f, FixedCameraYaw, 0.f));
    }
}

void APlayerControllerBase::AddYawInput(float Val)
{
    if (FMath::Abs(Val) > KINDA_SMALL_NUMBER)
    {
        UE_LOG(LogTemp, Warning, TEXT("[AddYawInput] Val=%.3f (Source: IMC Look/Orbit binding)"), Val);
    }
}

void APlayerControllerBase::InitializeEnhancedInput()
{
    UE_LOG(LogTemp, Log, TEXT("[PlayerController] InitializeEnhancedInput started"));

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

void APlayerControllerBase::Input_Move_Triggered(const FInputActionValue& Value)
{
    UE_LOG(LogTemp, Warning, TEXT("[Client] Input_Move_Triggered fired"));

    if (!IsValid(CachedTurnManager))
    {
        UE_LOG(LogTemp, Warning, TEXT("[Client] InputMoveTriggered: TurnManager invalid"));
        return;
    }

    const bool bWaitingReplicated = CachedTurnManager->WaitingForPlayerInput;

    bool bGateOpenClient = false;
    if (const IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(GetPawn()))
    {
        if (UAbilitySystemComponent* ASC = ASI->GetAbilitySystemComponent())
        {
            bGateOpenClient = ASC->HasMatchingGameplayTag(RogueGameplayTags::Gate_Input_Open);
        }
    }

    UE_LOG(LogTemp, Warning,
        TEXT("[Client] Input_Move_Triggered: bWaitingReplicated=%d, bGateOpenClient=%d, bSentThisInputWindow=%d, WindowId=%d"),
        bWaitingReplicated, bGateOpenClient, bSentThisInputWindow, CurrentInputWindowId);

    if (!bWaitingReplicated || !bGateOpenClient)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[Client] ❌ Input BLOCKED by gate check: WaitingForPlayerInput=%d, Gate=%d"),
            bWaitingReplicated, bGateOpenClient);
        return;
    }

    if (bSentThisInputWindow)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Client] ❌ Input BLOCKED by latch: bSentThisInputWindow=true, WindowId=%d"),
            CurrentInputWindowId);
        return;
    }

    UE_LOG(LogTemp, Warning, TEXT("[Client] ✅ Input PASSED all guards, preparing to send command"));

    const FVector2D RawInput = Value.Get<FVector2D>();
    FVector Direction = CalculateCameraRelativeDirection(RawInput);

    if (Direction.Size() < DeadzoneThreshold) 
    {
        UE_LOG(LogTemp, Verbose, TEXT("[Client] Input ignored: deadzone (%.2f < %.2f)"),
            Direction.Size(), DeadzoneThreshold);
        return;
    }

    Direction.Normalize();
    CachedInputDirection = FVector2D(Direction.X, Direction.Y);

    FIntPoint GridOffset = Quantize8Way(CachedInputDirection, AxisThreshold);

    FPlayerCommand Command;
    Command.CommandTag = MoveInputTag;
    Command.Direction = FVector(GridOffset.X, GridOffset.Y, 0.0f);
    Command.TargetActor = GetPawn();
    Command.TargetCell = GetCurrentGridCell() + GridOffset;

    // CodeRevision: INC-2025-00032-R1 (Replace GetCurrentTurnIndex() with TurnFlowCoordinator access) (2025-01-XX XX:XX)
    if (UWorld* World = GetWorld())
    {
        if (UTurnFlowCoordinator* TFC = World->GetSubsystem<UTurnFlowCoordinator>())
        {
            Command.TurnId = TFC->GetCurrentTurnIndex();
            Command.WindowId = TFC->GetCurrentInputWindowId();
        }
        else
        {
            // Fallback to TurnManager if TurnFlowCoordinator not available
            Command.TurnId = CachedTurnManager ? CachedTurnManager->GetCurrentTurnIndex() : 0;
            Command.WindowId = CachedTurnManager ? CachedTurnManager->GetCurrentInputWindowId() : 0;
        }
    }
    else
    {
        Command.TurnId = 0;
        Command.WindowId = 0;
    }

    UE_LOG(LogTemp, Log, TEXT("[Client] Command created: TurnId=%d (from TurnManager) WindowId=%d"),
        Command.TurnId, Command.WindowId);

    bSentThisInputWindow = true;

    UE_LOG(LogTemp, Warning, TEXT("[Client] 📤 Latch SET (before RPC): bSentThisInputWindow=TRUE, WindowId=%d"),
        Command.WindowId);

    Server_SubmitCommand(Command);

    UE_LOG(LogTemp, Warning, TEXT("[Client] 📤 RPC sent: Server_SubmitCommand(WindowId=%d)"),
        Command.WindowId);
}





void APlayerControllerBase::Input_Move_Canceled(const FInputActionValue& Value)
{
    UE_LOG(LogTemp, Log, TEXT("[PlayerController] Input_Move_Canceled"));
}




void APlayerControllerBase::Input_Move_Completed(const FInputActionValue& Value)
{
    UE_LOG(LogTemp, Verbose, TEXT("[PlayerController] Input_Move_Completed (no-op, WindowId=%d)"),
        CurrentInputWindowId);
}



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
    const FVector2D Raw = Value.Get<FVector2D>();
    
    FVector Dir = CalculateCameraRelativeDirection(Raw);
    if (Dir.Size() < DeadzoneThreshold) 
    {
        return;
    }
    Dir.Normalize();

    const FIntPoint Quant = Quantize8Way(FVector2D(Dir.X, Dir.Y), AxisThreshold);
    if (Quant.X == 0 && Quant.Y == 0) 
    {
        return;
    }

    if (Quant == ServerLastTurnDirectionQuantized) 
    {
        return;
    }

    ServerLastTurnDirectionQuantized = Quant;
    Server_TurnFacing(FVector2D(Quant.X, Quant.Y));

    UE_LOG(LogTemp, Verbose, TEXT("[TurnFacing] Triggered: Quant=(%d,%d)"), Quant.X, Quant.Y);
}

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
        static bool bLoggedOnce = false;
        if (!bLoggedOnce)
        {
            UE_LOG(LogTemp, Warning, TEXT("[TurnFacing] PlayerCameraManager is null, falling back to Pawn Forward"));
            bLoggedOnce = true;
        }
        
        if (APawn* P = GetPawn())
        {
            FVector Forward = P->GetActorForwardVector();
            FVector Right = P->GetActorRightVector();
            return (Forward * InputValue.Y + Right * InputValue.X).GetSafeNormal();
        }
        
        return FVector::ZeroVector;
    }

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

bool APlayerControllerBase::IsAdjacent(const FIntPoint& FromCell, const FIntPoint& ToCell) const
{
    const int32 Dx = FMath::Abs(FromCell.X - ToCell.X);
    const int32 Dy = FMath::Abs(FromCell.Y - ToCell.Y);

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

void APlayerControllerBase::Server_SubmitCommand_Implementation(const FPlayerCommand& CommandIn)
{
    UE_LOG(LogTemp, Warning, TEXT("[Server] ★ SubmitCommand RPC RECEIVED"));
    UE_LOG(LogTemp, Warning, TEXT("[Server] Client sent WindowId=%d, TurnId=%d, Tag=%s, Dir=(%.1f, %.1f)"),
        CommandIn.WindowId, CommandIn.TurnId, *CommandIn.CommandTag.ToString(),
        CommandIn.Direction.X, CommandIn.Direction.Y);

    if (!CachedTurnManager || !IsValid(CachedTurnManager))
    {
        UE_LOG(LogTemp, Error, TEXT("[Server] TurnManager is invalid or null"));
        return;
    }

    const int32 CurrentWindowId = CachedTurnManager->GetCurrentInputWindowId();

    if (CommandIn.WindowId != CurrentWindowId)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Server] REJECT: WindowId mismatch (Client=%d, Server=%d)"),
            CommandIn.WindowId, CurrentWindowId);
        return;
    }

    const FPlayerCommand& Command = CommandIn;

    UE_LOG(LogTemp, Log, TEXT("[Server] WindowId validated: %d"), Command.WindowId);

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

        Client_NotifyMoveRejected();

        return;
    }

    CachedTurnManager->OnPlayerCommandAccepted(Command);
}

void APlayerControllerBase::Server_TurnFacing_Implementation(FVector2D Direction)
{
    if (!IsValid(GetPawn())) 
    {
        return;
    }

    const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X));

    SetControlRotation(FRotator(0.f, Yaw, 0.f));

    if (APawn* P = GetPawn())
    {
        P->SetActorRotation(FRotator(0.f, Yaw, 0.f));
    }

    UE_LOG(LogTemp, Verbose, TEXT("[TurnFacing_Server] Yaw=%.1f (Non-Consuming, Barrier-free)"), Yaw);
}







//------------------------------------------------------------------------------
// Debug Commands (Exec Functions)
//------------------------------------------------------------------------------

// CodeRevision: INC-2025-00030-R2 (Migrate to UGridPathfindingSubsystem) (2025-11-17 00:40)
void APlayerControllerBase::GridSmokeTest()
{
    UE_LOG(LogTemp, Warning, TEXT("[PlayerController] GridSmokeTest command received"));

    if (UWorld* World = GetWorld())
    {
        UGridPathfindingSubsystem* PathSys = World->GetSubsystem<UGridPathfindingSubsystem>();
        if (PathSys)
        {
            UE_LOG(LogTemp, Warning, TEXT("[PlayerController] Calling GridSmokeTest on UGridPathfindingSubsystem"));
            PathSys->GridSmokeTest();
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("[PlayerController] UGridPathfindingSubsystem not found"));
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[PlayerController] World is NULL"));
    }
}

void APlayerControllerBase::Client_NotifyMoveRejected_Implementation()
{
    UE_LOG(LogTemp, Warning, TEXT("[Client] ★★★ MOVE REJECTED RPC RECEIVED ★★★"));
    UE_LOG(LogTemp, Warning, TEXT("[Client] BEFORE reset: bSentThisInputWindow=%d, LastProcessedWindowId=%d, WindowId=%d"),
        bSentThisInputWindow, LastProcessedWindowId, CurrentInputWindowId);

    bSentThisInputWindow = false;
    LastProcessedWindowId = INDEX_NONE;

    UE_LOG(LogTemp, Warning, TEXT("[Client] AFTER reset: bSentThisInputWindow=%s, LastProcessedWindowId=%d"),
        bSentThisInputWindow ? TEXT("TRUE") : TEXT("FALSE"), LastProcessedWindowId);

    if (!IsValid(CachedTurnManager))
    {
        UE_LOG(LogTemp, Error, TEXT("[Client] Client_NotifyMoveRejected: TurnManager invalid, cannot ensure Gate state"));
    }
    else
    {
        CachedTurnManager->ApplyWaitInputGate(true);
        UE_LOG(LogTemp, Warning, TEXT("[Client] ★ Gate_Input_Open tag explicitly re-applied via TurnManager"));
    }

    UE_LOG(LogTemp, Warning, TEXT("[Client] ✅ All state reset complete. Player can retry input."));
}

void APlayerControllerBase::Client_ConfirmCommandAccepted_Implementation(int32 WindowId)
{
    if (WindowId != CurrentInputWindowId)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Client] ACK IGNORED: WindowId mismatch (ACK=%d, Current=%d)"),
            WindowId, CurrentInputWindowId);
        return;
    }

    bSentThisInputWindow = true;
    LastProcessedWindowId = WindowId;

    UE_LOG(LogTemp, Log, TEXT("[Client] ★★★ COMMAND ACCEPTED ACK (WindowId=%d) -> Latches confirmed ★★★"),
        WindowId);
    UE_LOG(LogTemp, Log, TEXT("[Client]   bSentThisInputWindow: false -> true"));
    UE_LOG(LogTemp, Log, TEXT("[Client]   LastProcessedWindowId: %d -> %d"),
        LastProcessedWindowId == WindowId ? -1 : LastProcessedWindowId, WindowId);
}

void APlayerControllerBase::Client_ApplyFacingNoTurn_Implementation(int32 WindowId, FVector2D Direction)
{
    if (WindowId != CurrentInputWindowId)
    {
        UE_LOG(LogTemp, Verbose, TEXT("[Client] FacingNoTurn IGNORED: WindowId mismatch (RPC=%d, Current=%d)"),
            WindowId, CurrentInputWindowId);
        return;
    }

    if (APawn* ControlledPawn = GetPawn())
    {
        const float Yaw = FMath::Atan2(Direction.Y, Direction.X) * 180.f / PI;
        FRotator NewRotation = ControlledPawn->GetActorRotation();
        NewRotation.Yaw = Yaw;
        ControlledPawn->SetActorRotation(NewRotation);

        UE_LOG(LogTemp, Log, TEXT("[Client] ★ FACING ONLY (No Turn): Direction=(%.1f,%.1f), Yaw=%.1f"),
            Direction.X, Direction.Y, Yaw);
    }
}
