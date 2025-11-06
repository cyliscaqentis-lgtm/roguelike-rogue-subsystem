#pragma once

#include "CoreMinimal.h"
#include "UnitStatBlock.generated.h"

/** BP の UnitStatBlock を 1:1 マッピング */
USTRUCT(BlueprintType)
struct LYRAGAME_API FUnitStatBlock
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Strength = 5;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Intelligence = 5;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Dexterity = 5;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Constitution = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite) float MaxHealth = 50.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float CurrentHealth = 50.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float MaxPower = 20.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float CurrentPower = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite) float MaxSpeed = 600.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float CurrentSpeed = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite) float MaxSightRange = 1000.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float CurrentSightRange = 1000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bCanAct = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bHasActed = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bIsDisabled = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bCanMove = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bHasMoved = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bIsRooted = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Team = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 RangePerMove = 3;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 NumberOfMoves = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 CurrentTotalMovementRnage = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 MeleeBaseAttack = 10;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 RangedBaseAttack = 8;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 MagicBaseAttack = 12;

	UPROPERTY(EditAnywhere, BlueprintReadWrite) float MeleeBaseDamage = 10.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float RangedBaseDamage = 8.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float MagicBaseDamage = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite) float BaseDamageAbsorb = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float BaseDamageAvoidance = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float BaseMagicResist = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float BaseMagicPenetration = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite) float CurrentDamageAbsorb = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float CurrentDamageAvoidance = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float CurrentMagicResist = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float CurrentMagicPenetration = 0.f;
};
