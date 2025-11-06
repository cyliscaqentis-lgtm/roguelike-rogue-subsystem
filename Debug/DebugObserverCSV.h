// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Debug/DebugObserverInterface.h"
#include "DebugObserverCSV.generated.h"

/**
 * Simple CSV logger for turn-system debug information.
 * Can be swapped out in Blueprints by implementing IDebugObserver elsewhere.
 */
UCLASS(ClassGroup = (Turn), meta = (BlueprintSpawnableComponent))
class LYRAGAME_API UDebugObserverCSV : public UActorComponent, public IDebugObserver
{
    GENERATED_BODY()

public:
    UDebugObserverCSV();

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

private:
    UPROPERTY()
    TArray<FString> LogLines;
};
