// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "NativeGameplayTags.h"

namespace RogueGameplayTags
{
    // Phase tags -------------------------------------------------------------
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Phase_Turn_Init);
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Phase_Player_WaitInput);
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Phase_Player_ResolveCommand);
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Phase_Reaction_Precheck);
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Phase_Simul_Move);
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Phase_Enemy_IntentBuild);
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Phase_Enemy_MoveCommit);
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Phase_Enemy_AttackCommit);
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Phase_Turn_Cleanup);
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Phase_Turn_Advance);
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Phase_Turn_PostAdvance);
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Phase_Ally_IntentBuild);
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Phase_Ally_ActionExecute);

    // Shared gates & states --------------------------------------------------
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Gate_Input_Open);
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Action_InProgress);

	// Input tags -------------------------------------------------------------
	LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Move);
	LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Attack);
	LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Turn);

	// CodeRevision: INC-2025-1134-R1 (Add explicit player command tags for attack dispatch) (2025-12-13 09:30)
	LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Player_Attack);

    // AI intent tags ---------------------------------------------------------
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(AI_Intent_Move);
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(AI_Intent_Attack);
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(AI_Intent_Wait);

    // Ability tags -----------------------------------------------------------
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Attack);
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Wait);
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Melee);
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Move);
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Move_Completed);
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Move_Chase);
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Attack_Melee);
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Attack_Completed);
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Category_Attack);
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_MeleeAttack);

    // Event tags -------------------------------------------------------------
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Move_Finished);
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Attack_Finished);
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Dungeon_Step);

    // Action tags ------------------------------------------------------------
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Action_Move);
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Action_Attack);
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Action_Follow);
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Action_Wait);
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Action_FreeRoam);

    // Gameplay event tags ----------------------------------------------------
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Gameplay_Event_Turn_Ability_Completed);
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Gameplay_Event_Turn_Ability_Started);
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayEvent_Enemy_ActionFinished);
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayEvent_Turn_StartMove);
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayEvent_Intent_Move);
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayEvent_Intent_Attack);
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayEvent_Intent_Wait);

// State tags -------------------------------------------------------------
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Ability_Executing);
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Presentation_Dash);
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Moving);

    // Effect tags ------------------------------------------------------------
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Effect_Turn_AbilityTimeout);

    // Ally command tags ------------------------------------------------------
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(AllyCommand_Follow);
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(AllyCommand_StayHere);
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(AllyCommand_FreeRoam);
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(AllyCommand_NoAbility);
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(AllyCommand_Auto);

    // Faction tags -----------------------------------------------------------
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Faction_Enemy);

    // SetByCaller tags -------------------------------------------------------
    LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Damage);
}
