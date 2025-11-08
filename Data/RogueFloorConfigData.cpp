#include "Data/RogueFloorConfigData.h"
#include "Data/DungeonTemplateAsset.h"
#include "Data/DungeonPresetTemplates.h"
#include "Math/RandomStream.h"

namespace
{
    FDungeonTemplateConfig MakeTemplateConfig(
        TSubclassOf<UDungeonTemplateAsset> TemplateClass,
        float Weight,
        int32 MinRooms,
        int32 MaxRooms,
        int32 MinRoomSize,
        int32 MaxRoomSize,
        float ExtraConnector = -1.f,
        float StopSplitProbability = -1.f)
    {
        FDungeonTemplateConfig Config;
        Config.TemplateClass = TemplateClass;
        Config.Weight = Weight;
        Config.Rooms.bOverride = true;
        Config.Rooms.Min = MinRooms;
        Config.Rooms.Max = MaxRooms;
        Config.RoomSize.bOverride = true;
        Config.RoomSize.Min = MinRoomSize;
        Config.RoomSize.Max = MaxRoomSize;

        if (ExtraConnector >= 0.0f)
        {
            Config.ExtraConnectorChance.bOverride = true;
            Config.ExtraConnectorChance.Value = ExtraConnector;
        }
        if (StopSplitProbability >= 0.0f)
        {
            Config.StopSplitProbability.bOverride = true;
            Config.StopSplitProbability.Value = StopSplitProbability;
        }

        return Config;
    }
}

URogueFloorConfigData::URogueFloorConfigData()
{
    EnsureDefaultTemplates();
}

void URogueFloorConfigData::PostLoad()
{
    Super::PostLoad();
    EnsureDefaultTemplates();
}

const FDungeonTemplateConfig* URogueFloorConfigData::PickTemplateConfig(FRandomStream& Rng) const
{
    const_cast<URogueFloorConfigData*>(this)->EnsureDefaultTemplates();

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

    SanitizeResolvedParams(Out);
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

void URogueFloorConfigData::EnsureDefaultTemplates()
{
    if (TemplateConfigs.Num() > 0 || TemplateWeights_Legacy.Num() > 0)
    {
        return;
    }

    TemplateConfigs.Reserve(4);
    TemplateConfigs.Add(MakeTemplateConfig(UDungeonTemplate_NormalBSP::StaticClass(), 0.45f, 12, 28, 5, 12, 0.2f, 0.1f));
    TemplateConfigs.Add(MakeTemplateConfig(UDungeonTemplate_LargeHall::StaticClass(), 0.2f, 6, 12, 6, 10, 0.05f));
    TemplateConfigs.Add(MakeTemplateConfig(UDungeonTemplate_FourQuads::StaticClass(), 0.2f, 4, 8, 6, 12, 0.15f));
    TemplateConfigs.Add(MakeTemplateConfig(UDungeonTemplate_CentralCross::StaticClass(), 0.15f, 6, 14, 4, 9, 0.25f));
}

void URogueFloorConfigData::SanitizeResolvedParams(FDungeonResolvedParams& Params) const
{
    Params.Width = FMath::Clamp(Params.Width, 16, 512);
    Params.Height = FMath::Clamp(Params.Height, 16, 512);
    Params.CellSizeUU = FMath::Clamp(Params.CellSizeUU, 50, 500);

    Params.MinRoomSize = FMath::Clamp(Params.MinRoomSize, 2, 64);
    Params.MaxRoomSize = FMath::Clamp(Params.MaxRoomSize, Params.MinRoomSize, 96);

    Params.MinRooms = FMath::Clamp(Params.MinRooms, 1, 256);
    Params.MaxRooms = FMath::Clamp(FMath::Max(Params.MaxRooms, Params.MinRooms), Params.MinRooms, 512);

    Params.RoomMargin = FMath::Clamp(Params.RoomMargin, 0, 16);
    Params.OuterMargin = FMath::Clamp(Params.OuterMargin, 0, FMath::Min(Params.Width, Params.Height) / 3);

    Params.StopSplitProbability = FMath::Clamp(Params.StopSplitProbability, 0.0f, 1.0f);
    Params.ExtraConnectorChance = FMath::Clamp(Params.ExtraConnectorChance, 0.0f, 1.0f);
    Params.ReachabilityThreshold = FMath::Clamp(Params.ReachabilityThreshold, 0.3f, 1.0f);

    Params.MaxReroll = FMath::Clamp(Params.MaxReroll, 1, 64);
    Params.MinDoorSpacing = FMath::Clamp(Params.MinDoorSpacing, 1, 16);
}
