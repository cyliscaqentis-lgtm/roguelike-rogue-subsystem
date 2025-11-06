#pragma once

#include "Rogue/Data/DungeonTemplateAsset.h"
#include "DungeonPresetTemplates.generated.h"

UCLASS(Blueprintable, meta=(DisplayName="Preset: Normal BSP"))
class LYRAGAME_API UDungeonTemplate_NormalBSP : public UDungeonTemplateAsset
{
    GENERATED_BODY()
public:
    virtual void Generate_Implementation(ADungeonFloorGenerator* Generator, const FDungeonResolvedParams& Params, FRandomStream& Rng) override;
};

UCLASS(Blueprintable, meta=(DisplayName="Preset: Large Hall"))
class LYRAGAME_API UDungeonTemplate_LargeHall : public UDungeonTemplateAsset
{
    GENERATED_BODY()
public:
    virtual void Generate_Implementation(ADungeonFloorGenerator* Generator, const FDungeonResolvedParams& Params, FRandomStream& Rng) override;
};

UCLASS(Blueprintable, meta=(DisplayName="Preset: Four Quads"))
class LYRAGAME_API UDungeonTemplate_FourQuads : public UDungeonTemplateAsset
{
    GENERATED_BODY()
public:
    virtual void Generate_Implementation(ADungeonFloorGenerator* Generator, const FDungeonResolvedParams& Params, FRandomStream& Rng) override;
};

UCLASS(Blueprintable, meta=(DisplayName="Preset: Central Cross"))
class LYRAGAME_API UDungeonTemplate_CentralCross : public UDungeonTemplateAsset
{
    GENERATED_BODY()
public:
    virtual void Generate_Implementation(ADungeonFloorGenerator* Generator, const FDungeonResolvedParams& Params, FRandomStream& Rng) override;
};