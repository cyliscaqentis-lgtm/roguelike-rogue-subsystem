#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DungeonFloorGenerator.h"
#include "DungeonConfigAsset.generated.h"

UCLASS(BlueprintType)
class LYRAGAME_API UDungeonConfigAsset : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Generation|Grid") FDungeonGenParams GenerationParams;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Generation|Templates") TArray<FMapTemplateWeight> TemplateWeights;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Generation|Seed") bool bUseDynamicSeed = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Generation|Seed") int32 BaseSeed = 12345;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Metadata") FString FloorName = TEXT("Dungeon Floor");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Metadata") int32 FloorLevel = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Metadata") FString Description = TEXT("");

    UFUNCTION(BlueprintCallable, Category="Config")
    void SetTemplateWeights(const TArray<FMapTemplateWeight>& NewWeights)
    {
        TemplateWeights = NewWeights;
    }

    UFUNCTION(BlueprintCallable, Category="Config")
    void SetGenerationParams(const FDungeonGenParams& NewParams)
    {
        GenerationParams = NewParams;
    }

    UFUNCTION(BlueprintCallable, Category="Config")
    int32 GetEffectiveSeed() const
    {
        if (bUseDynamicSeed)
        {
            const int32 RandPart = static_cast<int32>(FDateTime::Now().GetTicks() & 0x7fffffff);
            return RandPart ^ BaseSeed;
        }
        return BaseSeed;
    }

    virtual void PostLoad() override
    {
        Super::PostLoad();
        
        if (TemplateWeights.Num() == 0)
        {
            TemplateWeights.Add({EMapTemplate::NormalBSP, 3.0f});
            TemplateWeights.Add({EMapTemplate::LargeHall, 1.0f});
            TemplateWeights.Add({EMapTemplate::FourQuads, 1.0f});
            TemplateWeights.Add({EMapTemplate::CentralCrossWithMiniRooms, 2.0f});
        }
    }
};
