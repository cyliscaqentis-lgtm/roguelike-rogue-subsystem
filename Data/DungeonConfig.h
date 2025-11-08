
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DungeonConfig.generated.h"

UCLASS(BlueprintType)
class LYRAGAME_API UDungeonConfig : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dungeon")
    int32 CellSize = 100;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dungeon")
    bool bUseFixedSeed = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dungeon", meta = (EditCondition = "bUseFixedSeed"))
    int32 FixedSeed = 12345;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dungeon")
    FIntPoint MapSize = FIntPoint(64, 64);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dungeon")
    FIntPoint RoomSizeMinMax = FIntPoint(4, 12);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dungeon")
    TSoftObjectPtr<UStaticMesh> DoorMesh;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dungeon")
    TSoftObjectPtr<UStaticMesh> StairMesh;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Team")
    int32 TeamSizePlayer = 1;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Team")
    int32 TeamSizeEnemy = 32;
};
