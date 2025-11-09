// ============================================================================
// TurnDebugSubsystem.cpp
// ターンシステムデバッグSubsystem実装
// GameTurnManagerBaseから分離（2025-11-09）
// ============================================================================

#include "Turn/TurnDebugSubsystem.h"
#include "Debug/DebugObserverCSV.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "../ProjectDiagnostics.h"

//------------------------------------------------------------------------------
// Subsystem Lifecycle
//------------------------------------------------------------------------------

void UTurnDebugSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	bDebugDrawEnabled = false;
	bDebugLogEnabled = true;
	FrameTimeHistory.Reserve(MaxHistorySize);

	UE_LOG(LogTurnManager, Log, TEXT("[TurnDebugSubsystem] Initialized"));
}

void UTurnDebugSubsystem::Deinitialize()
{
	StopCSVRecording();
	FrameTimeHistory.Empty();

	UE_LOG(LogTurnManager, Log, TEXT("[TurnDebugSubsystem] Deinitialized"));

	Super::Deinitialize();
}

//------------------------------------------------------------------------------
// Debug Output
//------------------------------------------------------------------------------

void UTurnDebugSubsystem::DumpTurnState(int32 TurnId)
{
	if (!bDebugLogEnabled)
	{
		return;
	}

	UE_LOG(LogTurnManager, Log, TEXT("========== Turn State Dump =========="));
	UE_LOG(LogTurnManager, Log, TEXT("TurnId: %d"), TurnId);
	UE_LOG(LogTurnManager, Log, TEXT("Debug Draw: %s"), bDebugDrawEnabled ? TEXT("Enabled") : TEXT("Disabled"));
	UE_LOG(LogTurnManager, Log, TEXT("Debug Log: %s"), bDebugLogEnabled ? TEXT("Enabled") : TEXT("Disabled"));
	UE_LOG(LogTurnManager, Log, TEXT("Frame History Size: %d"), FrameTimeHistory.Num());
	UE_LOG(LogTurnManager, Log, TEXT("====================================="));
}

void UTurnDebugSubsystem::LogPhaseTransition(FGameplayTag OldPhase, FGameplayTag NewPhase)
{
	if (!bDebugLogEnabled)
	{
		return;
	}

	const FString Message = FString::Printf(TEXT("Phase transition: %s -> %s"),
		*OldPhase.ToString(), *NewPhase.ToString());

	LogDebugMessage(TEXT("PhaseTransition"), Message);

	// CSV記録
	if (CSVLogger)
	{
		// TODO: CSV記録の実装
	}
}

void UTurnDebugSubsystem::DrawDebugInfo()
{
	if (!bDebugDrawEnabled)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// デバッグ情報を画面に描画
	// TODO: 実際の描画処理を実装
	const FVector DebugLocation(0, 0, 200);
	DrawDebugString(World, DebugLocation, TEXT("Turn Debug Info"), nullptr, FColor::Green, 0.0f, true);
}

//------------------------------------------------------------------------------
// CSV Recording
//------------------------------------------------------------------------------

void UTurnDebugSubsystem::StartCSVRecording(const FString& FilePath)
{
	if (!CSVLogger)
	{
		CSVLogger = NewObject<UDebugObserverCSV>(this);
	}

	if (CSVLogger)
	{
		// TODO: CSV記録開始の実装
		UE_LOG(LogTurnManager, Log, TEXT("[TurnDebugSubsystem] CSV recording started: %s"), *FilePath);
	}
}

void UTurnDebugSubsystem::StopCSVRecording()
{
	if (CSVLogger)
	{
		// TODO: CSV記録停止の実装
		UE_LOG(LogTurnManager, Log, TEXT("[TurnDebugSubsystem] CSV recording stopped"));
		CSVLogger = nullptr;
	}
}

//------------------------------------------------------------------------------
// Debug Logging
//------------------------------------------------------------------------------

void UTurnDebugSubsystem::LogDebugMessage(const FString& Category, const FString& Message)
{
	if (!bDebugLogEnabled)
	{
		return;
	}

	const FString FormattedMessage = FormatDebugMessage(Category, Message);
	UE_LOG(LogTurnManager, Log, TEXT("%s"), *FormattedMessage);
}

void UTurnDebugSubsystem::DumpPerformanceStats()
{
	if (!bDebugLogEnabled || FrameTimeHistory.Num() == 0)
	{
		return;
	}

	float TotalTime = 0.0f;
	float MinTime = FLT_MAX;
	float MaxTime = 0.0f;

	for (float FrameTime : FrameTimeHistory)
	{
		TotalTime += FrameTime;
		MinTime = FMath::Min(MinTime, FrameTime);
		MaxTime = FMath::Max(MaxTime, FrameTime);
	}

	const float AvgTime = TotalTime / FrameTimeHistory.Num();

	UE_LOG(LogTurnManager, Log, TEXT("========== Performance Stats =========="));
	UE_LOG(LogTurnManager, Log, TEXT("Samples: %d"), FrameTimeHistory.Num());
	UE_LOG(LogTurnManager, Log, TEXT("Avg Frame Time: %.3f ms"), AvgTime * 1000.0f);
	UE_LOG(LogTurnManager, Log, TEXT("Min Frame Time: %.3f ms"), MinTime * 1000.0f);
	UE_LOG(LogTurnManager, Log, TEXT("Max Frame Time: %.3f ms"), MaxTime * 1000.0f);
	UE_LOG(LogTurnManager, Log, TEXT("======================================="));
}

//------------------------------------------------------------------------------
// Configuration
//------------------------------------------------------------------------------

void UTurnDebugSubsystem::SetDebugDrawEnabled(bool bEnabled)
{
	bDebugDrawEnabled = bEnabled;
	UE_LOG(LogTurnManager, Log, TEXT("[TurnDebugSubsystem] Debug draw %s"),
		bEnabled ? TEXT("enabled") : TEXT("disabled"));
}

void UTurnDebugSubsystem::SetDebugLogEnabled(bool bEnabled)
{
	bDebugLogEnabled = bEnabled;
	UE_LOG(LogTurnManager, Log, TEXT("[TurnDebugSubsystem] Debug log %s"),
		bEnabled ? TEXT("enabled") : TEXT("disabled"));
}

//------------------------------------------------------------------------------
// Internal Helpers
//------------------------------------------------------------------------------

void UTurnDebugSubsystem::UpdateStats(float DeltaTime)
{
	FrameTimeHistory.Add(DeltaTime);

	// 履歴サイズを制限
	if (FrameTimeHistory.Num() > MaxHistorySize)
	{
		FrameTimeHistory.RemoveAt(0);
	}
}

FString UTurnDebugSubsystem::FormatDebugMessage(const FString& Category, const FString& Message) const
{
	const UWorld* World = GetWorld();
	const float CurrentTime = World ? World->GetTimeSeconds() : 0.0f;

	return FString::Printf(TEXT("[%.2f][%s] %s"), CurrentTime, *Category, *Message);
}
