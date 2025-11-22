#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TurnSystemTypes.h"
#include "TurnCorePhaseManager.generated.h"

// Log category
DECLARE_LOG_CATEGORY_EXTERN(LogTurnCore, Log, All);

// Forward declarations
class UDistanceFieldSubsystem;
class UConflictResolverSubsystem;
class UStableActorRegistry;
class UAbilitySystemComponent;
class AGameTurnManagerBase;

/**
 * Core phase orchestrator for the turn-based system.
 *
 * Provides:
 * - Observation → Intent Generation → Resolution → Execution → Cleanup
 * - TimeSlot-based fast execution pipeline
 * - Intent bucketization and per-slot evaluation
 * - ASC resolution helpers
 */
UCLASS()
class LYRAGAME_API UTurnCorePhaseManager : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // ========================================================================
    // Core Phase Pipeline
    // ========================================================================

    UFUNCTION(BlueprintCallable, Category = "Turn|Core")
    void CoreObservationPhase(const FIntPoint& PlayerCell);

    UFUNCTION(BlueprintCallable, Category = "Turn|Core")
    TArray<FEnemyIntent> CoreThinkPhase(const TArray<AActor*>& Enemies);

    UFUNCTION(BlueprintCallable, Category = "Turn|Core")
    TArray<FResolvedAction> CoreResolvePhase(const TArray<FEnemyIntent>& Intents);

    /**
     * Phase 4: Execute Phase
     * Delegated entirely to Blueprint. C++ performs no execution.
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Core")
    void CoreExecutePhase(const TArray<FResolvedAction>& ResolvedActions);

    UFUNCTION(BlueprintCallable, Category = "Turn|Core")
    void CoreCleanupPhase();

    // ========================================================================
    // TimeSlot-Based Execution Pipeline
    // ========================================================================

    /**
     * Execute movement phase grouped by TimeSlot.
     *
     * Flow:
     * 1. Bucketize intents by TimeSlot.
     * 2. Evaluate from slot 0 → MaxSlot.
     * 3. Resolve → Execute per slot.
     * 4. Occupancy updated only during Execute to ensure consistency.
     *
     * @param AllIntents Mixed Intent list (TimeSlot values may vary)
     * @param OutActions Accumulated resolved actions
     * @return Max TimeSlot detected
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Core")
    int32 ExecuteMovePhaseWithSlots(
        const TArray<FEnemyIntent>& AllIntents,
        TArray<FResolvedAction>& OutActions);

    /**
     * Execute attack phase grouped by TimeSlot.
     *
     * Flow:
     * 1. Bucketize intents.
     * 2. Filter only intents with the attack tag.
     * 3. Send gameplay attack events to the actors.
     * 4. No delay or recursion needed; ASC handles it.
     *
     * @param AllIntents Mixed Intent list
     * @param OutActions Accumulated resolved actions
     * @return Max TimeSlot detected
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Core")
    int32 ExecuteAttackPhaseWithSlots(
        const TArray<FEnemyIntent>& AllIntents,
        TArray<FResolvedAction>& OutActions);

    /**
     * Generate intents with TimeSlot expansion.
     *
     * Flow:
     * 1. Generate base intents (TimeSlot=0) via CoreThinkPhase.
     * 2. For actors with the "Fast" tag, add additional TimeSlot=1 intent.
     * 3. Return combined list.
     *
     * @param Enemies Enemy actors
     * @param OutIntents Output array of generated intents with TimeSlot assigned
     */
    UFUNCTION(BlueprintCallable, Category = "Turn|Core")
    void CoreThinkPhaseWithTimeSlots(
        const TArray<AActor*>& Enemies,
        TArray<FEnemyIntent>& OutIntents);

    // ========================================================================
    // Helper Functions
    // ========================================================================

    /**
     * Resolve an AbilitySystemComponent from Actor following Lyra rules:
     * Actor → Pawn → Controller → PlayerState resolution chain.
     */
    UFUNCTION(BlueprintPure, Category = "Turn|Core")
    static UAbilitySystemComponent* ResolveASC(AActor* Actor);

    /**
     * Ensure a Pawn-owned ASC exists.
     * If missing, create a new ASC directly on the Pawn and initialize it.
     *
     * Intended for:
     * - AI units requiring an ASC without PlayerState
     * - Pawns that do not use the PlayerState-driven ASC in Lyra
     */
    UFUNCTION(BlueprintCallable, Category = "Turn System")
    static bool IsGASReady(AActor* Actor);

	UFUNCTION()
	bool ShouldEnsureIntents();

	UFUNCTION()
	void EnsureEnemyIntents(int32 TurnId, APawn* PlayerPawn);

    UFUNCTION(BlueprintCallable, Category = "Turn System")
    bool AllEnemiesReady(const TArray<AActor*>& Enemies) const;

    /**
     * Execute the move phase with conflict resolution
     * @param TurnId Current turn ID
     * @param PlayerPawn Player's pawn
     * @param OutCancelledPlayer True if player move was cancelled
     * @return Resolved actions to dispatch
     */
    TArray<FResolvedAction> ExecuteMovePhaseWithResolution(
        int32 TurnId,
        APawn* PlayerPawn,
        bool& OutCancelledPlayer);


private:
    // ========================================================================
    // Subsystem Dependencies
    // ========================================================================

    UPROPERTY()
    TObjectPtr<UDistanceFieldSubsystem> DistanceField = nullptr;

    UPROPERTY()
    TObjectPtr<UConflictResolverSubsystem> ConflictResolver = nullptr;

    UPROPERTY()
    TObjectPtr<UStableActorRegistry> ActorRegistry = nullptr;

    // ========================================================================
    // Helper Methods
    // ========================================================================

    const FGameplayTag& Tag_Move();
    const FGameplayTag& Tag_Attack();
    const FGameplayTag& Tag_Wait();

    void BucketizeIntentsBySlot(
        const TArray<FEnemyIntent>& All,
        TArray<TArray<FEnemyIntent>>& OutBuckets,
        int32& OutMaxSlot);

    TWeakObjectPtr<AGameTurnManagerBase> CachedTurnManager;
    AGameTurnManagerBase* ResolveTurnManager();
};
