#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/RogueFloorConfigData.h"  // URogueFloorConfigData の完全な型情報を取得
#include "DungeonConfigActor.generated.h"

UCLASS(Blueprintable)
class LYRAGAME_API ADungeonConfigActor : public AActor
{
    GENERATED_BODY()

public:
    ADungeonConfigActor();

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dungeon", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<URogueFloorConfigData> DungeonConfigAsset;
};
