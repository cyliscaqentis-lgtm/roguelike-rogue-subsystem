#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AABB.generated.h"

class UBoxComponent;

UCLASS()
class LYRAGAME_API AAABB : public AActor
{
	GENERATED_BODY()
public:
	AAABB();

	/** 部屋境界 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<UBoxComponent> Box;

	/** UnitManager 側のヘルパが参照する半径（BoxExtent） */
	FORCEINLINE FVector GetBoxExtent() const;
};
