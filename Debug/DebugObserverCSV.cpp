// Copyright Epic Games, Inc. All Rights Reserved.

#include "Debug/DebugObserverCSV.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/OutputDeviceRedirector.h"
#include "../Utility/ProjectDiagnostics.h"

// ★★★ セッションIDの静的カウンター ★★★
int32 UDebugObserverCSV::NextSessionID = 1;

UDebugObserverCSV::UDebugObserverCSV()
{
    PrimaryComponentTick.bCanEverTick = false;
    CurrentSessionID = 0; // セッション開始時に設定される
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
    if (!bIsCurrentRecordFromRogue)
    {
        return;
    }

    bIsCurrentRecordFromRogue = false;

    // ★★★ RogueフォルダのC++ファイルからのログのみCSVに追加 ★★★
    if (!ShouldAddToCSV(Category, FString(V)))
    {
        return;
    }

    // カテゴリ名から"Log"プレフィックスを除去
    FString CategoryName = Category.ToString();
    if (CategoryName.StartsWith(TEXT("Log")))
    {
        CategoryName = CategoryName.Mid(3); // "Log"を除去
    }
    
    // CSVに追加
    LogMessageWithLevel(CategoryName, FString(V), TEXT("Log"));
}

void UDebugObserverCSV::SerializeRecord(const UE::FLogRecord& Record)
{
    bIsCurrentRecordFromRogue = IsRogueSourceFile(Record.GetFile());
    FOutputDevice::SerializeRecord(Record);
}


bool UDebugObserverCSV::ShouldAddToCSV(const FName& Category, const FString& Message) const
{
    return !Message.IsEmpty();
}

void UDebugObserverCSV::OnPhaseStarted_Implementation(FGameplayTag PhaseTag, const TArray<AActor*>& Actors)
{
    LogLines.Add(FString::Printf(TEXT("PhaseStart,%d,%s,%d,%.6f"),
        CurrentSessionID,
        *PhaseTag.ToString(),
        Actors.Num(),
        FPlatformTime::Seconds()));
}

void UDebugObserverCSV::OnPhaseCompleted_Implementation(FGameplayTag PhaseTag, float DurationSeconds)
{
    LogLines.Add(FString::Printf(TEXT("PhaseEnd,%d,%s,%.3f"),
        CurrentSessionID,
        *PhaseTag.ToString(),
        DurationSeconds * 1000.0f));
}

void UDebugObserverCSV::OnIntentGenerated_Implementation(AActor* Enemy, const FEnemyIntent& Intent)
{
    const FString EnemyName = Enemy ? Enemy->GetName() : TEXT("None");
    LogLines.Add(FString::Printf(TEXT("Intent,%d,%s,%s,%s"),
        CurrentSessionID,
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
    
    // ★★★ デバッグ：保存先の完全なパスをログ出力 ★★★
    UE_LOG(LogTemp, Log, TEXT("[DebugObserverCSV] ProjectSavedDir: %s"), *ProjectSavedDir);
    UE_LOG(LogTemp, Log, TEXT("[DebugObserverCSV] Saving CSV to: %s"), *FilePath);
    
    // ★★★ CSVヘッダー行を追加（SessionID列を追加） ★★★
    TArray<FString> LinesWithHeader;
    LinesWithHeader.Add(TEXT("Type,SessionID,Category,Timestamp,Message"));
    LinesWithHeader.Append(LogLines);
    
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

void UDebugObserverCSV::LogMessage(const FString& Category, const FString& Message)
{
    // デフォルトのログレベルで記録
    LogMessageWithLevel(Category, Message, TEXT("Log"));
}

void UDebugObserverCSV::LogMessageWithLevel(const FString& Category, const FString& Message, const FString& InLogLevel)
{
    FString EscapedMessage = Message;
    EscapedMessage.ReplaceInline(TEXT("\""), TEXT("\"\""));
    if (EscapedMessage.Contains(TEXT(",")) || EscapedMessage.Contains(TEXT("\"")) || EscapedMessage.Contains(TEXT("\n")))
    {
        EscapedMessage = FString::Printf(TEXT("\"%s\""), *EscapedMessage);
    }

    LogLines.Add(FString::Printf(TEXT("%s,%d,%s,%.6f,%s"),
        *InLogLevel,
        CurrentSessionID,
        *Category,
        FPlatformTime::Seconds(),
        *EscapedMessage));
}

void UDebugObserverCSV::MarkSessionStart()
{
    // ★★★ 変更（2025-11-12）：新しいセッション開始時に前回のログをクリア ★★★
    // 毎回消して直前のセッションのログだけを記録する
    ClearLogs();

    CurrentSessionID = NextSessionID++;
    const double Timestamp = FPlatformTime::Seconds();

    // ★★★ セッション開始マーカーを追加（CSV形式: Type,SessionID,Category,Timestamp,Message） ★★★
    // SessionIDをわかりやすく表示するため、メッセージに含める
    LogLines.Add(FString::Printf(TEXT("SessionStart,%d,Session,%.6f,=== SESSION %d STARTED ==="),
        CurrentSessionID,
        Timestamp,
        CurrentSessionID));

    UE_LOG(LogTemp, Log, TEXT("[CSV] Session %d started at %.6f (logs cleared)"), CurrentSessionID, Timestamp);
}

void UDebugObserverCSV::MarkSessionEnd()
{
    if (CurrentSessionID == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[CSV] MarkSessionEnd called but no session was started"));
        return;
    }
    
    const double Timestamp = FPlatformTime::Seconds();
    
    // ★★★ セッション終了マーカーを追加（CSV形式: Type,SessionID,Category,Timestamp,Message） ★★★
    // SessionIDをわかりやすく表示するため、メッセージに含める
    LogLines.Add(FString::Printf(TEXT("SessionEnd,%d,Session,%.6f,=== SESSION %d ENDED ==="),
        CurrentSessionID,
        Timestamp,
        CurrentSessionID));
    
    UE_LOG(LogTemp, Log, TEXT("[CSV] Session %d ended at %.6f"), CurrentSessionID, Timestamp);
    
    // セッションIDをリセット（次のセッション開始まで）
    // CurrentSessionID = 0; // コメントアウト：セッション終了後もIDを保持
}

bool UDebugObserverCSV::IsRogueSourceFile(const ANSICHAR* FilePath) const
{
    if (!FilePath)
    {
        return false;
    }

    const FString SourcePath = UTF8_TO_TCHAR(FilePath);
    return SourcePath.Contains(TEXT("/Rogue/"), ESearchCase::IgnoreCase)
        || SourcePath.Contains(TEXT("\\Rogue\\"), ESearchCase::IgnoreCase);
}
