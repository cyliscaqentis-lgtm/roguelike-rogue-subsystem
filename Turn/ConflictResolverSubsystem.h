 
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TurnSystemTypes.h"
#include "StableActorRegistry.h"
#include "GameplayTagContainer.h"
#include "ConflictResolverSubsystem.generated.h"

class AGameTurnManagerBase;

/**
 * UConflictResolverSubsystem: —\–ñƒe[ƒuƒ‚ÆÕ“Ë‰ğŒˆiv2.2 ‘æ7ğE‘æ17ğj
 */
UCLASS()
class LYRAGAME_API UConflictResolverSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    /**
     * —\–ñƒe[ƒuƒ‚ğƒNƒŠƒAiƒ^[ƒ“ŠJnj
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Resolve")
    void ClearReservations();

    /**
     * —\–ñ‰•å‚ğ’Ç‰Á
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Resolve")
    void AddReservation(const FReservationEntry& Entry);

    /**
     * ‘S‚Ä‚ÌÕ“Ë‚ğ‰ğŒˆiv2.2 O’iƒoƒPƒbƒg + ƒTƒCƒNƒ–‰Âj
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Resolve")
    TArray<FResolvedAction> ResolveAllConflicts();

    /**
     * s“®ƒ^ƒO‚©‚çTier‚ğæ“¾iAttack=3, Dash=2, Move=1, Wait=0j
     */
    UFUNCTION(BlueprintPure, Category = "Turn|Resolve")
    int32 GetActionTier(const FGameplayTag& AbilityTag) const;

private:
    // —\–ñƒe[ƒuƒ: Key=(TimeSlot, Cell), Value=‰•åÒƒŠƒXƒg
    TMap<TPair<int32, FIntPoint>, TArray<FReservationEntry>> ReservationTable;

    // O’iƒoƒPƒbƒg‰ğŒˆiv2.2 ‘æ17ğj
    TArray<FResolvedAction> ResolveWithTripleBucket(const TArray<FReservationEntry>& Applicants);

    // ƒTƒCƒNƒŒŸoik†3‚ÌzŠÂ–‰Âj
    bool DetectAndAllowCycle(const TArray<FReservationEntry>& Applicants, TArray<FStableActorID>& OutCycle);

    // ƒtƒH[ƒƒoƒbƒNŒó•âiß–T1‰ñ‚Ì‚İj
    FResolvedAction TryFallbackMove(const FReservationEntry& LoserEntry);

    // Wait~Ši
    FResolvedAction CreateWaitAction(const FReservationEntry& Entry);
    mutable TWeakObjectPtr<AGameTurnManagerBase> CachedTurnManager;
    AGameTurnManagerBase* ResolveTurnManager() const;

};
