// =============================================================================
// AllyTurnDataSubsystem.cpp
// =============================================================================

#include "AI/Ally/AllyTurnDataSubsystem.h"
#include "AI/Ally/AllyThinkerBase.h"
#include "Turn/TurnSystemTypes.h"

// ���O�J�e�S����`
DEFINE_LOG_CATEGORY(LogAllyTurnData);

//------------------------------------------------------------------------------
// ������
//------------------------------------------------------------------------------

void UAllyTurnDataSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    Allies.Reset();
    Intents.Reset();
    AllyCommands.Reset();

    UE_LOG(LogAllyTurnData, Log, TEXT("[AllyTurnData] Subsystem Initialized"));
}

void UAllyTurnDataSubsystem::Deinitialize()
{
    Allies.Reset();
    Intents.Reset();
    AllyCommands.Reset();

    UE_LOG(LogAllyTurnData, Log, TEXT("[AllyTurnData] Subsystem Deinitialized"));

    Super::Deinitialize();
}

//------------------------------------------------------------------------------
// �����w��p�[
//------------------------------------------------------------------------------

int32 UAllyTurnDataSubsystem::FindAllyIndex(AActor* AllyActor) const
{
    if (!AllyActor)
    {
        return INDEX_NONE;
    }

    for (int32 i = 0; i < Allies.Num(); ++i)
    {
        if (Allies[i] == AllyActor)
        {
            return i;
        }
    }

    return INDEX_NONE;
}

//------------------------------------------------------------------------------
// �o�^�Ǘ�
//------------------------------------------------------------------------------

void UAllyTurnDataSubsystem::RegisterAlly(AActor* AllyActor, EAllyCommand InitialCommand)
{
    if (!AllyActor)
    {
        UE_LOG(LogAllyTurnData, Warning, TEXT("[AllyTurnData] RegisterAlly: AllyActor is nullptr"));
        return;
    }

    if (FindAllyIndex(AllyActor) != INDEX_NONE)
    {
        UE_LOG(LogAllyTurnData, Warning, TEXT("[AllyTurnData] RegisterAlly: %s already registered"),
            *AllyActor->GetName());
        return;
    }

    Allies.Add(AllyActor);
    AllyCommands.Add(AllyActor, InitialCommand);
    Intents.AddDefaulted(1);

    UE_LOG(LogAllyTurnData, Verbose, TEXT("[AllyTurnData] RegisterAlly: %s (Command: %d)"),
        *AllyActor->GetName(), static_cast<int32>(InitialCommand));

    OnAllyRegistered(AllyActor);
}

void UAllyTurnDataSubsystem::UnregisterAlly(AActor* AllyActor)
{
    const int32 Index = FindAllyIndex(AllyActor);
    if (Index == INDEX_NONE)
    {
        UE_LOG(LogAllyTurnData, Warning, TEXT("[AllyTurnData] UnregisterAlly: %s not found"),
            AllyActor ? *AllyActor->GetName() : TEXT("nullptr"));
        return;
    }

    Allies.RemoveAt(Index);
    Intents.RemoveAt(Index);
    AllyCommands.Remove(AllyActor);

    UE_LOG(LogAllyTurnData, Verbose, TEXT("[AllyTurnData] UnregisterAlly: %s"),
        *AllyActor->GetName());

    OnAllyUnregistered(AllyActor);
}

//------------------------------------------------------------------------------
// �R�}���h����
//------------------------------------------------------------------------------

void UAllyTurnDataSubsystem::SetAllyCommand(AActor* AllyActor, EAllyCommand Command)
{
    if (!AllyActor)
    {
        UE_LOG(LogAllyTurnData, Warning, TEXT("[AllyTurnData] SetAllyCommand: AllyActor is nullptr"));
        return;
    }

    if (EAllyCommand* Found = AllyCommands.Find(AllyActor))
    {
        const EAllyCommand Old = *Found;
        *Found = Command;

        UE_LOG(LogAllyTurnData, Verbose, TEXT("[AllyTurnData] SetAllyCommand: %s OldCommand=%d NewCommand=%d"),
            *AllyActor->GetName(), static_cast<int32>(Old), static_cast<int32>(Command));

        OnAllyCommandChanged(AllyActor, Old, Command);
    }
    else
    {
        AllyCommands.Add(AllyActor, Command);

        UE_LOG(LogAllyTurnData, Verbose, TEXT("[AllyTurnData] SetAllyCommand: %s NewCommand=%d (initial)"),
            *AllyActor->GetName(), static_cast<int32>(Command));

        OnAllyCommandChanged(AllyActor, EAllyCommand::Follow, Command);
    }
}

EAllyCommand UAllyTurnDataSubsystem::GetAllyCommand(AActor* AllyActor) const
{
    if (!AllyActor)
    {
        return EAllyCommand::Follow;
    }

    if (const EAllyCommand* Found = AllyCommands.Find(AllyActor))
    {
        return *Found;
    }

    return EAllyCommand::Follow;
}

void UAllyTurnDataSubsystem::SetAllAllyCommands(EAllyCommand Command)
{
    UE_LOG(LogAllyTurnData, Verbose, TEXT("[AllyTurnData] SetAllAllyCommands: Command=%d for %d allies"),
        static_cast<int32>(Command), Allies.Num());

    for (TPair<TObjectPtr<AActor>, EAllyCommand>& Pair : AllyCommands)
    {
        Pair.Value = Command;
    }
}

//------------------------------------------------------------------------------
// �Ӑ}�\�z
//------------------------------------------------------------------------------

void UAllyTurnDataSubsystem::BuildAllAllyIntents_Implementation(const FTurnContext& Context)
{
    PreBuildIntents(Context);

    // �����i�T�C�Y�����j
    if (Intents.Num() != Allies.Num())
    {
        Intents.SetNum(Allies.Num());
    }

    for (int32 i = 0; i < Allies.Num(); ++i)
    {
        AActor* Ally = Allies[i];
        if (!Ally)
        {
            Intents[i] = FAllyIntent{};
            continue;
        }

        // �v���C���[�w��������ꍇ�̓X�L�b�v
        if (Intents[i].bIsPlayerDirected)
        {
            UE_LOG(LogAllyTurnData, Verbose, TEXT("[AllyTurnData] BuildAllAllyIntents: %s (Player Directed)"),
                *Ally->GetName());
            continue;
        }

        const EAllyCommand Command = GetAllyCommand(Ally);

        UAllyThinkerBase* Thinker = Ally->FindComponentByClass<UAllyThinkerBase>();
        FAllyIntent Out;
        if (Thinker)
        {
            Thinker->ComputeAllyIntent(Context, Command, /*out*/Out);
            UE_LOG(LogAllyTurnData, Verbose, TEXT("[AllyTurnData] BuildAllAllyIntents: %s Command=%d Action=%s"),
                *Ally->GetName(), static_cast<int32>(Command), *Out.ActionType.ToString());
        }
        else
        {
            Out.ActionType = FGameplayTag(); // ����: �������Ȃ�
            Out.Score = 0.0f;

            UE_LOG(LogAllyTurnData, Warning, TEXT("[AllyTurnData] BuildAllAllyIntents: %s has no AllyThinkerBase"),
                *Ally->GetName());
        }

        Intents[i] = Out;
    }

    PostBuildIntents(Context);
    UpdateAllyStates(Context);
}

void UAllyTurnDataSubsystem::SetPlayerDirectedIntent(AActor* AllyActor, const FAllyIntent& Intent)
{
    const int32 Index = FindAllyIndex(AllyActor);
    if (Index == INDEX_NONE)
    {
        UE_LOG(LogAllyTurnData, Warning, TEXT("[AllyTurnData] SetPlayerDirectedIntent: Actor %s not registered"),
            AllyActor ? *AllyActor->GetName() : TEXT("nullptr"));
        return;
    }

    FAllyIntent Copy = Intent;
    Copy.bIsPlayerDirected = true;
    Intents[Index] = Copy;

    UE_LOG(LogAllyTurnData, Verbose, TEXT("[AllyTurnData] SetPlayerDirectedIntent: %s Action=%s"),
        *AllyActor->GetName(), *Intent.ActionType.ToString());
}
