#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "DungeonTemplateTypes.generated.h"

UENUM(BlueprintType)
enum class EMapTemplate : uint8
{
    NormalBSP,
    LargeHall,
    FourQuads,
    CentralCrossWithMiniRooms,
};

USTRUCT(BlueprintType)
struct FMapTemplateWeight
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Template") EMapTemplate Template = EMapTemplate::NormalBSP;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Template") float Weight = 1.0f;
};