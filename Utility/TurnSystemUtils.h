#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"

/**
 * TurnSystemUtils
 * 
 * Static utility functions for turn system operations.
 * Moved from AGameTurnManagerBase to reduce class complexity.
 */
namespace TurnSystemUtils
{
	/**
	 * Get the player's AbilitySystemComponent
	 * @param WorldContext World context object (usually 'this' from calling class)
	 * @return Player's ASC, or nullptr if not found
	 */
	LYRAGAME_API UAbilitySystemComponent* GetPlayerASC(const UObject* WorldContext);
}
