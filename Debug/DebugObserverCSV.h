// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Debug/DebugObserverInterface.h"
#include "Misc/OutputDevice.h"
#include "Logging/StructuredLog.h"
#include "DebugObserverCSV.generated.h"

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
    virtual void SerializeRecord(const UE::FLogRecord& Record) override;

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
    int32 GetLogCount() const { return LogLines.Num(); }

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

private:
    UPROPERTY()
    TArray<FString> LogLines;
    
    // ★★★ セッション管理 ★★★
    int32 CurrentSessionID = 0;
    static int32 NextSessionID;
    
    // ★★★ ログキャプチャ用 ★★★
    bool bIsCapturingLogs = false;
    bool bIsCurrentRecordFromRogue = false;
    
    // ★★★ ログをCSVに追加するかどうかを判定 ★★★
    bool ShouldAddToCSV(const FName& Category, const FString& Message) const;
    bool IsRogueSourceFile(const ANSICHAR* FilePath) const;
};
