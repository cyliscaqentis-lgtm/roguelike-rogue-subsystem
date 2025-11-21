#include "Turn/UnitTurnStateSubsystem.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"

void UUnitTurnStateSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ResetTurn(INDEX_NONE);
}

void UUnitTurnStateSubsystem::Deinitialize()
{
	ResetTurn(INDEX_NONE);
	Super::Deinitialize();
}

void UUnitTurnStateSubsystem::ResetTurn(int32 TurnId)
{
	// CodeRevision: INC-2025-1205-R1 (Reset cached enemy/state collections at the start of each turn) (2025-11-21 23:08)
	CurrentTurnId = TurnId;
	CachedEnemies.Reset();
	UnitStates.Reset();
	CachedPlayerPawn.Reset();
}

void UUnitTurnStateSubsystem::UpdatePlayerPawn(APawn* PlayerPawn)
{
	CachedPlayerPawn = PlayerPawn;
	EnsureUnitState(PlayerPawn);
}

void UUnitTurnStateSubsystem::UpdateEnemies(const TArray<AActor*>& Enemies)
{
	CachedEnemies.Reset();
	for (AActor* Enemy : Enemies)
	{
		if (Enemy)
		{
			CachedEnemies.Add(Enemy);
			EnsureUnitState(Enemy);
		}
	}
}

void UUnitTurnStateSubsystem::CopyEnemiesTo(TArray<AActor*>& OutEnemies) const
{
	OutEnemies.Reset();
	for (const TWeakObjectPtr<AActor>& EnemyRef : CachedEnemies)
	{
		if (AActor* Enemy = EnemyRef.Get())
		{
			OutEnemies.Add(Enemy);
		}
	}
}

void UUnitTurnStateSubsystem::MarkUnitAsActed(AActor* Unit)
{
	if (!Unit)
	{
		return;
	}

	const TWeakObjectPtr<AActor> Key(Unit);
	FUnitTurnSnapshot* Snapshot = UnitStates.Find(Key);
	if (!Snapshot)
	{
		EnsureUnitState(Unit);
		Snapshot = UnitStates.Find(Key);
	}

	if (Snapshot)
	{
		Snapshot->bHasActed = true;
	}
}

bool UUnitTurnStateSubsystem::HasUnitActed(AActor* Unit) const
{
	if (!Unit)
	{
		return false;
	}

	const TWeakObjectPtr<AActor> Key(Unit);
	if (const FUnitTurnSnapshot* Snapshot = UnitStates.Find(Key))
	{
		return Snapshot->bHasActed;
	}

	return false;
}

void UUnitTurnStateSubsystem::MarkUnitAsMoved(AActor* Unit)
{
	if (!Unit)
	{
		return;
	}

	const TWeakObjectPtr<AActor> Key(Unit);
	FUnitTurnSnapshot* Snapshot = UnitStates.Find(Key);
	if (!Snapshot)
	{
		EnsureUnitState(Unit);
		Snapshot = UnitStates.Find(Key);
	}

	if (Snapshot)
	{
		Snapshot->bHasMoved = true;
	}
}

bool UUnitTurnStateSubsystem::HasUnitMoved(AActor* Unit) const
{
	if (!Unit)
	{
		return false;
	}

	const TWeakObjectPtr<AActor> Key(Unit);
	if (const FUnitTurnSnapshot* Snapshot = UnitStates.Find(Key))
	{
		return Snapshot->bHasMoved;
	}

	return false;
}

AActor* UUnitTurnStateSubsystem::GetNextUnitToAct() const
{
	for (const TPair<TWeakObjectPtr<AActor>, FUnitTurnSnapshot>& Pair : UnitStates)
	{
		if (const AActor* Actor = Pair.Value.Actor.Get())
		{
			if (!Pair.Value.bHasActed)
			{
				return const_cast<AActor*>(Actor);
			}
		}
	}
	return nullptr;
}

void UUnitTurnStateSubsystem::EnsureUnitState(AActor* Unit)
{
	if (!Unit)
	{
		return;
	}

	const TWeakObjectPtr<AActor> Key(Unit);
	FUnitTurnSnapshot& Snapshot = UnitStates.FindOrAdd(Key);
	Snapshot.Actor = Unit;
}
