#pragma once
#include "UObject/Object.h"
#include "Engine/DataAsset.h"
#include "DungeonTemplateAsset.generated.h"

class ADungeonFloorGenerator;
struct FDungeonResolvedParams;
struct FRandomStream;

UCLASS(Abstract, BlueprintType, EditInlineNew, DefaultToInstanced)
class LYRAGAME_API UDungeonTemplateAsset : public UDataAsset
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintNativeEvent, Category="Dungeon|Template")
    void Generate(ADungeonFloorGenerator* Generator, const FDungeonResolvedParams& Params, FRandomStream& Rng);
    virtual void Generate_Implementation(ADungeonFloorGenerator* Generator, const FDungeonResolvedParams& Params, FRandomStream& Rng) {}
};