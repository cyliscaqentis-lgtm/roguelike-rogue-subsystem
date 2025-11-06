#pragma once

#include "EDebugDrawMode.generated.h"

UENUM(BlueprintType)
enum class EDebugDrawMode : uint8
{
    None,
    GridLines,
    RoomCenters,
    CorridorPaths,
    All,
};

