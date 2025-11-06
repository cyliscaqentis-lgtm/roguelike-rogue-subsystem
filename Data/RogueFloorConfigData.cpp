#include "Data/RogueFloorConfigData.h"
#include "Data/DungeonTemplateAsset.h"
#include "Math/RandomStream.h"

const FDungeonTemplateConfig* URogueFloorConfigData::PickTemplateConfig(FRandomStream& Rng) const
{
    if (TemplateConfigs.Num() > 0)
    {
        double Sum = 0.0;
        for (const auto& T : TemplateConfigs) { Sum += FMath::Max(0.0f, T.Weight); }
        if (Sum <= 0.0) return nullptr;

        double Pick = Rng.FRand() * Sum;
        for (const auto& T : TemplateConfigs)
        {
            const double W = FMath::Max(0.0f, T.Weight);
            if (Pick <= W) return &T;
            Pick -= W;
        }
        return &TemplateConfigs.Last();
    }

    if (TemplateWeights_Legacy.Num() > 0)
    {
        static TArray<FDungeonTemplateConfig> Temp;
        Temp.Reset();
        Temp.Reserve(TemplateWeights_Legacy.Num());
        for (const auto& L : TemplateWeights_Legacy)
        {
            FDungeonTemplateConfig T;
            if (L.Template) T.TemplateClass = L.Template->GetClass();
            T.Weight   = L.Weight;
            Temp.Add(T);
        }

        double Sum = 0.0;
        for (const auto& T : Temp) { Sum += FMath::Max(0.0f, T.Weight); }
        if (Sum <= 0.0) return nullptr;

        double Pick = Rng.FRand() * Sum;
        for (const auto& T : Temp)
        {
            const double W = FMath::Max(0.0f, T.Weight);
            if (Pick <= W) return &Temp[&T - Temp.GetData()];
            Pick -= W;
        }
        return &Temp.Last();
    }

    return nullptr;
}

FDungeonResolvedParams URogueFloorConfigData::ResolveParamsFor(const FDungeonTemplateConfig& In) const
{
    FDungeonResolvedParams Out;

    Out.Width = Width;
    Out.Height = Height;
    Out.CellSizeUU = CellSizeUU;
    Out.RoomMargin = RoomMargin;
    Out.OuterMargin = OuterMargin;
    Out.MaxReroll = MaxReroll;

    Out.MinRoomSize = MinRoomSize;
    Out.MaxRoomSize = MaxRoomSize;
    Out.MinRooms    = MinRooms;
    Out.MaxRooms    = MaxRooms;
    Out.StopSplitProbability = StopSplitProbability;
    Out.ExtraConnectorChance = ExtraConnectorChance;
    Out.ReachabilityThreshold = ReachabilityThreshold;
    Out.MinDoorSpacing = MinDoorSpacing;

    if (In.RoomSize.bOverride)
    {
        Out.MinRoomSize = In.RoomSize.Min;
        Out.MaxRoomSize = In.RoomSize.Max;
    }
    if (In.Rooms.bOverride)
    {
        Out.MinRooms = In.Rooms.Min;
        Out.MaxRooms = In.Rooms.Max;
    }
    if (In.StopSplitProbability.bOverride)
    {
        Out.StopSplitProbability = In.StopSplitProbability.Value;
    }
    if (In.ExtraConnectorChance.bOverride)
    {
        Out.ExtraConnectorChance = In.ExtraConnectorChance.Value;
    }
    if (In.ReachabilityThreshold.bOverride)
    {
        Out.ReachabilityThreshold = In.ReachabilityThreshold.Value;
    }
    if (In.MinDoorSpacing.bOverride)
    {
        Out.MinDoorSpacing = In.MinDoorSpacing.Value;
    }

    if (In.Width.bOverride) Out.Width = In.Width.Value;
    if (In.Height.bOverride) Out.Height = In.Height.Value;
    if (In.CellSizeUU.bOverride) Out.CellSizeUU = In.CellSizeUU.Value;
    if (In.RoomMargin.bOverride) Out.RoomMargin = In.RoomMargin.Value;
    if (In.OuterMargin.bOverride) Out.OuterMargin = In.OuterMargin.Value;
    if (In.MaxReroll.bOverride) Out.MaxReroll = In.MaxReroll.Value;

    return Out;
}

void URogueFloorConfigData::MigrateFromLegacy()
{
    if (TemplateConfigs.Num() > 0 || TemplateWeights_Legacy.Num() == 0) return;
    for (const auto& L : TemplateWeights_Legacy)
    {
        FDungeonTemplateConfig T;
        if (L.Template) T.TemplateClass = L.Template->GetClass();
        T.Weight   = L.Weight;
        TemplateConfigs.Add(T);
    }
    Modify();
}
