#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "RogueFloorConfigData.generated.h"

class UDungeonTemplateAsset;
struct FRandomStream;

USTRUCT(BlueprintType)
struct FIntRangeOverride
{
    GENERATED_BODY();

    UPROPERTY(EditAnywhere, Category="Override")
    bool bOverride = false;

    UPROPERTY(EditAnywhere, Category="Override", meta=(EditCondition="bOverride", ClampMin="0"))
    int32 Min = 0;

    UPROPERTY(EditAnywhere, Category="Override", meta=(EditCondition="bOverride", ClampMin="0"))
    int32 Max = 0;
};

USTRUCT(BlueprintType)
struct FFloatOverride
{
    GENERATED_BODY();

    UPROPERTY(EditAnywhere, Category="Override")
    bool bOverride = false;

    UPROPERTY(EditAnywhere, Category="Override", meta=(EditCondition="bOverride"))
    float Value = 0.f;
};

USTRUCT(BlueprintType)
struct FIntOverride
{
    GENERATED_BODY();

    UPROPERTY(EditAnywhere, Category="Override")
    bool bOverride = false;

    UPROPERTY(EditAnywhere, Category="Override", meta=(EditCondition="bOverride", ClampMin="0"))
    int32 Value = 0;
};

USTRUCT(BlueprintType)
struct FDungeonTemplateConfig
{
    GENERATED_BODY();

    UPROPERTY(EditAnywhere, Category="Template")
    TSubclassOf<UDungeonTemplateAsset> TemplateClass = nullptr;

    UPROPERTY(EditAnywhere, Category="Template", meta=(ClampMin="0.0"))
    float Weight = 1.0f;

    UPROPERTY(EditAnywhere, Category="Overrides") FIntRangeOverride RoomSize;
    UPROPERTY(EditAnywhere, Category="Overrides") FIntRangeOverride Rooms;
    UPROPERTY(EditAnywhere, Category="Overrides") FFloatOverride    StopSplitProbability;
    UPROPERTY(EditAnywhere, Category="Overrides") FFloatOverride    ExtraConnectorChance;
    UPROPERTY(EditAnywhere, Category="Overrides") FFloatOverride    ReachabilityThreshold;
    UPROPERTY(EditAnywhere, Category="Overrides") FIntOverride      MinDoorSpacing;
    UPROPERTY(EditAnywhere, Category="Overrides") FIntOverride      Width;
    UPROPERTY(EditAnywhere, Category="Overrides") FIntOverride      Height;
    UPROPERTY(EditAnywhere, Category="Overrides") FIntOverride      CellSizeUU;
    UPROPERTY(EditAnywhere, Category="Overrides") FIntOverride      RoomMargin;
    UPROPERTY(EditAnywhere, Category="Overrides") FIntOverride      OuterMargin;
    UPROPERTY(EditAnywhere, Category="Overrides") FIntOverride      MaxReroll;
};

USTRUCT(BlueprintType)
struct FDungeonResolvedParams
{
    GENERATED_BODY();

    int32 Width;
    int32 Height;

    int32 CellSizeUU = 100;
    int32 RoomMargin = 1;
    int32 OuterMargin = 1;
    int32 MaxReroll = 8;

    int32 MinRoomSize = 4;
    int32 MaxRoomSize = 10;
    int32 MinRooms    = 10;
    int32 MaxRooms    = 24;
    float StopSplitProbability = 0.15f;
    float ExtraConnectorChance = 0.10f;
    float ReachabilityThreshold = 0.9f;
    int32 MinDoorSpacing = 2;
};

USTRUCT(BlueprintType)
struct FDungeonTemplateWeight_Legacy
{
    GENERATED_BODY();

    UPROPERTY(EditAnywhere) TObjectPtr<UDungeonTemplateAsset> Template = nullptr;
    UPROPERTY(EditAnywhere, meta=(ClampMin="0.0")) float Weight = 1.0f;
};

UCLASS(BlueprintType)
class LYRAGAME_API URogueFloorConfigData : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Category="Common|Geometry") int32 Width = 64;
    UPROPERTY(EditAnywhere, Category="Common|Geometry") int32 Height = 64;
    UPROPERTY(EditAnywhere, Category="Common|Geometry") int32 CellSizeUU = 100;
    UPROPERTY(EditAnywhere, Category="Common|Rooms")   int32 MinRoomSize = 4;
    UPROPERTY(EditAnywhere, Category="Common|Rooms")   int32 MaxRoomSize = 10;
    UPROPERTY(EditAnywhere, Category="Common|Rooms")   int32 MinRooms = 10;
    UPROPERTY(EditAnywhere, Category="Common|Rooms")   int32 MaxRooms = 24;
    UPROPERTY(EditAnywhere, Category="Common|Margins") int32 RoomMargin = 1;
    UPROPERTY(EditAnywhere, Category="Common|Margins") int32 OuterMargin = 1;
    UPROPERTY(EditAnywhere, Category="Common|Algo")    float StopSplitProbability = 0.15f;
    UPROPERTY(EditAnywhere, Category="Common|Algo")    float ExtraConnectorChance  = 0.10f;
    UPROPERTY(EditAnywhere, Category="Common|Algo", meta=(ClampMin="0.0", ClampMax="1.0"))
                                                        float ReachabilityThreshold = 0.9f;
    UPROPERTY(EditAnywhere, Category="Common|Quality") int32 MaxReroll = 8;
    UPROPERTY(EditAnywhere, Category="Common|Doors")   int32 MinDoorSpacing = 2;

    UPROPERTY(EditAnywhere, Category="Templates")
    TArray<FDungeonTemplateConfig> TemplateConfigs;

    UPROPERTY(EditAnywhere, Category="Templates", meta=(DeprecatedProperty, DisplayName="Template Weights (Legacy)"))
    TArray<FDungeonTemplateWeight_Legacy> TemplateWeights_Legacy;

public:
    const FDungeonTemplateConfig* PickTemplateConfig(FRandomStream& Rng) const;

    FDungeonResolvedParams ResolveParamsFor(const FDungeonTemplateConfig& In) const;

    UFUNCTION(CallInEditor, Category="Templates")
    void MigrateFromLegacy();
};
