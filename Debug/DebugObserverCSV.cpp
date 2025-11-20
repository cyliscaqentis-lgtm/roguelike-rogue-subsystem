// Copyright Epic Games, Inc. All Rights Reserved.

// CodeRevision: INC-2025-1120-R9 (Switched to timestamp-based session filenames) (2025-11-20 00:00)
// CodeRevision: INC-2025-1120-R8 (Fix const-correctness compiler error in GetLogCount) (2025-11-20 00:00)
// CodeRevision: INC-2025-1120-R7 (Add thread-safety locks to prevent crash in logging) (2025-11-20 00:00)
// CodeRevision: INC-2025-1120-R6 (Rearchitected logger for one file per session and TurnID stamping) (2025-11-20 00:00)
// CodeRevision: INC-2025-1120-R5 (Add GetCurrentSessionID to enable unique log filenames) (2025-11-20 00:00)
// CodeRevision: INC-2025-1120-R4 (Fix C4459 compiler error by renaming LogLevel variable) (2025-11-20 00:00)
// CodeRevision: INC-2025-1120-R3 (Removed restrictive log filtering to capture all UE_LOG messages) (2025-11-20 00:00)
#include "Debug/DebugObserverCSV.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/DateTime.h"
#include "../Utility/ProjectDiagnostics.h"

// ★★★ セッションIDの静的カウンター ★★★

UDebugObserverCSV::UDebugObserverCSV()
{
    PrimaryComponentTick.bCanEverTick = false;
    bIsCapturingLogs = false;
}

UDebugObserverCSV::~UDebugObserverCSV()
{
    // ログキャプチャを停止
    if (bIsCapturingLogs && GLog)
    {
        GLog->RemoveOutputDevice(this);
        bIsCapturingLogs = false;
    }
}

void UDebugObserverCSV::BeginPlay()
{
    Super::BeginPlay();
    
    // ★★★ ログキャプチャを開始 ★★★
    if (GLog && !bIsCapturingLogs)
    {
        GLog->AddOutputDevice(this);
        bIsCapturingLogs = true;
    }
}

void UDebugObserverCSV::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // ログキャプチャを停止
    if (bIsCapturingLogs && GLog)
    {
        GLog->RemoveOutputDevice(this);
        bIsCapturingLogs = false;
    }
    
    Super::EndPlay(EndPlayReason);
}

void UDebugObserverCSV::Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category)
{
    // The filter has been removed. All UE_LOG messages are now processed.

    // Category nameから"Log"プレフィックスを除去
    FString CategoryName = Category.ToString();
    if (CategoryName.StartsWith(TEXT("Log")))
    {
        CategoryName = CategoryName.Mid(3); // "Log"を除去
    }
    
    // Verbosity levelを文字列に変換
    const TCHAR* VerbosityString = ToString(Verbosity);

    // CSVに追加
    LogMessageWithLevel(CategoryName, FString(V), FString(VerbosityString));
}

void UDebugObserverCSV::OnPhaseStarted_Implementation(FGameplayTag PhaseTag, const TArray<AActor*>& Actors)
{
    FScopeLock Lock(&LogLinesCS);
    LogLines.Add(FString::Printf(TEXT("PhaseStart,%d,%s,%d,%.6f"),
        LoggingTurnID,
        *PhaseTag.ToString(),
        Actors.Num(),
        FPlatformTime::Seconds()));
}

void UDebugObserverCSV::OnPhaseCompleted_Implementation(FGameplayTag PhaseTag, float DurationSeconds)
{
    FScopeLock Lock(&LogLinesCS);
    LogLines.Add(FString::Printf(TEXT("PhaseEnd,%d,%s,%.3f"),
        LoggingTurnID,
        *PhaseTag.ToString(),
        DurationSeconds * 1000.0f));
}

void UDebugObserverCSV::OnIntentGenerated_Implementation(AActor* Enemy, const FEnemyIntent& Intent)
{
    FScopeLock Lock(&LogLinesCS);
    const FString EnemyName = Enemy ? Enemy->GetName() : TEXT("None");
    LogLines.Add(FString::Printf(TEXT("Intent,%d,%s,%s,%s"),
        LoggingTurnID,
        *EnemyName,
        *Intent.AbilityTag.ToString(),
        *Intent.NextCell.ToString()));
}

bool UDebugObserverCSV::SaveToFile(const FString& Filename)
{
    const FString ProjectSavedDir = FPaths::ProjectSavedDir();
    const FString Directory = ProjectSavedDir / TEXT("TurnLogs");
    IFileManager::Get().MakeDirectory(*Directory, /*Tree*/true);

    const FString FilePath = Directory / Filename;
    
    UE_LOG(LogTemp, Log, TEXT("[DebugObserverCSV] Saving CSV to: %s"), *FilePath);
    
    TArray<FString> LinesWithHeader;
    LinesWithHeader.Add(TEXT("Type,TurnID,Category,Timestamp,Message"));
    
    {
        FScopeLock Lock(&LogLinesCS);
        LinesWithHeader.Append(LogLines);
    }
    
    if (!FFileHelper::SaveStringArrayToFile(LinesWithHeader, *FilePath))
    {
        UE_LOG(LogTemp, Warning, TEXT("[DebugObserverCSV] Failed to save log: %s"), *FilePath);
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("[DebugObserverCSV] Successfully saved CSV: %s"), *FilePath);
    return true;
}

void UDebugObserverCSV::ClearLogs()
{
    LogLines.Reset();
}

int32 UDebugObserverCSV::GetLogCount() const
{
    FScopeLock Lock(&LogLinesCS);
    return LogLines.Num();
}

FString UDebugObserverCSV::GetSessionTimestamp() const
{
    return SessionTimestamp;
}

void UDebugObserverCSV::LogMessage(const FString& Category, const FString& Message)
{
    // デフォルトのログレベルで記録
    LogMessageWithLevel(Category, Message, TEXT("Log"));
}

void UDebugObserverCSV::LogMessageWithLevel(const FString& Category, const FString& Message, const FString& InLogLevel)
{
    FScopeLock Lock(&LogLinesCS);

    FString EscapedMessage = Message;
    EscapedMessage.ReplaceInline(TEXT("\""), TEXT("\"\""));
    if (EscapedMessage.Contains(TEXT(",")) || EscapedMessage.Contains(TEXT("\"")) || EscapedMessage.Contains(TEXT("\n")))
    {
        EscapedMessage = FString::Printf(TEXT("\"%s\""), *EscapedMessage);
    }

    LogLines.Add(FString::Printf(TEXT("%s,%d,%s,%.6f,%s"),
        *InLogLevel,
        LoggingTurnID,
        *Category,
        FPlatformTime::Seconds(),
        *EscapedMessage));
}

void UDebugObserverCSV::SetCurrentTurnForLogging(int32 TurnID)
{
    LoggingTurnID = TurnID;
}

void UDebugObserverCSV::MarkSessionStart()
{
    FScopeLock Lock(&LogLinesCS);
    ClearLogs();

    SessionTimestamp = FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"));
    LoggingTurnID = 0; // Reset turn number at session start
    const double Timestamp = FPlatformTime::Seconds();

    LogLines.Add(FString::Printf(TEXT("SessionStart,%d,Session,%.6f,=== SESSION %s STARTED ==="),
        LoggingTurnID,
        Timestamp,
        *SessionTimestamp));

    UE_LOG(LogTemp, Log, TEXT("[CSV] Session %s started at %.6f (logs cleared)"), *SessionTimestamp, Timestamp);
}

void UDebugObserverCSV::MarkSessionEnd()
{
    if (SessionTimestamp.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[CSV] MarkSessionEnd called but no session was started"));
        return;
    }
    
    FScopeLock Lock(&LogLinesCS);
    const double Timestamp = FPlatformTime::Seconds();
    
    LogLines.Add(FString::Printf(TEXT("SessionEnd,%d,Session,%.6f,=== SESSION %s ENDED ==="),
        LoggingTurnID,
        Timestamp,
        *SessionTimestamp));
    
    UE_LOG(LogTemp, Log, TEXT("[CSV] Session %s ended at %.6f"), *SessionTimestamp, Timestamp);
}

