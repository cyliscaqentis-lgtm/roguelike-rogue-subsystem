// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rogue/Utility/RogueGameplayTags.h"

namespace RogueGameplayTags
{
    // Phase tags -------------------------------------------------------------
    UE_DEFINE_GAMEPLAY_TAG(Phase_Turn_Init, "Phase.Turn.Init");
    UE_DEFINE_GAMEPLAY_TAG(Phase_Player_WaitInput, "Phase.Player.WaitInput");
    UE_DEFINE_GAMEPLAY_TAG(Phase_Player_ResolveCommand, "Phase.Player.ResolveCommand");
    UE_DEFINE_GAMEPLAY_TAG(Phase_Reaction_Precheck, "Phase.Reaction.Precheck");
    UE_DEFINE_GAMEPLAY_TAG(Phase_Simul_Move, "Phase.Simul.Move");
    UE_DEFINE_GAMEPLAY_TAG(Phase_Enemy_IntentBuild, "Phase.Enemy.IntentBuild");
    UE_DEFINE_GAMEPLAY_TAG(Phase_Enemy_MoveCommit, "Phase.Enemy.MoveCommit");
    UE_DEFINE_GAMEPLAY_TAG(Phase_Enemy_AttackCommit, "Phase.Enemy.AttackCommit");
    UE_DEFINE_GAMEPLAY_TAG(Phase_Turn_Cleanup, "Phase.Turn.Cleanup");
    UE_DEFINE_GAMEPLAY_TAG(Phase_Turn_Advance, "Phase.Turn.Advance");
    UE_DEFINE_GAMEPLAY_TAG(Phase_Turn_PostAdvance, "Phase.Turn.PostAdvance");
    UE_DEFINE_GAMEPLAY_TAG(Phase_Ally_IntentBuild, "Phase.Ally.IntentBuild");
    UE_DEFINE_GAMEPLAY_TAG(Phase_Ally_ActionExecute, "Phase.Ally.ActionExecute");

    // Shared gates & states --------------------------------------------------
    UE_DEFINE_GAMEPLAY_TAG(Gate_Input_Open, "Gate.Input.Open");
    UE_DEFINE_GAMEPLAY_TAG(State_Action_InProgress, "State.Action.InProgress");

    // Input tags -------------------------------------------------------------
    UE_DEFINE_GAMEPLAY_TAG(InputTag_Move, "InputTag.Move");
    UE_DEFINE_GAMEPLAY_TAG(InputTag_Attack, "InputTag.Attack");
    UE_DEFINE_GAMEPLAY_TAG(InputTag_Turn, "InputTag.Turn");

    // AI intent tags ---------------------------------------------------------
    UE_DEFINE_GAMEPLAY_TAG(AI_Intent_Move, "AI.Intent.Move");
    UE_DEFINE_GAMEPLAY_TAG(AI_Intent_Attack, "AI.Intent.Attack");
    UE_DEFINE_GAMEPLAY_TAG(AI_Intent_Wait, "AI.Intent.Wait");

    // Ability tags -----------------------------------------------------------
    UE_DEFINE_GAMEPLAY_TAG(Ability_Attack, "Ability.Attack");
    UE_DEFINE_GAMEPLAY_TAG(Ability_Wait, "Ability.Wait");
    UE_DEFINE_GAMEPLAY_TAG(Ability_Melee, "Ability.Melee");
    UE_DEFINE_GAMEPLAY_TAG(Ability_Move, "Ability.Move");
    UE_DEFINE_GAMEPLAY_TAG(Ability_Move_Completed, "Ability.Move.Completed");
    UE_DEFINE_GAMEPLAY_TAG(Ability_Move_Chase, "Ability.Move.Chase");
    UE_DEFINE_GAMEPLAY_TAG(Ability_Attack_Melee, "Ability.Attack.Melee");
    UE_DEFINE_GAMEPLAY_TAG(Ability_Attack_Completed, "Ability.Attack.Completed");
    UE_DEFINE_GAMEPLAY_TAG(Ability_Category_Attack, "Ability.Category.Attack");
    UE_DEFINE_GAMEPLAY_TAG(Ability_MeleeAttack, "Ability.MeleeAttack");

    // Event tags -------------------------------------------------------------
    UE_DEFINE_GAMEPLAY_TAG(Event_Move_Finished, "Event.Move.Finished");
    UE_DEFINE_GAMEPLAY_TAG(Event_Attack_Finished, "Event.Attack.Finished");
    UE_DEFINE_GAMEPLAY_TAG(Event_Dungeon_Step, "Event.Dungeon.Step");
    UE_DEFINE_GAMEPLAY_TAG(Event_Turn_ExecuteMove, "Event.Turn.ExecuteMove");

    // Action tags ------------------------------------------------------------
    UE_DEFINE_GAMEPLAY_TAG(Action_Move, "Action.Move");
    UE_DEFINE_GAMEPLAY_TAG(Action_Attack, "Action.Attack");
    UE_DEFINE_GAMEPLAY_TAG(Action_Follow, "Action.Follow");
    UE_DEFINE_GAMEPLAY_TAG(Action_Wait, "Action.Wait");
    UE_DEFINE_GAMEPLAY_TAG(Action_FreeRoam, "Action.FreeRoam");

    // Gameplay event tags ----------------------------------------------------
    UE_DEFINE_GAMEPLAY_TAG(Gameplay_Event_Turn_Ability_Completed, "Gameplay.Event.Turn.Ability.Completed");
    UE_DEFINE_GAMEPLAY_TAG(Gameplay_Event_Turn_Ability_Started, "Gameplay.Event.Turn.Ability.Started");
    UE_DEFINE_GAMEPLAY_TAG(GameplayEvent_Enemy_ActionFinished, "GameplayEvent.Enemy.ActionFinished");
    UE_DEFINE_GAMEPLAY_TAG(GameplayEvent_Turn_StartMove, "Turn.StartMove");
    UE_DEFINE_GAMEPLAY_TAG(GameplayEvent_Intent_Move, "GameplayEvent.Intent.Move");
    UE_DEFINE_GAMEPLAY_TAG(GameplayEvent_Intent_Attack, "GameplayEvent.Intent.Attack");
    UE_DEFINE_GAMEPLAY_TAG(GameplayEvent_Intent_Wait, "GameplayEvent.Intent.Wait");

    // State tags -------------------------------------------------------------
    UE_DEFINE_GAMEPLAY_TAG(State_Ability_Executing, "State.Ability.Executing");
    UE_DEFINE_GAMEPLAY_TAG(State_Presentation_Dash, "State.Presentation.Dash");
    UE_DEFINE_GAMEPLAY_TAG(State_Moving, "State.Moving");

    // Effect tags ------------------------------------------------------------
    UE_DEFINE_GAMEPLAY_TAG(Effect_Turn_AbilityTimeout, "Effect.Turn.AbilityTimeout");

    // Ally command tags ------------------------------------------------------
    UE_DEFINE_GAMEPLAY_TAG(AllyCommand_Follow, "AllyCommand.Follow");
    UE_DEFINE_GAMEPLAY_TAG(AllyCommand_StayHere, "AllyCommand.StayHere");
    UE_DEFINE_GAMEPLAY_TAG(AllyCommand_FreeRoam, "AllyCommand.FreeRoam");
    UE_DEFINE_GAMEPLAY_TAG(AllyCommand_NoAbility, "AllyCommand.NoAbility");
    UE_DEFINE_GAMEPLAY_TAG(AllyCommand_Auto, "AllyCommand.Auto");

    // Faction tags -----------------------------------------------------------
    UE_DEFINE_GAMEPLAY_TAG(Faction_Enemy, "Faction.Enemy");

    // SetByCaller tags -------------------------------------------------------
    UE_DEFINE_GAMEPLAY_TAG(SetByCaller_Damage, "SetByCaller.Damage");
}
