#include "Grid/URogueDungeonSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Math/RandomStream.h"
#include "Data/RogueFloorConfigData.h"
#include "Grid/DungeonConfigActor.h"
#include "Grid/DungeonFloorGenerator.h"
#include "Grid/DungeonRenderComponent.h"
#include "Grid/AABB.h"
#include "Components/BoxComponent.h"
#include "Containers/Queue.h"
#include "Turn/TurnEventDispatcher.h"

DEFINE_LOG_CATEGORY_STATIC(LogRogueDungeon, Log, All);

void URogueDungeonSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogRogueDungeon, Warning, TEXT("★★★ [URogueDungeonSubsystem] INIT_V3 - Subsystem ready (NEW BINARY) ★★★"));
}

void URogueDungeonSubsystem::StartGenerateFromLevel()
{
	UE_LOG(LogRogueDungeon, Warning, TEXT("★★★ [URogueDungeonSubsystem] START_GEN_V3 called (NEW BINARY) ★★★"));

    // Authority check & Re-entrancy guard
    if (!GetWorld() || !GetWorld()->GetAuthGameMode())
    {
		UE_LOG(LogRogueDungeon, Warning, TEXT("[URogueDungeonSubsystem] Not on server, skipping generation"));
        return; // Not on server
    }
    if (bDungeonGenerationStarted)
    {
        UE_LOG(LogRogueDungeon, Warning, TEXT("[URogueDungeonSubsystem] StartGenerateFromLevel: Generation already started. Ignored."));
        return;
    }

    EnsureConfigActor();

    const URogueFloorConfigData* Config = nullptr;
    if (CachedConfigActor && CachedConfigActor->DungeonConfigAsset)
    {
        Config = CachedConfigActor->DungeonConfigAsset;
    }

    if (!Config)
    {
        UE_LOG(LogRogueDungeon, Error, TEXT("[URogueDungeonSubsystem] No URogueFloorConfigData found in level. Place an ADungeonConfigActor and assign a valid config asset."));
        return;
    }

    UE_LOG(LogRogueDungeon, Log, TEXT("[URogueDungeonSubsystem] ConfigActor=%s, ConfigDA=%s"), *GetNameSafe(CachedConfigActor), *Config->GetPathName());

    // Set the flag *before* starting generation
    bDungeonGenerationStarted = true;
    StartGenerate(Config);
}

void URogueDungeonSubsystem::StartGenerate(const URogueFloorConfigData* Cfg)
{
    if (!Cfg)
    {
        UE_LOG(LogTemp, Error, TEXT("[RogueSubsystem] StartGenerate: Invalid config provided."));
        return;
    }

    EnsureFloorGenerator();
    if (!FloorGenerator || !IsValid(FloorGenerator))
    {
        UE_LOG(LogTemp, Error, TEXT("[RogueSubsystem] No FloorGenerator available."));
        return;
    }

    FRandomStream Rng;
    Rng.GenerateNewSeed(); // URogueFloorConfigData にはシード設定がないため、常に新しいシードを生成

    UE_LOG(LogTemp, Display, TEXT("[RogueSubsystem] Generate START (Seed=%d, Map=%dx%d, Cell=%d)"), Rng.GetCurrentSeed(), Cfg->Width, Cfg->Height, Cfg->CellSizeUU);

    FloorGenerator->Generate(Cfg, Rng);
    
    RebuildRoomMarkers();

    EnsureRenderComponent();
    if (RenderComponent && IsValid(RenderComponent))
    {
        RenderComponent->RenderDungeonFromFloor(FloorGenerator);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[RogueSubsystem] RenderComponent not available. Dungeon mesh will not be shown."));
    }

    int32 RoomCount, WalkableCount;
    float Reachability;
    FloorGenerator->GetGenerationStats(RoomCount, WalkableCount, Reachability);
    UE_LOG(LogTemp, Display, TEXT("[RogueSubsystem] Generate DONE (Rooms=%d, Walkable=%d, Reach=%.1f%%)"), RoomCount, WalkableCount, Reachability * 100.0f);

    // ★★★ コアシステム: OnFloorReady配信（2025-11-09） ★★★
    // EventDispatcher経由で配信を試みる
    if (UWorld* World = GetWorld())
    {
        if (auto* EventDispatcher = World->GetSubsystem<UTurnEventDispatcher>())
        {
            EventDispatcher->BroadcastFloorReady(this);
        }
    }

    // 既存のデリゲートも維持（後方互換性）
    OnGridReady.Broadcast(this);
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
    UE_LOG(LogTemp, Log, TEXT("[RogueSubsystem] World=%s  FG=%s  RC=%s"),
        *GetNameSafe(GetWorld()),
        *GetNameSafe(FloorGenerator),
        *GetNameSafe(RenderComponent));
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

void URogueDungeonSubsystem::TransitionToFloor(int32 FloorIndex)
{
    // Authority check
    if (!GetWorld() || !GetWorld()->GetAuthGameMode())
    {
        UE_LOG(LogTemp, Warning, TEXT("[RogueSubsystem] TransitionToFloor: Not on server. Ignored."));
        return;
    }

    EnsureConfigActor();

    const URogueFloorConfigData* Config = nullptr;
    if (CachedConfigActor && CachedConfigActor->DungeonConfigAsset)
    {
        Config = CachedConfigActor->DungeonConfigAsset;
    }

    if (!Config)
    {
        UE_LOG(LogTemp, Error, TEXT("[RogueSubsystem] TransitionToFloor: No URogueFloorConfigData found in level."));
        return;
    }

    // Reset generation flag to allow re-generation
    bDungeonGenerationStarted = false;

    // Destroy existing room markers before generating new floor
    DestroyRoomMarkers();

    // Start generation with the config (seed can be modified based on FloorIndex if needed)
    StartGenerate(Config);
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
    const TArray<int32>& Grid = FloorGenerator->GridCells;

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

void URogueDungeonSubsystem::EnsureConfigActor()
{
    if (CachedConfigActor && IsValid(CachedConfigActor)) return;

    UWorld* World = GetWorld();
    if (!World) return;

    TArray<AActor*> FoundActors;
    FoundActors.Reserve(1);
    UGameplayStatics::GetAllActorsOfClass(World, ADungeonConfigActor::StaticClass(), FoundActors);

    if (FoundActors.Num() > 0)
    {
        CachedConfigActor = Cast<ADungeonConfigActor>(FoundActors[0]);
    }
}

void URogueDungeonSubsystem::Deinitialize()
{
	UE_LOG(LogRogueDungeon, Log, TEXT("[URogueDungeonSubsystem] Deinitialize called - Cleaning up"));

    DestroyRoomMarkers();
    RoomMarkers.Reset();
    FloorGenerator = nullptr;
    RenderComponent = nullptr;
    CachedConfigActor = nullptr;

    // ★ 再入防止フラグをリセット（PIE再起動時に再生成を可能にする）
    bDungeonGenerationStarted = false;

    Super::Deinitialize();
}
