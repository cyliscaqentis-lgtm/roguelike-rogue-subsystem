#pragma once
#include "UObject/Object.h"
#include "DungeonTemplateAsset.generated.h"

class ADungeonFloorGenerator;
struct FDungeonResolvedParams;
struct FRandomStream;

UCLASS(Abstract, BlueprintType)
class LYRAGAME_API UDungeonTemplateAsset : public UObject
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintNativeEvent, Category="Dungeon|Template")
    void Generate(ADungeonFloorGenerator* Generator, const FDungeonResolvedParams& Params, FRandomStream& Rng);
    virtual void Generate_Implementation(ADungeonFloorGenerator* Generator, const FDungeonResolvedParams& Params, FRandomStream& Rng) {}
};