// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Debug/DebugObserverInterface.h"
#include "Misc/OutputDevice.h"
#include "Logging/StructuredLog.h"
#include "HAL/CriticalSection.h"
#include "Misc/DateTime.h"
#include "DebugObserverCSV.generated.h"

// Log category
DECLARE_LOG_CATEGORY_EXTERN(LogDebugObserver, Log, All);

// CodeRevision: INC-2025-1120-R9 (Switched to timestamp-based session filenames) (2025-11-20 00:00)
// CodeRevision: INC-2025-1120-R8 (Fix const-correctness compiler error in GetLogCount) (2025-11-20 00:00)
// CodeRevision: INC-2025-1120-R7 (Add thread-safety locks to prevent crash in logging) (2025-11-20 00:00)
// CodeRevision: INC-2025-1120-R6 (Rearchitected logger for one file per session and TurnID stamping) (2025-11-20 00:00)
// CodeRevision: INC-2025-1120-R5 (Add GetCurrentSessionID to enable unique log filenames) (2025-11-20 00:00)
// CodeRevision: INC-2025-1120-R3 (Removed restrictive log filtering to capture all UE_LOG messages) (2025-11-20 00:00)
/**
 * Simple CSV logger for turn-system debug information.
 * Can be swapped out in Blueprints by implementing IDebugObserver elsewhere.
 */
UCLASS(ClassGroup = (Turn), meta = (BlueprintSpawnableComponent))
class LYRAGAME_API UDebugObserverCSV : public UActorComponent, public IDebugObserver, public FOutputDevice
{
    GENERATED_BODY()

public:
    UDebugObserverCSV();
    virtual ~UDebugObserverCSV();

    // UActorComponent interface ---------------------------------------------------
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    // FOutputDevice interface ---------------------------------------------------
    virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override;

    // IDebugObserver interface ------------------------------------------------
    virtual void OnPhaseStarted_Implementation(FGameplayTag PhaseTag, const TArray<AActor*>& Actors) override;
    virtual void OnPhaseCompleted_Implementation(FGameplayTag PhaseTag, float DurationSeconds) override;
    virtual void OnIntentGenerated_Implementation(AActor* Enemy, const FEnemyIntent& Intent) override;

    // CSV export --------------------------------------------------------------
    UFUNCTION(BlueprintCallable, Category = "Turn|Debug")
    bool SaveToFile(const FString& Filename);

    UFUNCTION(BlueprintCallable, Category = "Turn|Debug")
    void ClearLogs();

    UFUNCTION(BlueprintPure, Category = "Turn|Debug")
    int32 GetLogCount() const;

    FString GetSessionTimestamp() const;

    // ★★★ RogueフォルダのC++ファイルからのログ記録用 ★★★
    UFUNCTION(BlueprintCallable, Category = "Turn|Debug")
    void LogMessage(const FString& Category, const FString& Message);
    
    // ★★★ ログレベルを指定できるバージョン ★★★
    UFUNCTION(BlueprintCallable, Category = "Turn|Debug")
    void LogMessageWithLevel(const FString& Category, const FString& Message, const FString& InLogLevel);
    
    // ★★★ プレイセッション管理 ★★★
    UFUNCTION(BlueprintCallable, Category = "Turn|Debug")
    void MarkSessionStart();
    
    UFUNCTION(BlueprintCallable, Category = "Turn|Debug")
    void MarkSessionEnd();

    // Turn Number Management
    void SetCurrentTurnForLogging(int32 TurnID);

private:
    /** Thread-safe access to LogLines */
    mutable FCriticalSection LogLinesCS;

    UPROPERTY()
    TArray<FString> LogLines;
    
    // ★★★ セッション管理 ★★★
    FString SessionTimestamp;
    int32 LoggingTurnID = -1; // Current turn number for logging
    
    // ★★★ ログキャプチャ用 ★★★
    bool bIsCapturingLogs = false;
};
