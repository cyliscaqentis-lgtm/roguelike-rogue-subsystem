// UTBSDamageExecution.h
// Location: Rogue/Character/UTBSDamageExecution.h

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectExecutionCalculation.h"
#include "Character/TBSAttributeSet_Combat.h"
#include "UTBSDamageExecution.generated.h"

// CodeRevision: INC-2025-00035-R1 (Add UTBSDamageExecution for custom damage calculation) (2025-01-XX XX:XX)

/**
 * Internal structure for capturing (reading) attributes
 * Similar to Lyra's FDamageStatics pattern
 */
struct FTBSDamageStatics
{
    // どの属性を
    DECLARE_ATTRIBUTE_CAPTUREDEF(Attack);
    DECLARE_ATTRIBUTE_CAPTUREDEF(Defense);
    DECLARE_ATTRIBUTE_CAPTUREDEF(CritChance);
    DECLARE_ATTRIBUTE_CAPTUREDEF(CritMultiplier);
    DECLARE_ATTRIBUTE_CAPTUREDEF(DamageDealtRate);
    DECLARE_ATTRIBUTE_CAPTUREDEF(DamageTakenRate);

    FTBSDamageStatics()
    {
        // Define where to read from (Source=attacker, Target=defender)
        // (Attribute class, Attribute name, Source/Target, Snapshot flag)

        // Attacker attributes: Attack, CritChance, CritMultiplier, DamageDealtRate
        DEFINE_ATTRIBUTE_CAPTUREDEF(
            UTBSAttributeSet_Combat, Attack, Source, true);
        DEFINE_ATTRIBUTE_CAPTUREDEF(
            UTBSAttributeSet_Combat, CritChance, Source, true);
        DEFINE_ATTRIBUTE_CAPTUREDEF(
            UTBSAttributeSet_Combat, CritMultiplier, Source, true);
        DEFINE_ATTRIBUTE_CAPTUREDEF(
            UTBSAttributeSet_Combat, DamageDealtRate, Source, true);

        // Defender attributes: Defense, DamageTakenRate
        DEFINE_ATTRIBUTE_CAPTUREDEF(
            UTBSAttributeSet_Combat, Defense, Target, false);
        DEFINE_ATTRIBUTE_CAPTUREDEF(
            UTBSAttributeSet_Combat, DamageTakenRate, Target, false);
    }
};

/**
 * Custom damage calculation class for TBS
 * Calculates damage using Attack, Defense, Crit, and damage modifiers from TBSAttributeSet_Combat
 */
UCLASS()
class LYRAGAME_API UTBSDamageExecution : public UGameplayEffectExecutionCalculation
{
    GENERATED_BODY()

public:
    UTBSDamageExecution();

    /** Main function called during execution */
    virtual void Execute_Implementation(
        const FGameplayEffectCustomExecutionParameters& ExecutionParams,
        FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const override;

    /** Helper function to get attribute definitions */
    static const FTBSDamageStatics& GetDamageStatics()
    {
        static FTBSDamageStatics DmgStatics;
        return DmgStatics;
    }
};