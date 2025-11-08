#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/DungeonTemplateTypes.h"
#include "DungeonFloorGenerator.generated.h"

// Forward declarations for the new data-driven system
struct FDungeonResolvedParams;
class URogueFloorConfigData;
class UDungeonTemplateAsset;
struct FRandomStream;

UENUM(BlueprintType)
enum class ECellType : uint8
{
    Wall, Floor, Corridor, Room, Door, StairUp, StairDown
};

USTRUCT(BlueprintType)
struct FIntRectLite
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 X0=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Y0=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 X1=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Y1=0;

    FIntRectLite(){}
    FIntRectLite(int32 InX0,int32 InY0,int32 InX1,int32 InY1):X0(InX0),Y0(InY0),X1(InX1),Y1(InY1){}

    FORCEINLINE int32 W() const { return X1 - X0 + 1; }
    FORCEINLINE int32 H() const { return Y1 - Y0 + 1; }
    FORCEINLINE FIntPoint Center() const { return { X0 + W()/2, Y0 + H()/2 }; }
    FORCEINLINE bool IsValid() const { return X0<=X1 && Y0<=Y1; }
    FORCEINLINE bool Contains(int32 X,int32 Y) const { return X>=X0 && X<=X1 && Y>=Y0 && Y<=Y1; }

    bool OverlapsWith(const FIntRectLite& Other, int32 Margin=0) const
    {
        const int32 Ax0 = X0 - Margin, Ax1 = X1 + Margin;
        const int32 Ay0 = Y0 - Margin, Ay1 = Y1 + Margin;
        const int32 Bx0 = Other.X0 - Margin, Bx1 = Other.X1 + Margin;
        const int32 By0 = Other.Y0 - Margin, By1 = Other.Y1 + Margin;
        const bool sepX = (Ax1 < Bx0) || (Bx1 < Ax0);
        const bool sepY = (Ay1 < By0) || (By1 < Ay0);
        return !(sepX || sepY);
    }
};

USTRUCT(BlueprintType)
struct FDungeonGenParams
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Width  = 64;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Height = 64;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 CellSizeUU = 100;

    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 MinRoomSize = 4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 MaxRoomSize = 10;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 MinRooms    = 10;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 MaxRooms    = 24;

    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 OuterMargin = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 RoomMargin  = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float StopSplitProbability = 0.15f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float ExtraConnectorChance = 0.10f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite) float ReachabilityThreshold = 0.90f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 MaxReroll = 8;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 MinDoorSpacing = 2;
};

UCLASS(BlueprintType)
class LYRAGAME_API ADungeonFloorGenerator : public AActor
{
    GENERATED_BODY()

public:
    ADungeonFloorGenerator();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Grid")
    TArray<int32> mGrid;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Grid")
    int32 GridWidth = 64;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Grid")
    int32 GridHeight = 64;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Grid")
    int32 CellSize = 100;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Generation")
    int32 Seed = 12345;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Generation")
    TArray<FMapTemplateWeight> TemplateWeights;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Generation")
    FDungeonGenParams GenParams;

    void Generate(const URogueFloorConfigData* Config, FRandomStream& Rng);

    UFUNCTION(BlueprintCallable, Category="Grid")
    int32 ReturnGridStatus(FVector InputVector) const;

    UFUNCTION(BlueprintCallable, Category="Grid")
    void GridChangeVector(FVector InputVector, int32 Value);

    UFUNCTION(BlueprintCallable, Category="Grid")
    bool IsInside(int32 X, int32 Y) const;

    UFUNCTION(BlueprintCallable, Category="Grid")
    void SetCellXY(int32 X, int32 Y, ECellType Type);

    UFUNCTION(BlueprintCallable, Category="Grid")
    ECellType GetCellXY(int32 X, int32 Y) const;

    UFUNCTION(BlueprintCallable, Category="Grid")
    void FillRect(int32 X, int32 Y, int32 W, int32 H, ECellType Type);

    UFUNCTION(BlueprintCallable, Category="Generation")
    void GetGenerationStats(int32& OutRoomCount, int32& OutWalkableCount, float& OutReachability);

public:
    bool Make_NormalBSP(FRandomStream& RNG, const FDungeonResolvedParams& Params);
    bool Make_LargeHall(FRandomStream& RNG, const FDungeonResolvedParams& Params);
    bool Make_FourQuads(FRandomStream& RNG, const FDungeonResolvedParams& Params);
    bool Make_CentralCrossWithMiniRooms(FRandomStream& RNG, const FDungeonResolvedParams& Params);

private:
    void GenerateWithTemplate(UDungeonTemplateAsset* TemplateAsset, const FDungeonResolvedParams& Params, FRandomStream& Rng);

    int32 LastGeneratedRoomCount = 0;
    int32 LastGeneratedWalkableCount = 0;
    float LastGeneratedReachability = 0.0f;
    EMapTemplate LastUsedTemplate = EMapTemplate::NormalBSP;

    FORCEINLINE bool InBounds(int32 X, int32 Y) const { return (X>=0 && Y>=0 && X<GridWidth && Y<GridHeight); }
    FORCEINLINE int32 Index(int32 X, int32 Y) const { return Y*GridWidth + X; }

    void ResetGrid(ECellType Fill=ECellType::Wall);
    void EnsureOuterWall();

    void CarveLineX(const FIntPoint& A, const FIntPoint& B, ECellType Type);
    void CarveLineY(const FIntPoint& A, const FIntPoint& B, ECellType Type);
    void CarveManhattan_XY(const FIntPoint& A, const FIntPoint& B, ECellType Type);

    void FixDoubleWidthCorridors();
    void AutoPlaceDoors(const FDungeonResolvedParams& Params);
    void PlaceDoorIfBoundary(int32 X, int32 Y, const FDungeonResolvedParams& Params);
    bool IsValidDoorPlacement(int32 X, int32 Y, const FDungeonResolvedParams& Params) const;
    void PlaceStairsFarthestPair();

    void BSP_Split(const FIntRectLite& Root, FRandomStream& RNG, const FDungeonResolvedParams& Params, TArray<FIntRectLite>& OutLeaves);
    FIntRectLite MakeRoomInLeaf(const FIntRectLite& Leaf, FRandomStream& RNG, const FDungeonResolvedParams& Params, const TArray<FIntRectLite>& ExistingRooms) const;

    void ConnectCentersWithMST(const TArray<FIntPoint>& Centers, ECellType CorridorType=ECellType::Corridor);

    bool ValidateReachability(float& OutRatio, const FDungeonResolvedParams& Params);
    bool IsWalkable(int32 X, int32 Y) const;
    int32 CountWalkable() const;
    int32 ComputeRoomClusterCount() const;

    bool GenerateFallbackLayout(const FDungeonResolvedParams& Params, FRandomStream& Rng);
};
