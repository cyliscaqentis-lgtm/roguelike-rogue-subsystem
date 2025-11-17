// TBSAttributeSet_Combat.cpp
// Location: Rogue/Character/TBSAttributeSet_Combat.cpp

#include "Character/TBSAttributeSet_Combat.h"
#include "Net/UnrealNetwork.h"
#include "GameplayEffectExtension.h"

// CodeRevision: INC-2025-00034-R1 (Add PreAttributeChange and PostGameplayEffectExecute to TBSAttributeSet_Combat) (2025-01-XX XX:XX)

// Constructor: Set default initial values for each attribute
UTBSAttributeSet_Combat::UTBSAttributeSet_Combat()
{
    // ===== HP =====
    Health = 10.0f;
    MaxHealth = 10.0f;

    // ===== 攻撃・防御 =====
    Attack = 5.0f;
    Defense = 0.0f;

    // ===== 行動順・命中回避 =====
    Speed = 10.0f;
    Accuracy = 1.0f;
    Evasion = 0.0f;

    // ===== 会心 =====
    CritChance = 0.05f;
    CritMultiplier = 1.5f;

    // ===== ダメージ補正 =====
    DamageDealtRate  = 1.0f;
    DamageTakenRate  = 1.0f;
}

// Replication setup
void UTBSAttributeSet_Combat::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME_CONDITION_NOTIFY(
        UTBSAttributeSet_Combat, Health, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(
        UTBSAttributeSet_Combat, MaxHealth, COND_None, REPNOTIFY_Always);

    DOREPLIFETIME_CONDITION_NOTIFY(
        UTBSAttributeSet_Combat, Attack, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(
        UTBSAttributeSet_Combat, Defense, COND_None, REPNOTIFY_Always);

    DOREPLIFETIME_CONDITION_NOTIFY(
        UTBSAttributeSet_Combat, Speed, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(
        UTBSAttributeSet_Combat, Accuracy, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(
        UTBSAttributeSet_Combat, Evasion, COND_None, REPNOTIFY_Always);

    DOREPLIFETIME_CONDITION_NOTIFY(
        UTBSAttributeSet_Combat, CritChance, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(
        UTBSAttributeSet_Combat, CritMultiplier, COND_None, REPNOTIFY_Always);

    DOREPLIFETIME_CONDITION_NOTIFY(
        UTBSAttributeSet_Combat, DamageDealtRate, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(
        UTBSAttributeSet_Combat, DamageTakenRate, COND_None, REPNOTIFY_Always);
}

// ===== OnRep functions =====

void UTBSAttributeSet_Combat::OnRep_Health(
    const FGameplayAttributeData& OldValue)
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UTBSAttributeSet_Combat, Health, OldValue);
}

void UTBSAttributeSet_Combat::OnRep_MaxHealth(
    const FGameplayAttributeData& OldValue)
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UTBSAttributeSet_Combat, MaxHealth, OldValue);
}

void UTBSAttributeSet_Combat::OnRep_Attack(
    const FGameplayAttributeData& OldValue)
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UTBSAttributeSet_Combat, Attack, OldValue);
}

void UTBSAttributeSet_Combat::OnRep_Defense(
    const FGameplayAttributeData& OldValue)
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UTBSAttributeSet_Combat, Defense, OldValue);
}

void UTBSAttributeSet_Combat::OnRep_Speed(
    const FGameplayAttributeData& OldValue)
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UTBSAttributeSet_Combat, Speed, OldValue);
}

void UTBSAttributeSet_Combat::OnRep_Accuracy(
    const FGameplayAttributeData& OldValue)
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UTBSAttributeSet_Combat, Accuracy, OldValue);
}

void UTBSAttributeSet_Combat::OnRep_Evasion(
    const FGameplayAttributeData& OldValue)
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UTBSAttributeSet_Combat, Evasion, OldValue);
}

void UTBSAttributeSet_Combat::OnRep_CritChance(
    const FGameplayAttributeData& OldValue)
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UTBSAttributeSet_Combat, CritChance, OldValue);
}

void UTBSAttributeSet_Combat::OnRep_CritMultiplier(
    const FGameplayAttributeData& OldValue)
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UTBSAttributeSet_Combat, CritMultiplier, OldValue);
}

void UTBSAttributeSet_Combat::OnRep_DamageDealtRate(
    const FGameplayAttributeData& OldValue)
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UTBSAttributeSet_Combat, DamageDealtRate, OldValue);
}

void UTBSAttributeSet_Combat::OnRep_DamageTakenRate(
    const FGameplayAttributeData& OldValue)
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UTBSAttributeSet_Combat, DamageTakenRate, OldValue);
}

// ===== Clamping and death detection =====

// CodeRevision: INC-2025-00034-R1 (Add PreAttributeChange and PostGameplayEffectExecute to TBSAttributeSet_Combat) (2025-01-XX XX:XX)
void UTBSAttributeSet_Combat::PreAttributeChange(
    const FGameplayAttribute& Attribute, float& NewValue)
{
    Super::PreAttributeChange(Attribute, NewValue);

    // Clamp Health attribute between 0 and MaxHealth
    if (Attribute == GetHealthAttribute())
    {
        NewValue = FMath::Clamp(NewValue, 0.0f, GetMaxHealth());
    }
    // TODO: Add clamping for other attributes with max limits (e.g., MaxSpeed, MaxCritChance)
}

// CodeRevision: INC-2025-00034-R1 (Add PreAttributeChange and PostGameplayEffectExecute to TBSAttributeSet_Combat) (2025-01-XX XX:XX)
void UTBSAttributeSet_Combat::PostGameplayEffectExecute(
    const FGameplayEffectModCallbackData& Data)
{
    Super::PostGameplayEffectExecute(Data);

    // Check Health attribute after modification
    if (Data.EvaluatedData.Attribute == GetHealthAttribute())
    {
        float CurrentHealth = GetHealth();

        if (CurrentHealth <= 0.0f)
        {
            // Death handling
            // TODO: Add death tag or broadcast death event here
            
            // Example: Add death tag
            // UAbilitySystemComponent* ASC = GetOwningAbilitySystemComponent();
            // if (ASC)
            // {
            //    ASC->AddLooseGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("State.Dead")));
            // }

            // Example: Broadcast death event via delegate
            // OnDeath.Broadcast(OwningActor);
        }
    }
}