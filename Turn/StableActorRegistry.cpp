// Copyright Epic Games, Inc. All Rights Reserved.

#include "StableActorRegistry.h"
#include "Engine/World.h"

void UStableActorRegistry::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    ActorToID.Reset();
    IDToActor.Reset();
    UsedGUIDs.Reset();
    NextGenerationOrder = 0;

    UE_LOG(LogTemp, Log, TEXT("[StableActorRegistry] Initialized"));
}

void UStableActorRegistry::Deinitialize()
{
    Super::Deinitialize();

    UE_LOG(LogTemp, Log, TEXT("[StableActorRegistry] Deinitialized. Total: %d"), ActorToID.Num());
}

FStableActorID UStableActorRegistry::RegisterActor(AActor* Actor)
{
    if (!Actor)
    {
        UE_LOG(LogTemp, Warning, TEXT("[StableActorRegistry] RegisterActor: null Actor"));
        return FStableActorID();
    }

    if (FStableActorID* ExistingID = ActorToID.Find(Actor))
    {
        return *ExistingID;
    }

    FStableActorID NewID;
    NewID.PersistentGUID = FGuid::NewGuid();
    NewID.GenerationOrder = NextGenerationOrder++;

    while (UsedGUIDs.Contains(NewID.PersistentGUID))
    {
        NewID.PersistentGUID = FGuid::NewGuid();
    }

    UsedGUIDs.Add(NewID.PersistentGUID);
    ActorToID.Add(Actor, NewID);
    IDToActor.Add(NewID, Actor);

    UE_LOG(LogTemp, Log, TEXT("[StableActorRegistry] Registered: %s -> %s"),
        *Actor->GetName(), *NewID.ToString());

    return NewID;
}

void UStableActorRegistry::RestoreFromSave(AActor* Actor, const FStableActorID& SavedID)
{
    if (!Actor || !SavedID.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("[StableActorRegistry] RestoreFromSave: Invalid"));
        return;
    }

    ActorToID.Add(Actor, SavedID);
    IDToActor.Add(SavedID, Actor);
    UsedGUIDs.Add(SavedID.PersistentGUID);

    NextGenerationOrder = FMath::Max(NextGenerationOrder, SavedID.GenerationOrder + 1);

    UE_LOG(LogTemp, Log, TEXT("[StableActorRegistry] Restored: %s -> %s"),
        *Actor->GetName(), *SavedID.ToString());
}

FStableActorID UStableActorRegistry::GetStableID(AActor* Actor) const
{
    if (const FStableActorID* ID = ActorToID.Find(Actor))
    {
        return *ID;
    }
    return FStableActorID();
}

AActor* UStableActorRegistry::GetActorByID(const FStableActorID& StableID) const
{
    if (const TWeakObjectPtr<AActor>* WeakActor = IDToActor.Find(StableID))
    {
        return WeakActor->Get();
    }
    return nullptr;
}

void UStableActorRegistry::UnregisterActor(AActor* Actor)
{
    if (!Actor)
    {
        return;
    }

    if (FStableActorID* ID = ActorToID.Find(Actor))
    {
        IDToActor.Remove(*ID);
        ActorToID.Remove(Actor);

        UE_LOG(LogTemp, Log, TEXT("[StableActorRegistry] Unregistered: %s"), *Actor->GetName());
    }
}
