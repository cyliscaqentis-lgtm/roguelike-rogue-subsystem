// TurnCommandEncoding.h
// CodeRevision: INC-2025-1135-R1 (Fix encoding issues: translate Japanese comments to English) (2025-11-19 17:25)
#pragma once
#include "CoreMinimal.h"

namespace TurnCommandEncoding
{
    // Dir: (-1,0,+1)
    constexpr int32 kDirBase = 1000;
    FORCEINLINE int32 PackDir(int32 dx, int32 dy)
    {
        dx = FMath::Clamp(dx, -1, 1);
        dy = FMath::Clamp(dy, -1, 1);
        return kDirBase + (dx + 1) * 100 + (dy + 1);
    }
    FORCEINLINE bool UnpackDir(int32 mag, int32& outDx, int32& outDy)
    {
        const int32 v = mag - kDirBase;
        if (v < 0 || v > 9999) return false;
        outDx = FMath::Clamp(v / 100 - 1, -1, 1);
        outDy = FMath::Clamp(v % 100 - 1, -1, 1);
        return !(outDx == 0 && outDy == 0); // Zero direction is invalid
    }

    // Cell: Grid coordinates (supports -1024..+1023)
    constexpr int32 kCellBase = 2000000;
    constexpr int32 kCellBias = 1024;
    constexpr int32 kCellStride = 2048;
    FORCEINLINE int32 PackCell(int32 gx, int32 gy)
    {
        return kCellBase + (gx + kCellBias) * kCellStride + (gy + kCellBias);
    }
    FORCEINLINE bool UnpackCell(int32 mag, int32& outGX, int32& outGY)
    {
        if (mag < kCellBase) return false;
        const int32 v = mag - kCellBase;
        outGX = v / kCellStride - kCellBias;
        outGY = v % kCellStride - kCellBias;
        return true;
    }
}
