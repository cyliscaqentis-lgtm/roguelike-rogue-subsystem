#pragma once

#include "CoreMinimal.h"
#include "GA_AttackBase.h"
#include "GA_MeleeAttack.generated.h"

// CodeRevision: INC-2025-00030-R2 (Migrate to UGridPathfindingSubsystem) (2025-11-17 00:40)

// Forward declarations
class UGameplayEffect;
class AUnitBase;
class UGridPathfindingSubsystem;

UCLASS(Blueprintable)
class LYRAGAME_API UGA_MeleeAttack : public UGA_AttackBase
{
    GENERATED_BODY()

public:
    UGA_MeleeAttack(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

    // GameplayAbility overrides
    virtual void ActivateAbility(
        const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,
        const FGameplayEventData* TriggerEventData
    ) override;

    virtual void EndAbility(
        const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,
        bool bReplicateEndAbility,
        bool bWasCancelled
    ) override;

protected:
    // Config parameters

    /** GameplayEffect used for melee damage */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MeleeAttack")
    TSubclassOf<UGameplayEffect> MeleeAttackEffect;

    /** Animation montage for melee attack */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MeleeAttack|Animation")
    TObjectPtr<UAnimMontage> MeleeAttackMontage;

    /** Base damage amount (SetByCaller can override) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MeleeAttack")
    float Damage = 28.0f;

    // Runtime state

    /** Current attack target */
    UPROPERTY(BlueprintReadOnly, Category = "MeleeAttack|State")
    TObjectPtr<AActor> TargetUnit;

    // Internal C++ logic

    /** Finds an adjacent enemy unit, if any */
    AActor* FindAdjacentTarget();

    /** Applies GameplayEffect-based damage to the target */
    void ApplyDamageToTarget(AActor* Target);

    /** Montage callbacks */
    UFUNCTION()
    void OnMontageCompleted();

    UFUNCTION()
    void OnMontageBlendOut();

    UFUNCTION()
    void OnMontageCancelled();

    // Blueprint extension points

    /**
     * Hook for project-specific attack montage handling.
     * Default implementation uses PlayMontageAndWait in C++.
     */
    UFUNCTION(BlueprintNativeEvent, Category = "MeleeAttack|Animation")
    void PlayAttackMontage();
    virtual void PlayAttackMontage_Implementation();

private:
    /** Montage delegate handles */
    FDelegateHandle MontageCompletedHandle;
    FDelegateHandle MontageBlendOutHandle;
    FDelegateHandle MontageCancelledHandle;

    /** Whether input is disabled during the attack */
    bool bInputDisabled = false;
    
    /** Cached TurnManager reference */
    UPROPERTY(Transient)
    mutable TWeakObjectPtr<class AGameTurnManagerBase> CachedTurnManager;
    
    /** Resolves the TurnManager instance */
    class AGameTurnManagerBase* GetTurnManager() const;

    /** Cached target position and grid cell */
    FVector CachedTargetLocation = FVector::ZeroVector;
    FIntPoint CachedTargetCell = FIntPoint(-1, -1);
    bool bHasCachedTargetCell = false;

    /**
     * Updates cached world location and grid cell for the current target.
     * CodeRevision: INC-2025-00030-R2 (Migrate to UGridPathfindingSubsystem)
     */
    void UpdateCachedTargetLocation(
        const FVector& Location,
        const FIntPoint& ReservedCell,
        const UGridPathfindingSubsystem* GridLib);
};
