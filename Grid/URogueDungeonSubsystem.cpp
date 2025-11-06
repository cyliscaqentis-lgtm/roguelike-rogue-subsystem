#include "Grid/URogueDungeonSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Math/RandomStream.h"
#include "Data/RogueFloorConfigData.h"
#include "Grid/DungeonFloorGenerator.h"
#include "Grid/DungeonRenderComponent.h"
#include "Grid/AABB.h"
#include "Components/BoxComponent.h"
#include "Containers/Queue.h"

void URogueDungeonSubsystem::RegisterFloorConfig(int32 FloorIndex, URogueFloorConfigData* Config)
{
    if (!Config) return;
    FloorConfigMap.Add(FloorIndex, Config);
}

void URogueDungeonSubsystem::TransitionToFloor(int32 FloorIndex)
{
    CurrentFloorIndex = FloorIndex;

    EnsureFloorGenerator();
    if (!FloorGenerator || !IsValid(FloorGenerator))
    {
        UE_LOG(LogTemp, Warning, TEXT("[RogueSubsystem] No FloorGenerator."));
        return;
    }

    URogueFloorConfigData* Config = GetActiveConfig(FloorIndex);
    if (!Config)
    {
        UE_LOG(LogTemp, Warning, TEXT("[RogueSubsystem] No Config for floor %d."), FloorIndex);
        return;
    }

    BuildFloor_Internal(Config, FloorIndex);

    EnsureRenderComponent();

    OnGridReady.Broadcast();
}

UDungeonRenderComponent* URogueDungeonSubsystem::GetRenderComponent()
{
    EnsureRenderComponent();
    return RenderComponent;
}

ADungeonFloorGenerator* URogueDungeonSubsystem::GetFloorGenerator() const
{
    return FloorGenerator;
}

void URogueDungeonSubsystem::SmokeTest() const
{
    UE_LOG(LogTemp, Log, TEXT("[RogueSubsystem] World=%s  FG=%s  RC=%s  Floor=%d"),
        *GetNameSafe(GetWorld()),
        *GetNameSafe(FloorGenerator),
        *GetNameSafe(RenderComponent),
        CurrentFloorIndex);
}

void URogueDungeonSubsystem::GetGeneratedRooms(TArray<AAABB*>& OutRooms) const
{
    OutRooms.Reset();
    for (AAABB* Room : RoomMarkers)
    {
        if (IsValid(Room))
        {
            OutRooms.Add(Room);
        }
    }
}

URogueFloorConfigData* URogueDungeonSubsystem::GetActiveConfig(int32 FloorIndex) const
{
    if (const TObjectPtr<URogueFloorConfigData>* Found = FloorConfigMap.Find(FloorIndex))
    {
        return Found->Get();
    }
    return DefaultConfig;
}

void URogueDungeonSubsystem::EnsureFloorGenerator()
{
    if (FloorGenerator && IsValid(FloorGenerator)) return;

    UWorld* World = GetWorld();
    if (!World) return;

    TArray<AActor*> Found;
    UGameplayStatics::GetAllActorsOfClass(World, ADungeonFloorGenerator::StaticClass(), Found);
    if (Found.Num() > 0)
    {
        FloorGenerator = Cast<ADungeonFloorGenerator>(Found[0]);
        return;
    }

    FActorSpawnParameters P;
    P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    FloorGenerator = World->SpawnActor<ADungeonFloorGenerator>(
        ADungeonFloorGenerator::StaticClass(),
        FVector::ZeroVector, FRotator::ZeroRotator, P);
}

void URogueDungeonSubsystem::EnsureRenderComponentFromPlacedActor()
{
    if (RenderComponent && IsValid(RenderComponent)) return;

    UWorld* World = GetWorld(); if (!World) return;

    if (RenderSingletonTag.IsNone()) return;

    TArray<AActor*> Tagged;
    UGameplayStatics::GetAllActorsWithTag(World, RenderSingletonTag, Tagged);
    for (AActor* A : Tagged)
    {
        if (!A) continue;
        if (UDungeonRenderComponent* C = A->FindComponentByClass<UDungeonRenderComponent>())
        {
            RenderComponent = C;
            return;
        }
        
        TArray<UActorComponent*> Cs;
        A->GetComponents(UDungeonRenderComponent::StaticClass(), Cs);
        if (Cs.Num() > 0)
        {
            RenderComponent = Cast<UDungeonRenderComponent>(Cs[0]);
            return;
        }
    }
}

void URogueDungeonSubsystem::EnsureRenderComponent()
{
    if (RenderComponent && IsValid(RenderComponent)) return;

    EnsureRenderComponentFromPlacedActor();
    if (RenderComponent && IsValid(RenderComponent)) return;

    EnsureFloorGenerator();
    if (!FloorGenerator || !IsValid(FloorGenerator)) return;

    RenderComponent = NewObject<UDungeonRenderComponent>(FloorGenerator, TEXT("DungeonRender"));
    if (!RenderComponent) return;

    RenderComponent->RegisterComponent();

    if (USceneComponent* Root = FloorGenerator->GetRootComponent())
    {
        RenderComponent->AttachToComponent(Root, FAttachmentTransformRules::KeepRelativeTransform);
    }

    if (!RenderSingletonTag.IsNone())
    {
        RenderComponent->ComponentTags.AddUnique(RenderSingletonTag);
    }
}

void URogueDungeonSubsystem::BuildFloor_Internal(URogueFloorConfigData* Config, int32 FloorIndex)
{
    if (!Config || !FloorGenerator) return;

    FRandomStream Rng;
    if (bUseRandomSeed)
    {
        Rng.GenerateNewSeed();
    }
    else
    {
        const int32 Seed = HashCombine(::GetTypeHash(SeedBase), ::GetTypeHash(FloorIndex));
        Rng.Initialize(Seed);
    }

    FloorGenerator->Generate(Config, Rng);
    RebuildRoomMarkers();
}

void URogueDungeonSubsystem::DestroyRoomMarkers()
{
    for (AAABB* Room : RoomMarkers)
    {
        if (IsValid(Room))
        {
            Room->Destroy();
        }
    }
    RoomMarkers.Reset();
}

void URogueDungeonSubsystem::RebuildRoomMarkers()
{
    DestroyRoomMarkers();

    if (!FloorGenerator || !IsValid(FloorGenerator))
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    const int32 Width = FloorGenerator->GridWidth;
    const int32 Height = FloorGenerator->GridHeight;
    const int32 CellSize = FloorGenerator->CellSize;
    const TArray<int32>& Grid = FloorGenerator->mGrid;

    if (Width <= 0 || Height <= 0 || Grid.Num() != Width * Height)
    {
        return;
    }

    const int32 RoomValue = static_cast<int32>(ECellType::Room);
    TArray<uint8> Visited;
    Visited.Init(0, Grid.Num());

    auto IndexOf = [Width](int32 X, int32 Y)
    {
        return Y * Width + X;
    };

    for (int32 Y = 0; Y < Height; ++Y)
    {
        for (int32 X = 0; X < Width; ++X)
        {
            const int32 Index = IndexOf(X, Y);
            if (Visited[Index] || Grid[Index] != RoomValue)
            {
                continue;
            }

            int32 MinX = X;
            int32 MaxX = X;
            int32 MinY = Y;
            int32 MaxY = Y;

            TQueue<FIntPoint> Queue;
            Queue.Enqueue(FIntPoint(X, Y));
            Visited[Index] = 1;

            while (!Queue.IsEmpty())
            {
                FIntPoint Current;
                Queue.Dequeue(Current);

                MinX = FMath::Min(MinX, Current.X);
                MaxX = FMath::Max(MaxX, Current.X);
                MinY = FMath::Min(MinY, Current.Y);
                MaxY = FMath::Max(MaxY, Current.Y);

                static const FIntPoint Directions[4] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
                for (const FIntPoint& Dir : Directions)
                {
                    const int32 NX = Current.X + Dir.X;
                    const int32 NY = Current.Y + Dir.Y;
                    if (NX < 0 || NX >= Width || NY < 0 || NY >= Height)
                    {
                        continue;
                    }

                    const int32 NeighborIdx = IndexOf(NX, NY);
                    if (Visited[NeighborIdx] || Grid[NeighborIdx] != RoomValue)
                    {
                        continue;
                    }

                    Visited[NeighborIdx] = 1;
                    Queue.Enqueue(FIntPoint(NX, NY));
                }
            }

            const float TileSize = static_cast<float>(CellSize);
            const float CenterX = (static_cast<float>(MinX + MaxX + 1) * 0.5f) * TileSize;
            const float CenterY = (static_cast<float>(MinY + MaxY + 1) * 0.5f) * TileSize;
            const FVector SpawnLocation(CenterX, CenterY, FloorGenerator->GetActorLocation().Z);

            const float HalfWidth = static_cast<float>(MaxX - MinX + 1) * 0.5f * TileSize;
            const float HalfHeight = static_cast<float>(MaxY - MinY + 1) * 0.5f * TileSize;
            const FVector BoxExtent(HalfWidth, HalfHeight, TileSize * 0.5f);

            FActorSpawnParameters Params;
            Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            AAABB* RoomActor = World->SpawnActor<AAABB>(AAABB::StaticClass(), SpawnLocation, FRotator::ZeroRotator, Params);
            if (!RoomActor)
            {
                continue;
            }

            if (RoomActor->Box)
            {
                RoomActor->Box->SetBoxExtent(BoxExtent, true);
                RoomActor->Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            }
            RoomActor->SetActorHiddenInGame(true);
            RoomActor->SetActorEnableCollision(false);

            RoomMarkers.Add(RoomActor);
        }
    }
}

void URogueDungeonSubsystem::Deinitialize()
{
    DestroyRoomMarkers();
    RoomMarkers.Reset();
    FloorGenerator = nullptr;
    RenderComponent = nullptr;

    Super::Deinitialize();
}
