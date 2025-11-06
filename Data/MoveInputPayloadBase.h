#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "MoveInputPayloadBase.generated.h"

UCLASS(BlueprintType)
class LYRAGAME_API UMoveInputPayloadBase : public UObject
{
    GENERATED_BODY()

public:
    /** ★★★ 追加: ターンID ★★★ */
    UPROPERTY(BlueprintReadWrite, Category = "Move")
    int32 TurnId = INDEX_NONE;
    FVector Direction;

    UPROPERTY(BlueprintReadWrite, Category = "Move")
    FIntPoint TargetCell;
};
