// TBSAttributeSet_Combat.h
// Location: Rogue/Character/TBSAttributeSet_Combat.h

#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "TBSAttributeSet_Combat.generated.h"

// CodeRevision: INC-2025-00034-R1 (Add PreAttributeChange and PostGameplayEffectExecute to TBSAttributeSet_Combat) (2025-01-XX XX:XX)

// マクロのショートカット（Lyra と同じノリ）
#define ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \
    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
    GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
    GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
    GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

/**
 * Combat attribute set for TBS units (player/enemy)
 * Provides common combat attributes: HP, Attack, Defense, Speed, Accuracy, Evasion, Crit, Damage modifiers
 * Includes PreAttributeChange for clamping and PostGameplayEffectExecute for death detection
 */
UCLASS()
class LYRAGAME_API UTBSAttributeSet_Combat : public UAttributeSet
{
    GENERATED_BODY()

public:
    UTBSAttributeSet_Combat();

    // ===== HP =====
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Health, Category = "Combat")
    FGameplayAttributeData Health;
    ATTRIBUTE_ACCESSORS(UTBSAttributeSet_Combat, Health);

    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MaxHealth, Category = "Combat")
    FGameplayAttributeData MaxHealth;
    ATTRIBUTE_ACCESSORS(UTBSAttributeSet_Combat, MaxHealth);

    // ===== 攻撃・防御 =====
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Attack, Category = "Combat")
    FGameplayAttributeData Attack;
    ATTRIBUTE_ACCESSORS(UTBSAttributeSet_Combat, Attack);

    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Defense, Category = "Combat")
    FGameplayAttributeData Defense;
    ATTRIBUTE_ACCESSORS(UTBSAttributeSet_Combat, Defense);

    // ===== 行動順・命中回避 =====
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Speed, Category = "Combat")
    FGameplayAttributeData Speed;          // イニシアチブ／行動順
    ATTRIBUTE_ACCESSORS(UTBSAttributeSet_Combat, Speed);

    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Accuracy, Category = "Combat")
    FGameplayAttributeData Accuracy;       // 命中率（0〜1）
    ATTRIBUTE_ACCESSORS(UTBSAttributeSet_Combat, Accuracy);

    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Evasion, Category = "Combat")
    FGameplayAttributeData Evasion;        // 回避率（0〜1）
    ATTRIBUTE_ACCESSORS(UTBSAttributeSet_Combat, Evasion);

    // ===== 会心 =====
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_CritChance, Category = "Combat")
    FGameplayAttributeData CritChance;     // 会心率（0〜1）
    ATTRIBUTE_ACCESSORS(UTBSAttributeSet_Combat, CritChance);

    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_CritMultiplier, Category = "Combat")
    FGameplayAttributeData CritMultiplier; // 会心倍率（例: 1.5, 2.0）
    ATTRIBUTE_ACCESSORS(UTBSAttributeSet_Combat, CritMultiplier);

    // ===== ダメージ補正 =====
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_DamageDealtRate, Category = "Combat")
    FGameplayAttributeData DamageDealtRate;  // 与ダメ倍率（1.0=等倍）
    ATTRIBUTE_ACCESSORS(UTBSAttributeSet_Combat, DamageDealtRate);

    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_DamageTakenRate, Category = "Combat")
    FGameplayAttributeData DamageTakenRate;  // 被ダメ倍率（1.0=等倍）
    ATTRIBUTE_ACCESSORS(UTBSAttributeSet_Combat, DamageTakenRate);

    // ここに FireResist / IceResist などを将来追加してOK

    // ===== UAttributeSet 基本オーバーライド =====
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    // 属性が変更される直前に呼ばれる（クランプ処理に使う）
    virtual void PreAttributeChange(
        const FGameplayAttribute& Attribute, 
        float& NewValue) override;

    // GEが実行された直後に呼ばれる（死亡判定などに使う）
    virtual void PostGameplayEffectExecute(
        const FGameplayEffectModCallbackData& Data) override;

protected:

    // ===== OnRep 関数群 =====
    UFUNCTION()
    void OnRep_Health(const FGameplayAttributeData& OldValue);

    UFUNCTION()
    void OnRep_MaxHealth(const FGameplayAttributeData& OldValue);

    UFUNCTION()
    void OnRep_Attack(const FGameplayAttributeData& OldValue);

    UFUNCTION()
    void OnRep_Defense(const FGameplayAttributeData& OldValue);

    UFUNCTION()
    void OnRep_Speed(const FGameplayAttributeData& OldValue);

    UFUNCTION()
    void OnRep_Accuracy(const FGameplayAttributeData& OldValue);

    UFUNCTION()
    void OnRep_Evasion(const FGameplayAttributeData& OldValue);

    UFUNCTION()
    void OnRep_CritChance(const FGameplayAttributeData& OldValue);

    UFUNCTION()
    void OnRep_CritMultiplier(const FGameplayAttributeData& OldValue);

    UFUNCTION()
    void OnRep_DamageDealtRate(const FGameplayAttributeData& OldValue);

    UFUNCTION()
    void OnRep_DamageTakenRate(const FGameplayAttributeData& OldValue);
};