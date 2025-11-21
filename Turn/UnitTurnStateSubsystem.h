#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UnitTurnStateSubsystem.generated.h"

class AActor;
class APawn;

USTRUCT()
struct FUnitTurnSnapshot
{
	GENERATED_BODY()

	UPROPERTY()
	TWeakObjectPtr<AActor> Actor;
	
	UPROPERTY()
	bool bHasActed = false;
	
	UPROPERTY()
	bool bHasMoved = false;
};

// CodeRevision: INC-2025-1205-R1 (Create UnitTurnStateSubsystem to centralize per-turn unit state tracking) (2025-11-21 23:08)
UCLASS()
class LYRAGAME_API UUnitTurnStateSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void ResetTurn(int32 TurnId);

	void UpdatePlayerPawn(APawn* PlayerPawn);
	void UpdateEnemies(const TArray<AActor*>& Enemies);
	void CopyEnemiesTo(TArray<AActor*>& OutEnemies) const;
	const TArray<TWeakObjectPtr<AActor>>& GetCachedEnemyRefs() const { return CachedEnemies; }

	void MarkUnitAsActed(AActor* Unit);
	bool HasUnitActed(AActor* Unit) const;
	void MarkUnitAsMoved(AActor* Unit);
	bool HasUnitMoved(AActor* Unit) const;
	AActor* GetNextUnitToAct() const;

private:
	void EnsureUnitState(AActor* Unit);

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AActor>> CachedEnemies;

	UPROPERTY(Transient)
	TWeakObjectPtr<APawn> CachedPlayerPawn;

	UPROPERTY(Transient)
	TMap<TWeakObjectPtr<AActor>, FUnitTurnSnapshot> UnitStates;

	int32 CurrentTurnId = INDEX_NONE;
};
