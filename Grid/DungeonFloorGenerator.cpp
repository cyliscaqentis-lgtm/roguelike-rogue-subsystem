#include "Grid/DungeonFloorGenerator.h"
#include "Data/RogueFloorConfigData.h"
#include "Data/DungeonTemplateAsset.h"
#include "Math/RandomStream.h"
#include "Containers/Queue.h"
#include "UObject/UObjectGlobals.h"
#include "Utility/GridUtils.h"

ADungeonFloorGenerator::ADungeonFloorGenerator()
{
    PrimaryActorTick.bCanEverTick = false;
}

void ADungeonFloorGenerator::Generate(const URogueFloorConfigData* Config, FRandomStream& Rng)
{
    if (!Config)
    {
        UE_LOG(LogTemp, Error, TEXT("[Generator] Generate called with null Config."));
        return;
    }

    const FDungeonTemplateConfig* Picked = Config->PickTemplateConfig(Rng);
    if (!Picked)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Generator] No template picked from Config."));
        return;
    }

    UDungeonTemplateAsset* TemplateAsset = nullptr;
    if (Picked->TemplateClass)
    {
        TemplateAsset = NewObject<UDungeonTemplateAsset>(this, Picked->TemplateClass, NAME_None, RF_Transient);
    }

    if (!TemplateAsset)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Generator] No valid template class provided in Config."));
        return;
    }

    const FDungeonResolvedParams Params = Config->ResolveParamsFor(*Picked);

    GridWidth = Params.Width;
    GridHeight = Params.Height;
    CellSize = Params.CellSizeUU;

    GenerateWithTemplate(TemplateAsset, Params, Rng);
}

void ADungeonFloorGenerator::GenerateWithTemplate(UDungeonTemplateAsset* TemplateAsset, const FDungeonResolvedParams& Params, FRandomStream& Rng)
{
    if (!TemplateAsset)
    {
        UE_LOG(LogTemp, Error, TEXT("[Generator] GenerateWithTemplate called with null TemplateAsset."));
        return;
    }

    // Reset cached stats so we do not leak values from previous generations
    LastGeneratedRoomCount = 0;
    LastGeneratedWalkableCount = 0;
    LastGeneratedReachability = 0.0f;

    for (int attempt = 0; attempt < Params.MaxReroll; ++attempt)
    {
        ResetGrid(ECellType::Wall);
        EnsureOuterWall();

        // Delegate to the template asset to generate the base map
        TemplateAsset->Generate(this, Params, Rng);

        FixDoubleWidthCorridors();
        AutoPlaceDoors(Params);
        PlaceStairsFarthestPair();

        const int32 ActualRoomCount = ComputeRoomClusterCount();
        LastGeneratedRoomCount = ActualRoomCount;
        if (ActualRoomCount <= 0 || (Params.MinRooms > 0 && ActualRoomCount < Params.MinRooms))
        {
            UE_LOG(LogTemp, Warning,
                TEXT("Dungeon attempt %d rejected: produced %d rooms (min required=%d) with template %s. Re-rolling..."),
                attempt + 1,
                ActualRoomCount,
                Params.MinRooms,
                *GetNameSafe(TemplateAsset));
            continue;
        }

        float reachability = 0.0f;
        if (ValidateReachability(reachability, Params))
        {
            LastGeneratedWalkableCount = CountWalkable();
            LastGeneratedReachability = reachability;

            UE_LOG(LogTemp, Log, TEXT("Dungeon GENERATED: Seed=%s Rooms=%d Walkable=%d Reach=%.1f%%"),
                   *Rng.ToString(), LastGeneratedRoomCount, LastGeneratedWalkableCount, reachability * 100.0f);
            return;
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("Dungeon FAILED: Seed=%s after %d attempts"),
           *Rng.ToString(), Params.MaxReroll);

    if (GenerateFallbackLayout(Params, Rng))
    {
        return;
    }
}

// --- Restored Logic ---

void ADungeonFloorGenerator::ResetGrid(ECellType Fill)
{
    GridCells.Init(static_cast<int32>(Fill), GridWidth * GridHeight);
}

void ADungeonFloorGenerator::EnsureOuterWall()
{
    for (int x = 0; x < GridWidth; ++x) { SetCellXY(x, 0, ECellType::Wall); SetCellXY(x, GridHeight - 1, ECellType::Wall); }
    for (int y = 0; y < GridHeight; ++y) { SetCellXY(0, y, ECellType::Wall); SetCellXY(GridWidth - 1, y, ECellType::Wall); }
}

void ADungeonFloorGenerator::SetCellXY(int32 X, int32 Y, ECellType Type)
{
    if (!InBounds(X, Y)) return;
    GridCells[Index(X, Y)] = static_cast<int32>(Type);
}

ECellType ADungeonFloorGenerator::GetCellXY(int32 X, int32 Y) const
{
    if (!InBounds(X, Y)) return ECellType::Wall;
    return static_cast<ECellType>(GridCells[Index(X, Y)]);
}

void ADungeonFloorGenerator::CarveLineX(const FIntPoint& A, const FIntPoint& B, ECellType Type)
{
    const int step = (A.X <= B.X) ? 1 : -1;
    for (int x = A.X; x != B.X + step; x += step) SetCellXY(x, A.Y, Type);
}

void ADungeonFloorGenerator::CarveLineY(const FIntPoint& A, const FIntPoint& B, ECellType Type)
{
    const int step = (A.Y <= B.Y) ? 1 : -1;
    for (int y = A.Y; y != B.Y + step; y += step) SetCellXY(A.X, y, Type);
}

void ADungeonFloorGenerator::CarveManhattan_XY(const FIntPoint& A, const FIntPoint& B, ECellType Type)
{
    const FIntPoint M{ B.X, A.Y };
    CarveLineX(A, M, Type);
    CarveLineY(M, B, Type);
}

void ADungeonFloorGenerator::FixDoubleWidthCorridors()
{
    auto isC = [&](int x, int y) { return InBounds(x, y) && GetCellXY(x, y) == ECellType::Corridor; };
    for (int y = 1; y < GridHeight; ++y)
        for (int x = 1; x < GridWidth; ++x)
            if (isC(x, y) && isC(x - 1, y) && isC(x, y - 1) && isC(x - 1, y - 1))
                SetCellXY(x, y, ECellType::Wall);
}

bool ADungeonFloorGenerator::IsValidDoorPlacement(int32 X, int32 Y, const FDungeonResolvedParams& Params) const
{
    if (!InBounds(X, Y)) return false;
    const int spacing = Params.MinDoorSpacing;
    for (int dy = -spacing; dy <= spacing; ++dy)
        for (int dx = -spacing; dx <= spacing; ++dx)
        {
            if (dx == 0 && dy == 0) continue;
            if (FMath::Abs(dx) + FMath::Abs(dy) > spacing) continue;
            const int nx = X + dx;
            const int ny = Y + dy;
            if (InBounds(nx, ny) && GetCellXY(nx, ny) == ECellType::Door) return false;
        }
    return true;
}

void ADungeonFloorGenerator::PlaceDoorIfBoundary(int32 X, int32 Y, const FDungeonResolvedParams& Params)
{
    if (!InBounds(X, Y) || GetCellXY(X, Y) != ECellType::Room) return;
    int adjCorr = 0, adjRoom = 0;
    const FIntPoint d4[4] = { {1,0},{-1,0},{0,1},{0,-1} };
    for (auto d : d4)
    {
        const int nx = X + d.X, ny = Y + d.Y;
        if (!InBounds(nx, ny)) continue;
        const ECellType t = GetCellXY(nx, ny);
        if (t == ECellType::Corridor) ++adjCorr;
        if (t == ECellType::Room) ++adjRoom;
    }
    if (adjCorr >= 1 && adjRoom >= 1 && IsValidDoorPlacement(X, Y, Params))
        SetCellXY(X, Y, ECellType::Door);
}

void ADungeonFloorGenerator::AutoPlaceDoors(const FDungeonResolvedParams& Params)
{
    for (int y = 1; y < GridHeight - 1; ++y)
        for (int x = 1; x < GridWidth - 1; ++x)
            PlaceDoorIfBoundary(x, y, Params);
}

void ADungeonFloorGenerator::PlaceStairsFarthestPair()
{
    FIntPoint start{ -1,-1 };
    for (int y = 0; y < GridHeight && start.X < 0; ++y)
        for (int x = 0; x < GridWidth; ++x)
            if (IsWalkable(x, y)) { start = { x,y }; break; }
    if (start.X < 0) return;

    auto bfs = [&](FIntPoint s, TArray<int32>& dist) -> FIntPoint {
        dist.Init(-1, GridWidth * GridHeight);
        TQueue<FIntPoint> q; q.Enqueue(s); dist[Index(s.X, s.Y)] = 0;
        const FIntPoint d4[4] = { {1,0},{-1,0},{0,1},{0,-1} };
        FIntPoint last = s;
        while (!q.IsEmpty()) {
            FIntPoint p; q.Dequeue(p); last = p;
            for (auto d : d4) {
                const int nx = p.X + d.X, ny = p.Y + d.Y;
                if (!InBounds(nx, ny) || !IsWalkable(nx, ny)) continue;
                const int idx = Index(nx, ny); if (dist[idx] >= 0) continue;
                dist[idx] = dist[Index(p.X, p.Y)] + 1; q.Enqueue({ nx,ny });
            }
        }
        return last;
    };

    TArray<int32> dist1, dist2;
    const FIntPoint a = bfs(start, dist1);
    const FIntPoint b = bfs(a, dist2);
    SetCellXY(a.X, a.Y, ECellType::StairUp);
    SetCellXY(b.X, b.Y, ECellType::StairDown);
}

void ADungeonFloorGenerator::BSP_Split(const FIntRectLite& Root, FRandomStream& RNG, const FDungeonResolvedParams& Params, TArray<FIntRectLite>& OutLeaves)
{
    TArray<FIntRectLite> stack;
    stack.Reserve(64); // Reserve for BSP split operations
    stack.Add(Root);
    const int32 minLeafW = Params.MinRoomSize + 2 * Params.RoomMargin + 2;
    const int32 minLeafH = Params.MinRoomSize + 2 * Params.RoomMargin + 2;

    while (stack.Num() > 0)
    {
        FIntRectLite leaf = stack.Pop();
        const bool canSplitH = leaf.W() >= 2 * minLeafW;
        const bool canSplitV = leaf.H() >= 2 * minLeafH;
        bool doSplit = (canSplitH || canSplitV) && (RNG.FRand() > Params.StopSplitProbability);

        if (!doSplit) { OutLeaves.Add(leaf); continue; }

        const bool splitVertical =
            (canSplitH && !canSplitV) ? true :
            (!canSplitH && canSplitV) ? false :
            (RNG.RandRange(0, 1) == 0);

        if (splitVertical)
        {
            const int32 cutMin = leaf.X0 + minLeafW;
            const int32 cutMax = leaf.X1 - minLeafW;
            if (cutMin >= cutMax) { OutLeaves.Add(leaf); continue; }
            const int32 cut = RNG.RandRange(cutMin, cutMax);
            stack.Add(FIntRectLite(leaf.X0, leaf.Y0, cut, leaf.Y1));
            stack.Add(FIntRectLite(cut + 1, leaf.Y0, leaf.X1, leaf.Y1));
        }
        else
        {
            const int32 cutMin = leaf.Y0 + minLeafH;
            const int32 cutMax = leaf.Y1 - minLeafH;
            if (cutMin >= cutMax) { OutLeaves.Add(leaf); continue; }
            const int32 cut = RNG.RandRange(cutMin, cutMax);
            stack.Add(FIntRectLite(leaf.X0, leaf.Y0, leaf.X1, cut));
            stack.Add(FIntRectLite(leaf.X0, cut + 1, leaf.X1, leaf.Y1));
        }
    }
}

FIntRectLite ADungeonFloorGenerator::MakeRoomInLeaf(const FIntRectLite& Leaf, FRandomStream& RNG, const FDungeonResolvedParams& Params, const TArray<FIntRectLite>& ExistingRooms) const
{
    const int32 margin = Params.RoomMargin;
    const int32 minW = Params.MinRoomSize;
    const int32 minH = Params.MinRoomSize;
    const int32 maxW = FMath::Min(Params.MaxRoomSize, Leaf.W() - 2 * margin);
    const int32 maxH = FMath::Min(Params.MaxRoomSize, Leaf.H() - 2 * margin);
    if (maxW < minW || maxH < minH) return FIntRectLite();

    for (int attempt = 0; attempt < 10; ++attempt)
    {
        const int32 w = RNG.RandRange(minW, maxW);
        const int32 h = RNG.RandRange(minH, maxH);
        const int32 rx0 = RNG.RandRange(Leaf.X0 + margin, FMath::Max(Leaf.X0 + margin, Leaf.X1 - margin - w + 1));
        const int32 ry0 = RNG.RandRange(Leaf.Y0 + margin, FMath::Max(Leaf.Y0 + margin, Leaf.Y1 - margin - h + 1));
        FIntRectLite room(rx0, ry0, rx0 + w - 1, ry0 + h - 1);

        bool overlaps = false;
        for (const auto& existing : ExistingRooms)
            if (room.OverlapsWith(existing, 1)) { overlaps = true; break; }

        if (!overlaps) return room;
    }
    return FIntRectLite();
}

void ADungeonFloorGenerator::ConnectCentersWithMST(const TArray<FIntPoint>& Centers, ECellType CorridorType)
{
    if (Centers.Num() <= 1) return;

    TArray<int32> used;
    used.Reserve(Centers.Num());
    used.Add(0);

    TArray<int32> unused;
    unused.Reserve(Centers.Num() - 1);
    for (int32 i = 1; i < Centers.Num(); ++i)
        unused.Add(i);

    while (unused.Num() > 0)
    {
        int32 bestU = -1, bestV = -1, bestIdx = -1;
        int32 bestDist = TNumericLimits<int32>::Max();
        for (int32 u : used)
            for (int32 vIdx = 0; vIdx < unused.Num(); ++vIdx)
            {
                const int32 v = unused[vIdx];
                // ☁E�E☁E最適匁E GridUtils使用�E�重褁E��ード削除 2025-11-09�E�E
                const int32 d = FGridUtils::ManhattanDistance(Centers[u], Centers[v]);
                if (d < bestDist) { bestDist = d; bestU = u; bestV = v; bestIdx = vIdx; }
            }
        if (bestU < 0) break;
        CarveManhattan_XY(Centers[bestU], Centers[bestV], CorridorType);
        used.Add(bestV);
        unused.RemoveAt(bestIdx);
    }
}

bool ADungeonFloorGenerator::Make_NormalBSP(FRandomStream& RNG, const FDungeonResolvedParams& Params)
{
    const FIntRectLite root(Params.OuterMargin, Params.OuterMargin,
        GridWidth - 1 - Params.OuterMargin, GridHeight - 1 - Params.OuterMargin);

    TArray<FIntRectLite> leaves;
    leaves.Reserve(Params.MaxRooms * 2);
    BSP_Split(root, RNG, Params, leaves);

    TArray<FIntPoint> centers;
    centers.Reserve(Params.MaxRooms);
    TArray<FIntRectLite> rooms;
    rooms.Reserve(Params.MaxRooms);
    int32 placed = 0;

    for (auto& leaf : leaves)
    {
        if (placed >= Params.MaxRooms) break;
        FIntRectLite room = MakeRoomInLeaf(leaf, RNG, Params, rooms);
        if (!room.IsValid()) continue;

        for (int y = room.Y0; y <= room.Y1; ++y)
            for (int x = room.X0; x <= room.X1; ++x)
                SetCellXY(x, y, ECellType::Room);

        rooms.Add(room);
        centers.Add(room.Center());
        ++placed;
    }

    if (centers.Num() < Params.MinRooms) return false;
    LastGeneratedRoomCount = centers.Num();

    ConnectCentersWithMST(centers, ECellType::Corridor);

    for (int i = 0; i < centers.Num(); ++i)
        if (RNG.FRand() < Params.ExtraConnectorChance)
        {
            const int j = RNG.RandRange(0, centers.Num() - 1);
            if (i != j) CarveManhattan_XY(centers[i], centers[j], ECellType::Corridor);
        }

    return true;
}

bool ADungeonFloorGenerator::Make_LargeHall(FRandomStream& RNG, const FDungeonResolvedParams& Params)
{
    const int32 w = FMath::Clamp(Params.MaxRoomSize * 3, 6, GridWidth - 2 - 2 * Params.OuterMargin);
    const int32 h = FMath::Clamp(Params.MaxRoomSize * 3, 6, GridHeight - 2 - 2 * Params.OuterMargin);
    const int32 ox = (GridWidth - w) / 2;
    const int32 oy = (GridHeight - h) / 2;

    for (int y = oy; y < oy + h; ++y)
        for (int x = ox; x < ox + w; ++x)
            SetCellXY(x, y, ECellType::Room);

    const int32 n = FMath::Clamp(Params.MinRooms / 2, 3, 10);
    TArray<FIntPoint> centers;
    centers.Reserve(n + 1);
    centers.Add({ ox + w / 2, oy + h / 2 });
    for (int i = 0; i < n; ++i)
    {
        const int32 rw = RNG.RandRange(Params.MinRoomSize, Params.MinRoomSize + 2);
        const int32 rh = RNG.RandRange(Params.MinRoomSize, Params.MinRoomSize + 2);
        const int32 rx = RNG.RandRange(Params.OuterMargin + 1, GridWidth - rw - Params.OuterMargin - 1);
        const int32 ry = RNG.RandRange(Params.OuterMargin + 1, GridHeight - rh - Params.OuterMargin - 1);

        for (int y = ry; y < ry + rh; ++y)
            for (int x = rx; x < rx + rw; ++x)
                SetCellXY(x, y, ECellType::Room);

        const FIntPoint c{ rx + rw / 2, ry + rh / 2 };
        centers.Add(c);
        CarveManhattan_XY(c, centers[0], ECellType::Corridor);
    }

    LastGeneratedRoomCount = centers.Num();
    return true;
}

bool ADungeonFloorGenerator::Make_FourQuads(FRandomStream& RNG, const FDungeonResolvedParams& Params)
{
    const int32 midX = GridWidth / 2;
    const int32 midY = GridHeight / 2;
    const int32 margin = FMath::Max(1, Params.RoomMargin);

    auto placeRect = [&](int32 x0, int32 y0, int32 x1, int32 y1) -> FIntPoint
    {
        x0 = FMath::Clamp(x0 + margin, Params.OuterMargin + 1, GridWidth - 2);
        y0 = FMath::Clamp(y0 + margin, Params.OuterMargin + 1, GridHeight - 2);
        x1 = FMath::Clamp(x1 - margin, Params.OuterMargin + 1, GridWidth - 2);
        y1 = FMath::Clamp(y1 - margin, Params.OuterMargin + 1, GridHeight - 2);

        const int32 rw = FMath::Clamp(Params.MaxRoomSize, Params.MinRoomSize, (x1 - x0 + 1));
        const int32 rh = FMath::Clamp(Params.MaxRoomSize, Params.MinRoomSize, (y1 - y0 + 1));
        const int32 rx = x0 + (x1 - x0 - rw + 1) / 2;
        const int32 ry = y0 + (y1 - y0 - rh + 1) / 2;

        for (int y = ry; y < ry + rh; ++y)
            for (int x = rx; x < rx + rw; ++x)
                SetCellXY(x, y, ECellType::Room);

        return { rx + rw / 2, ry + rh / 2 };
    };

    TArray<FIntPoint> centers;
    centers.Reserve(4);
    centers.Add(placeRect(1, 1, midX - 1, midY - 1));
    centers.Add(placeRect(midX + 1, 1, GridWidth - 2, midY - 1));
    centers.Add(placeRect(1, midY + 1, midX - 1, GridHeight - 2));
    centers.Add(placeRect(midX + 1, midY + 1, GridWidth - 2, GridHeight - 2));

    for (int x = Params.OuterMargin + 1; x < GridWidth - Params.OuterMargin - 1; ++x) SetCellXY(x, midY, ECellType::Corridor);
    for (int y = Params.OuterMargin + 1; y < GridHeight - Params.OuterMargin - 1; ++y) SetCellXY(midX, y, ECellType::Corridor);

    ConnectCentersWithMST(centers, ECellType::Corridor);
    LastGeneratedRoomCount = 4;
    return true;
}

bool ADungeonFloorGenerator::Make_CentralCrossWithMiniRooms(FRandomStream& RNG, const FDungeonResolvedParams& Params)
{
    const int32 y0 = GridHeight / 2;
    for (int x = Params.OuterMargin + 1; x < GridWidth - Params.OuterMargin - 1; ++x) SetCellXY(x, y0, ECellType::Corridor);

    const int interval = FMath::Max(Params.MinRoomSize + 2, 6);
    const int32 EstimatedRooms = (GridWidth - 2 * Params.OuterMargin) / interval + 2;

    TArray<FIntPoint> centers;
    centers.Reserve(EstimatedRooms);

    for (int x = Params.OuterMargin + 2; x < GridWidth - Params.OuterMargin - 2; x += interval)
    {
        const int32 rw = RNG.RandRange(Params.MinRoomSize, FMath::Min(Params.MinRoomSize + 1, Params.MaxRoomSize));
        const int32 rh = RNG.RandRange(Params.MinRoomSize, FMath::Min(Params.MinRoomSize + 1, Params.MaxRoomSize));
        const int32 rx = FMath::Clamp(x - rw / 2, Params.OuterMargin + 1, GridWidth - rw - Params.OuterMargin - 1);
        const int32 side = RNG.RandRange(0, 1) == 0 ? -1 : 1;
        const int32 ry = FMath::Clamp(y0 + side * (2 + rh), Params.OuterMargin + 1, GridHeight - rh - Params.OuterMargin - 1);

        for (int y = ry; y < ry + rh; ++y)
            for (int xx = rx; xx < rx + rw; ++xx)
                SetCellXY(xx, y, ECellType::Room);

        const FIntPoint c{ rx + rw / 2, ry + rh / 2 };
        centers.Add(c);
        CarveManhattan_XY(c, { x, y0 }, ECellType::Corridor);
    }

    LastGeneratedRoomCount = centers.Num();
    return centers.Num() > 0;
}

bool ADungeonFloorGenerator::IsWalkable(int32 X, int32 Y) const
{
    const ECellType t = GetCellXY(X, Y);
    return (t != ECellType::Wall);
}

int32 ADungeonFloorGenerator::CountWalkable() const
{
    int32 c = 0;
    for (int y = 0; y < GridHeight; ++y)
        for (int x = 0; x < GridWidth; ++x)
            if (IsWalkable(x, y)) ++c;
    return c;
}

bool ADungeonFloorGenerator::GenerateFallbackLayout(const FDungeonResolvedParams& Params, FRandomStream& Rng)
{
    FDungeonResolvedParams RelaxedParams = Params;
    RelaxedParams.MinRooms = FMath::Min(Params.MinRooms, 4);
    RelaxedParams.ReachabilityThreshold = FMath::Min(Params.ReachabilityThreshold, 0.25f);

    ResetGrid(ECellType::Wall);
    EnsureOuterWall();

    bool bGenerated = Make_CentralCrossWithMiniRooms(Rng, RelaxedParams);
    if (!bGenerated)
    {
        const int32 Margin = FMath::Clamp(RelaxedParams.RoomMargin + 1, 1, 4);
        const int32 RoomW = FMath::Clamp(GridWidth - (Margin * 2), Params.MinRoomSize, GridWidth - 2);
        const int32 RoomH = FMath::Clamp(GridHeight - (Margin * 2), Params.MinRoomSize, GridHeight - 2);
        FillRect(Margin, Margin, RoomW, RoomH, ECellType::Room);
        LastGeneratedRoomCount = 1;
        bGenerated = true;
    }

    if (!bGenerated)
    {
        return false;
    }

    FixDoubleWidthCorridors();
    AutoPlaceDoors(RelaxedParams);
    PlaceStairsFarthestPair();

    LastGeneratedWalkableCount = CountWalkable();
    float ReachValue = 0.0f;
    if (!ValidateReachability(ReachValue, RelaxedParams))
    {
        ReachValue = FMath::Clamp(ReachValue, 0.0f, 1.0f);
    }
    LastGeneratedReachability = ReachValue;

    UE_LOG(LogTemp, Warning,
        TEXT("Dungeon FALLBACK generated: Rooms=%d Walkable=%d Reach=%.1f%%"),
        LastGeneratedRoomCount,
        LastGeneratedWalkableCount,
        ReachValue * 100.0f);
    return true;
}

int32 ADungeonFloorGenerator::ComputeRoomClusterCount() const
{
    if (GridWidth <= 0 || GridHeight <= 0 || GridCells.Num() != GridWidth * GridHeight)
    {
        return 0;
    }

    const int32 RoomValue = static_cast<int32>(ECellType::Room);
    TArray<uint8> Visited;
    Visited.Init(0, GridCells.Num());
    int32 ClusterCount = 0;

    for (int32 Y = 0; Y < GridHeight; ++Y)
    {
        for (int32 X = 0; X < GridWidth; ++X)
        {
            const int32 IndexValue = Index(X, Y);
            if (Visited[IndexValue] || GridCells[IndexValue] != RoomValue)
            {
                continue;
            }

            ++ClusterCount;
            TQueue<FIntPoint> Queue;
            Queue.Enqueue(FIntPoint(X, Y));
            Visited[IndexValue] = 1;

            while (!Queue.IsEmpty())
            {
                FIntPoint Current;
                Queue.Dequeue(Current);

                static const FIntPoint Directions[4] = { {1,0}, {-1,0}, {0,1}, {0,-1} };
                for (const FIntPoint& Dir : Directions)
                {
                    const int32 NX = Current.X + Dir.X;
                    const int32 NY = Current.Y + Dir.Y;
                    if (!InBounds(NX, NY))
                    {
                        continue;
                    }

                    const int32 NeighborIdx = Index(NX, NY);
                    if (Visited[NeighborIdx] || GridCells[NeighborIdx] != RoomValue)
                    {
                        continue;
                    }

                    Visited[NeighborIdx] = 1;
                    Queue.Enqueue(FIntPoint(NX, NY));
                }
            }
        }
    }

    return ClusterCount;
}

bool ADungeonFloorGenerator::ValidateReachability(float& OutRatio, const FDungeonResolvedParams& Params)
{
    FIntPoint start{ -1,-1 };
    for (int y = 0; y < GridHeight && start.X < 0; ++y)
        for (int x = 0; x < GridWidth; ++x)
            if (IsWalkable(x, y)) { start = { x, y }; break; }
    if (start.X < 0) { OutRatio = 0.0f; return false; }

    TQueue<FIntPoint> q;
    TArray<uint8> vis; vis.Init(0, GridWidth * GridHeight);
    q.Enqueue(start); vis[Index(start.X, start.Y)] = 1;
    const FIntPoint d4[4] = { {1,0},{-1,0},{0,1},{0,-1} };
    int32 reached = 0;

    while (!q.IsEmpty())
    {
        FIntPoint p; q.Dequeue(p); ++reached;
        for (auto d : d4)
        {
            const int nx = p.X + d.X, ny = p.Y + d.Y;
            if (!InBounds(nx, ny) || !IsWalkable(nx, ny)) continue;
            const int idx = Index(nx, ny); if (vis[idx]) continue;
            vis[idx] = 1; q.Enqueue({ nx,ny });
        }
    }

    const int32 totalWalk = CountWalkable();
    if (totalWalk == 0) { OutRatio = 0.0f; return false; }
    OutRatio = float(reached) / float(totalWalk);
    return OutRatio >= Params.ReachabilityThreshold;
}

int32 ADungeonFloorGenerator::ReturnGridStatus(FVector InputVector) const
{
    const int32 X = FMath::FloorToInt(InputVector.X / float(CellSize));
    const int32 Y = FMath::FloorToInt(InputVector.Y / float(CellSize));
    if (!InBounds(X, Y)) return -999;
    return GridCells[Index(X, Y)];
}

void ADungeonFloorGenerator::GridChangeVector(FVector InputVector, int32 Value)
{
    const int32 X = FMath::FloorToInt(InputVector.X / float(CellSize));
    const int32 Y = FMath::FloorToInt(InputVector.Y / float(CellSize));
    if (!InBounds(X, Y)) return;
    GridCells[Index(X, Y)] = Value;
}

void ADungeonFloorGenerator::GetGenerationStats(int32& OutRoomCount, int32& OutWalkableCount, float& OutReachability)
{
    OutRoomCount = LastGeneratedRoomCount;
    OutWalkableCount = LastGeneratedWalkableCount;
    OutReachability = LastGeneratedReachability;
}

bool ADungeonFloorGenerator::IsInside(int32 X, int32 Y) const
{
    return InBounds(X, Y);
}

void ADungeonFloorGenerator::FillRect(int32 X, int32 Y, int32 W, int32 H, ECellType Type)
{
    for (int32 j = Y; j < Y + H; ++j)
    {
        for (int32 i = X; i < X + W; ++i)
        {
            if (InBounds(i, j))
            {
                SetCellXY(i, j, Type);
            }
        }
    }
}
