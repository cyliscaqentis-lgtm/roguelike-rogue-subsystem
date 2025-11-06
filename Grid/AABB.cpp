#include "AABB.h"
#include "Components/BoxComponent.h"

AAABB::AAABB()
{
	PrimaryActorTick.bCanEverTick = false;

	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	RootComponent = Box;

	// デフォルトで平面マップを想定（Zは0）
	Box->SetBoxExtent(FVector(400.f, 400.f, 10.f)); // 8x8 タイル相当（1タイル=100cm）
}

FVector AAABB::GetBoxExtent() const
{
	return Box ? Box->GetUnscaledBoxExtent() : FVector::ZeroVector;
}
