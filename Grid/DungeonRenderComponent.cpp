#include "DungeonRenderComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "DrawDebugHelpers.h"

UDungeonRenderComponent::UDungeonRenderComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    bVisualizeComponent = false;
}

void UDungeonRenderComponent::BeginPlay()
{
    Super::BeginPlay();
    if (bRenderInGame && bBuildInstancesImmediately && CachedFloorGenerator)
        RenderDungeonFromFloor(CachedFloorGenerator);
}

void UDungeonRenderComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
    DestroyISMComponents();
    Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UDungeonRenderComponent::CreateISMComponents()
{
    DestroyISMComponents();

    WallISM     = CreateISMComponent(TEXT("WallISM"), MeshSet.WallMesh, MeshSet.WallMaterial);
    FloorISM    = CreateISMComponent(TEXT("FloorISM"), MeshSet.FloorMesh, MeshSet.FloorMaterial);
    CorridorISM = CreateISMComponent(TEXT("CorridorISM"), MeshSet.CorridorMesh, MeshSet.CorridorMaterial);
    RoomISM     = CreateISMComponent(TEXT("RoomISM"), MeshSet.RoomMesh, MeshSet.RoomMaterial);
    DoorISM     = CreateISMComponent(TEXT("DoorISM"), MeshSet.DoorMesh, MeshSet.DoorMaterial);
    StairISM    = CreateISMComponent(TEXT("StairISM"), MeshSet.StairMesh, MeshSet.StairMaterial);

    AllISMs.Empty();
    if (WallISM)     AllISMs.Add(WallISM);
    if (FloorISM)    AllISMs.Add(FloorISM);
    if (CorridorISM) AllISMs.Add(CorridorISM);
    if (RoomISM)     AllISMs.Add(RoomISM);
    if (DoorISM)     AllISMs.Add(DoorISM);
    if (StairISM)    AllISMs.Add(StairISM);
}

void UDungeonRenderComponent::DestroyISMComponents()
{
    for (UInstancedStaticMeshComponent* ISM : AllISMs)
        if (ISM) ISM->DestroyComponent();
    AllISMs.Empty();
    WallISM = FloorISM = CorridorISM = RoomISM = DoorISM = StairISM = nullptr;
}

UInstancedStaticMeshComponent* UDungeonRenderComponent::CreateISMComponent(
    const FString& ComponentName, UStaticMesh* Mesh, UMaterialInterface* Material)
{
    if (!Mesh) return nullptr;
    AActor* Owner = GetOwner();
    if (!Owner) return nullptr;

    auto* ISM = NewObject<UInstancedStaticMeshComponent>(Owner, *ComponentName);
    ISM->SetStaticMesh(Mesh);
    if (Material) ISM->SetMaterial(0, Material);
    ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    ISM->SetCollisionResponseToAllChannels(ECR_Ignore);
    ISM->SetupAttachment(this);
    ISM->RegisterComponent();
    Owner->AddOwnedComponent(ISM);

    return ISM;
}

FVector UDungeonRenderComponent::GridToWorldPosition(int32 GridX, int32 GridY, float HeightOffset) const
{
    const FVector Base = GetComponentLocation();
    float WorldX = Base.X + (GridX + 0.5f) * CellSizeUU;
    float WorldY = Base.Y + (GridY + 0.5f) * CellSizeUU;
    float WorldZ = Base.Z + HeightOffset;
    return FVector(WorldX, WorldY, WorldZ);
}

void UDungeonRenderComponent::AddInstanceToISM(UInstancedStaticMeshComponent* ISM, int32 GridX, int32 GridY, float Height)
{
    if (!ISM) return;
    FVector Position = GridToWorldPosition(GridX, GridY, Height);
    FTransform Transform(FRotator::ZeroRotator, Position, FVector::OneVector);
    ISM->AddInstance(Transform);
}

void UDungeonRenderComponent::BuildAllInstances(ADungeonFloorGenerator* FloorGenerator)
{
    if (!FloorGenerator) return;

    CreateISMComponents();

    const int32 W = FloorGenerator->GridWidth;
    const int32 H = FloorGenerator->GridHeight;
    const TArray<int32>& Grid = FloorGenerator->mGrid;

    for (int32 Y = 0; Y < H; ++Y)
    {
        for (int32 X = 0; X < W; ++X)
        {
            const int32 V = Grid[Y * W + X];
            
            if (V < 0) AddInstanceToISM(WallISM, X, Y);
            else if (V == 0) AddInstanceToISM(FloorISM, X, Y);
            else if (V == 1) AddInstanceToISM(CorridorISM, X, Y);
            else if (V == 2) AddInstanceToISM(RoomISM, X, Y);
            else if (V == 3) AddInstanceToISM(DoorISM, X, Y);
            else if (V == 4 || V == 5) AddInstanceToISM(StairISM, X, Y);
        }
    }
}

void UDungeonRenderComponent::ExtractDungeonFeatures(ADungeonFloorGenerator* FloorGenerator)
{
    if (!FloorGenerator) return;

    CachedRoomCenters.Empty();

    const int32 W = FloorGenerator->GridWidth;
    const int32 H = FloorGenerator->GridHeight;

    TArray<uint8> Visited;
    Visited.Init(0, W * H);

    auto GetIndex = [W](int32 X, int32 Y) { return Y * W + X; };

    for (int32 Y = 0; Y < H; ++Y)
    {
        for (int32 X = 0; X < W; ++X)
        {
            int32 CellValue = FloorGenerator->ReturnGridStatus(FVector(X * CellSizeUU, Y * CellSizeUU, 0.0f));
            if (CellValue != 2) continue;
            if (Visited[GetIndex(X, Y)]) continue;

            TArray<FIntPoint> RoomCells;
            TQueue<FIntPoint> Q;
            Q.Enqueue(FIntPoint(X, Y));
            Visited[GetIndex(X, Y)] = 1;

            const FIntPoint D4[4] = {{1,0},{-1,0},{0,1},{0,-1}};
            while (!Q.IsEmpty())
            {
                FIntPoint P;
                Q.Dequeue(P);
                RoomCells.Add(P);

                for (const auto& D : D4)
                {
                    int32 NX = P.X + D.X, NY = P.Y + D.Y;
                    if (NX < 0 || NX >= W || NY < 0 || NY >= H) continue;
                    if (Visited[GetIndex(NX, NY)]) continue;
                    int32 NVal = FloorGenerator->ReturnGridStatus(FVector(NX * CellSizeUU, NY * CellSizeUU, 0.0f));
                    if (NVal != 2) continue;

                    Visited[GetIndex(NX, NY)] = 1;
                    Q.Enqueue(FIntPoint(NX, NY));
                }
            }

            if (RoomCells.Num() > 0)
            {
                int32 CX = 0, CY = 0;
                for (const auto& Cell : RoomCells)
                {
                    CX += Cell.X;
                    CY += Cell.Y;
                }
                CX /= RoomCells.Num();
                CY /= RoomCells.Num();
                CachedRoomCenters.Add(FIntPoint(CX, CY));
            }
        }
    }
}

void UDungeonRenderComponent::DrawDebugGrid()
{
    if (!CachedFloorGenerator) return;

    const int32 W = CachedFloorGenerator->GridWidth;
    const int32 H = CachedFloorGenerator->GridHeight;
    const float Scale = DebugDrawScale;

    for (int32 Y = 0; Y <= H; ++Y)
    {
        FVector Start = GridToWorldPosition(0, Y);
        FVector End = GridToWorldPosition(W, Y);
        DrawDebugLine(GetWorld(), Start, End, DebugGridColor.ToFColor(true), false, 0.0f, SDPG_Foreground, 2.0f * Scale);
    }

    for (int32 X = 0; X <= W; ++X)
    {
        FVector Start = GridToWorldPosition(X, 0);
        FVector End = GridToWorldPosition(X, H);
        DrawDebugLine(GetWorld(), Start, End, DebugGridColor.ToFColor(true), false, 0.0f, SDPG_Foreground, 2.0f * Scale);
    }
}

void UDungeonRenderComponent::DrawDebugRoomCenters()
{
    for (const auto& Center : CachedRoomCenters)
    {
        FVector WorldPos = GridToWorldPosition(Center.X, Center.Y, CellHeightUU / 2.0f);
        DrawDebugSphere(GetWorld(), WorldPos, 50.0f * DebugDrawScale, 8, DebugRoomCenterColor.ToFColor(true), false, 0.0f, SDPG_Foreground);
    }
}

void UDungeonRenderComponent::DrawDebugCorridorPaths()
{
    if (!CachedFloorGenerator) return;

    const int32 W = CachedFloorGenerator->GridWidth;
    const int32 H = CachedFloorGenerator->GridHeight;

    for (int32 Y = 0; Y < H; ++Y)
    {
        for (int32 X = 0; X < W; ++X)
        {
            int32 CellValue = CachedFloorGenerator->ReturnGridStatus(FVector(X * CellSizeUU, Y * CellSizeUU, 0.0f));
            if (CellValue != 1) continue;

            FVector WorldPos = GridToWorldPosition(X, Y, CellHeightUU / 2.0f);
            DrawDebugBox(GetWorld(), WorldPos, FVector(CellSizeUU * 0.4f * DebugDrawScale), DebugCorridorColor.ToFColor(true), false, 0.0f, SDPG_Foreground);
        }
    }
}

void UDungeonRenderComponent::RenderDungeonFromFloor(ADungeonFloorGenerator* FloorGenerator)
{
    if (!FloorGenerator) 
    {
        UE_LOG(LogTemp, Error, TEXT("[DungeonRenderComponent] RenderDungeonFromFloor called with NULL FloorGenerator!"));
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("[DungeonRenderComponent] RenderDungeonFromFloor called. bRenderInGame=%d, CellSize=%d, DebugDrawMode=%d"), 
        bRenderInGame, 
        FloorGenerator->CellSize,
        (int32)DebugDrawMode);

    if (!MeshSet.FloorMesh || !MeshSet.WallMesh)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DungeonRenderComponent] MeshSet is incomplete! FloorMesh=%s, WallMesh=%s. Dungeon will not be visible."), 
            *GetNameSafe(MeshSet.FloorMesh), 
            *GetNameSafe(MeshSet.WallMesh));
    }

    CachedFloorGenerator = FloorGenerator;
    CellSizeUU = FloorGenerator->CellSize;

    const bool bShouldRender = (GetWorld()->IsGameWorld() && bRenderInGame) ||
                                (!GetWorld()->IsGameWorld() && bRenderInEditor);
    if (!bShouldRender) 
    {
        UE_LOG(LogTemp, Warning, TEXT("[DungeonRenderComponent] bShouldRender is false. Skipping all rendering."));
        return;
    }

    BuildAllInstances(FloorGenerator);
    ExtractDungeonFeatures(FloorGenerator);

    if (DebugDrawMode != EDebugDrawMode::None)
    {
        UE_LOG(LogTemp, Log, TEXT("[DungeonRenderComponent] Attempting to draw debug shapes..."));
        if (DebugDrawMode == EDebugDrawMode::GridLines || DebugDrawMode == EDebugDrawMode::All)
            DrawDebugGrid();
        if (DebugDrawMode == EDebugDrawMode::RoomCenters || DebugDrawMode == EDebugDrawMode::All)
            DrawDebugRoomCenters();
        if (DebugDrawMode == EDebugDrawMode::CorridorPaths || DebugDrawMode == EDebugDrawMode::All)
            DrawDebugCorridorPaths();
    }
}

void UDungeonRenderComponent::ClearAllInstances()
{
    for (UInstancedStaticMeshComponent* ISM : AllISMs)
        if (ISM) ISM->ClearInstances();
    CachedRoomCenters.Empty();
}

void UDungeonRenderComponent::SetDebugDrawMode(EDebugDrawMode Mode)
{
    DebugDrawMode = Mode;
}

EDebugDrawMode UDungeonRenderComponent::GetDebugDrawMode() const
{
    return DebugDrawMode;
}