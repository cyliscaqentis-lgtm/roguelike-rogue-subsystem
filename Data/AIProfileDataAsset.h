#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "GameplayAbilitySpec.h"
#include "AIProfileDataAsset.generated.h"

UCLASS(BlueprintType)
class LYRAGAME_API UAIProfileDataAsset : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    // 付与するAbilityのクラス
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Abilities")
    TArray<TSubclassOf<UGameplayAbility>> GrantedAbilities;

    // タグ（後方互換用）
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Abilities")
    FGameplayTagContainer GrantedAbilityTags;
};
