// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Turn/TurnSystemTypes.h"
#include "UObject/Interface.h"
#include "DebugObserverInterface.generated.h"

UINTERFACE(BlueprintType)
class LYRAGAME_API UDebugObserver : public UInterface
{
    GENERATED_BODY()
};

class LYRAGAME_API IDebugObserver
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Turn|Debug")
    void OnPhaseStarted(FGameplayTag PhaseTag, const TArray<AActor*>& Actors);

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Turn|Debug")
    void OnPhaseCompleted(FGameplayTag PhaseTag, float DurationSeconds);

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Turn|Debug")
    void OnIntentGenerated(AActor* Enemy, const FEnemyIntent& Intent);
};
