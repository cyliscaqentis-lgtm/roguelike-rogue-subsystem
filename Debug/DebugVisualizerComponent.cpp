// Copyright Epic Games, Inc. All Rights Reserved.

#include "DebugVisualizerComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "../Turn/StableActorRegistry.h"

UDebugVisualizerComponent::UDebugVisualizerComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UDebugVisualizerComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!bEnableDebugDraw)
    {
        return;
    }
}

void UDebugVisualizerComponent::VisualizeReservations(const TArray<FReservationEntry>& Reservations)
{
    if (!bEnableDebugDraw || !GetWorld())
    {
        return;
    }

    for (const FReservationEntry& Entry : Reservations)
    {
        FVector Location = CellToWorld(Entry.Cell);

        FLinearColor Color = FLinearColor::Yellow;
        DrawDebugBox(GetWorld(), Location, FVector(GridSize * 0.4f),
            Color.ToFColor(true), false, 0.1f, 0, 5.0f);

        if (bShowText)
        {
            FString Text = FString::Printf(TEXT("Tier:%d Pri:%d"),
                Entry.ActionTier, Entry.BasePriority);
            DrawDebugText(Location + FVector(0, 0, 50), Text, Color);
        }
    }
}

void UDebugVisualizerComponent::VisualizeResolvedActions(const TArray<FResolvedAction>& Actions)
{
    if (!bEnableDebugDraw || !GetWorld())
    {
        return;
    }

    for (const FResolvedAction& Action : Actions)
    {
        if (Action.bIsWait)
        {
            FVector Location = CellToWorld(Action.NextCell);
            DrawDebugSphere(GetWorld(), Location, GridSize * 0.3f,
                12, FColor::Red, false, 0.1f, 0, 3.0f);

            if (bShowText)
            {
                DrawDebugText(Location + FVector(0, 0, 30),
                    TEXT("WAIT"), FLinearColor::Red);
            }
        }
        else
        {
            FVector Location = CellToWorld(Action.NextCell);
            DrawDebugSphere(GetWorld(), Location, GridSize * 0.3f,
                12, FColor::Green, false, 0.1f, 0, 3.0f);

            if (bShowText)
            {
                DrawDebugText(Location + FVector(0, 0, 30),
                    TEXT("WIN"), FLinearColor::Green);
            }
        }
    }
}

void UDebugVisualizerComponent::VisualizeStableIDs(const TArray<AActor*>& Actors)
{
    if (!bEnableDebugDraw || !GetWorld() || !bShowText)
    {
        return;
    }

    UStableActorRegistry* Registry = GetWorld()->GetSubsystem<UStableActorRegistry>();
    if (!Registry)
    {
        return;
    }

    for (AActor* Actor : Actors)
    {
        if (!Actor)
        {
            continue;
        }

        // ������ �C���FFStableActorID�̎��ۂ̃����o�[�ϐ����g�p ������
        FStableActorID StableID = Registry->GetStableID(Actor);
        FVector Location = Actor->GetActorLocation() + FVector(0, 0, 100);

        // PersistentGUID��GenerationOrder��\��
        FString Text = FString::Printf(TEXT("Gen:%d"), StableID.GenerationOrder);
        DrawDebugText(Location, Text, FLinearColor::White);
    }
}

void UDebugVisualizerComponent::VisualizeDashPath(const TArray<FIntPoint>& Path,
    const FLinearColor& Color)
{
    if (!bEnableDebugDraw || !GetWorld() || Path.Num() == 0)
    {
        return;
    }

    for (int32 i = 0; i < Path.Num(); ++i)
    {
        FVector Location = CellToWorld(Path[i]);

        if (i > 0)
        {
            FVector PrevLocation = CellToWorld(Path[i - 1]);
            DrawDebugLine(GetWorld(), PrevLocation, Location,
                Color.ToFColor(true), false, 0.1f, 0, 3.0f);
        }

        DrawDebugSphere(GetWorld(), Location, GridSize * 0.15f,
            8, Color.ToFColor(true), false, 0.1f, 0, 2.0f);

        if (bShowText)
        {
            DrawDebugText(Location + FVector(0, 0, 20),
                FString::FromInt(i + 1), Color);
        }
    }
}

FVector UDebugVisualizerComponent::CellToWorld(const FIntPoint& Cell) const
{
    return FVector(Cell.X * GridSize, Cell.Y * GridSize, 50.0f);
}

void UDebugVisualizerComponent::DrawDebugText(const FVector& Location, const FString& Text,
    const FLinearColor& Color) const
{
    if (!GetWorld())
    {
        return;
    }

    DrawDebugString(GetWorld(), Location, Text, nullptr,
        Color.ToFColor(true), 0.1f, false, 1.5f);
}
