#include "Rogue/Data/DungeonPresetTemplates.h"
#include "Rogue/Grid/DungeonFloorGenerator.h"

void UDungeonTemplate_NormalBSP::Generate_Implementation(ADungeonFloorGenerator* Generator, const FDungeonResolvedParams& Params, FRandomStream& Rng)

{

    if (Generator)

    {

        Generator->Make_NormalBSP(Rng, Params);

    }

}



void UDungeonTemplate_LargeHall::Generate_Implementation(ADungeonFloorGenerator* Generator, const FDungeonResolvedParams& Params, FRandomStream& Rng)

{

    if (Generator)

    {

        Generator->Make_LargeHall(Rng, Params);

    }

}



void UDungeonTemplate_FourQuads::Generate_Implementation(ADungeonFloorGenerator* Generator, const FDungeonResolvedParams& Params, FRandomStream& Rng)

{

    if (Generator)

    {

        Generator->Make_FourQuads(Rng, Params);

    }

}



void UDungeonTemplate_CentralCross::Generate_Implementation(ADungeonFloorGenerator* Generator, const FDungeonResolvedParams& Params, FRandomStream& Rng)

{

    if (Generator)

    {

        Generator->Make_CentralCrossWithMiniRooms(Rng, Params);

    }

}
