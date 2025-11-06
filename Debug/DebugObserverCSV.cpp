// Copyright Epic Games, Inc. All Rights Reserved.

#include "Debug/DebugObserverCSV.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

UDebugObserverCSV::UDebugObserverCSV()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UDebugObserverCSV::OnPhaseStarted_Implementation(FGameplayTag PhaseTag, const TArray<AActor*>& Actors)
{
    LogLines.Add(FString::Printf(TEXT("PhaseStart,%s,%d,%.6f"),
        *PhaseTag.ToString(),
        Actors.Num(),
        FPlatformTime::Seconds()));
}

void UDebugObserverCSV::OnPhaseCompleted_Implementation(FGameplayTag PhaseTag, float DurationSeconds)
{
    LogLines.Add(FString::Printf(TEXT("PhaseEnd,%s,%.3f"),
        *PhaseTag.ToString(),
        DurationSeconds * 1000.0f));
}

void UDebugObserverCSV::OnIntentGenerated_Implementation(AActor* Enemy, const FEnemyIntent& Intent)
{
    const FString EnemyName = Enemy ? Enemy->GetName() : TEXT("None");
    LogLines.Add(FString::Printf(TEXT("Intent,%s,%s,%s"),
        *EnemyName,
        *Intent.AbilityTag.ToString(),
        *Intent.NextCell.ToString()));
}

bool UDebugObserverCSV::SaveToFile(const FString& Filename)
{
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("TurnLogs");
    IFileManager::Get().MakeDirectory(*Directory, /*Tree*/true);

    const FString FilePath = Directory / Filename;
    if (!FFileHelper::SaveStringArrayToFile(LogLines, *FilePath))
    {
        UE_LOG(LogTemp, Warning, TEXT("[DebugObserverCSV] Failed to save log: %s"), *FilePath);
        return false;
    }

    return true;
}

void UDebugObserverCSV::ClearLogs()
{
    LogLines.Reset();
}
