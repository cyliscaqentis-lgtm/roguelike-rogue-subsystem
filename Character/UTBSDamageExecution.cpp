// UTBSDamageExecution.cpp
// Location: Rogue/Character/UTBSDamageExecution.cpp

#include "Character/UTBSDamageExecution.h"
#include "Character/TBSAttributeSet_Combat.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffectTypes.h"

// CodeRevision: INC-2025-00035-R1 (Add UTBSDamageExecution for custom damage calculation) (2025-01-XX XX:XX)

// FTBSDamageStatics constructor body (usually empty since defined in header)
// (If DEFINE_ATTRIBUTE_CAPTUREDEF is not in header, define here)
// FTBSDamageStatics::FTBSDamageStatics()
// {
// }

// CodeRevision: INC-2025-00035-R1 (Add UTBSDamageExecution for custom damage calculation) (2025-01-XX XX:XX)
UTBSDamageExecution::UTBSDamageExecution()
{
    // Register attributes that this Execution references
    RelevantAttributesToCapture.Add(GetDamageStatics().AttackDef);
    RelevantAttributesToCapture.Add(GetDamageStatics().DefenseDef);
    RelevantAttributesToCapture.Add(GetDamageStatics().CritChanceDef);
    RelevantAttributesToCapture.Add(GetDamageStatics().CritMultiplierDef);
    RelevantAttributesToCapture.Add(GetDamageStatics().DamageDealtRateDef);
    RelevantAttributesToCapture.Add(GetDamageStatics().DamageTakenRateDef);
}

// CodeRevision: INC-2025-00035-R1 (Add UTBSDamageExecution for custom damage calculation) (2025-01-XX XX:XX)
void UTBSDamageExecution::Execute_Implementation(
    const FGameplayEffectCustomExecutionParameters& ExecutionParams,
    FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const
{
    // --- 1. Get required parameters ---
    const FGameplayEffectSpec& Spec = ExecutionParams.GetOwningSpec();
    const FGameplayTagContainer* SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
    const FGameplayTagContainer* TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();

    FAggregatorEvaluateParameters EvalParams;
    EvalParams.SourceTags = SourceTags;
    EvalParams.TargetTags = TargetTags;

    // --- 2. Capture (read) attributes ---
    float Attack = 0.0f;
    ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
        GetDamageStatics().AttackDef, EvalParams, Attack);

    float Defense = 0.0f;
    ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
        GetDamageStatics().DefenseDef, EvalParams, Defense);

    float CritChance = 0.0f;
    ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
        GetDamageStatics().CritChanceDef, EvalParams, CritChance);

    float CritMultiplier = 1.0f; // Crit multiplier (default 1.0 = no crit)
    ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
        GetDamageStatics().CritMultiplierDef, EvalParams, CritMultiplier);
    
    float DamageDealtRate = 1.0f; // Damage dealt multiplier (default 1.0)
    ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
        GetDamageStatics().DamageDealtRateDef, EvalParams, DamageDealtRate);

    float DamageTakenRate = 1.0f; // Damage taken multiplier (default 1.0)
    ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
        GetDamageStatics().DamageTakenRateDef, EvalParams, DamageTakenRate);

    // --- 3. Get "skill power" from SetByCaller ---
    // Allows ability to specify skill power like 1.2 (120%)
    // Common to use "SetByCaller.Damage" tag
    const float SkillPower = Spec.GetSetByCallerMagnitude(
        FGameplayTag::RequestGameplayTag(TEXT("SetByCaller.Damage")),
        false, // Whether to log error if not found
        1.0f   // Default value if not found (1.0 = 100%)
    );

    // --- 4. Damage calculation formula (logic can be freely modified) ---

    // (A) Base damage = Attack * Skill power
    float BaseDamage = Attack * SkillPower;

    // (B) Apply damage dealt/taken multipliers
    BaseDamage *= DamageDealtRate;
    BaseDamage *= DamageTakenRate;

    // (C) Apply defense (guarantee at least 1 damage)
    float DamageAfterDefense = FMath::Max(1.0f, BaseDamage - Defense);

    // (D) Critical hit check
    bool bIsCritical = (FMath::FRand() < CritChance);
    float FinalDamage = DamageAfterDefense;

    if (bIsCritical)
    {
        FinalDamage *= CritMultiplier;
        // TODO: Trigger GameplayCue for critical hit here
        // FGameplayCueManager::ExecuteGameplayCue(......)
    }

    // --- 5. Apply calculated result to Health ---
    if (FinalDamage > 0.0f)
    {
        // Apply -FinalDamage additively to Health attribute
        // (= subtract FinalDamage from Health)
        OutExecutionOutput.AddOutputModifier(
            FGameplayModifierEvaluatedData(
                UTBSAttributeSet_Combat::GetHealthAttribute(), // Modify Health attribute
                EGameplayModOp::Additive,                      // Additive
                -FinalDamage                                   // Negative value
            )
        );
    }
}