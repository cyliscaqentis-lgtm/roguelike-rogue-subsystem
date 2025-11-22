#include "Utility/TurnSystemUtils.h"
#include "Player/LyraPlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "AbilitySystemGlobals.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

UAbilitySystemComponent* TurnSystemUtils::GetPlayerASC(const UObject* WorldContext)
{
	if (!WorldContext)
	{
		return nullptr;
	}

	// Try to get from PlayerState
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(WorldContext, 0))
	{
		if (ALyraPlayerState* PS = PC->GetPlayerState<ALyraPlayerState>())
		{
			return PS->GetAbilitySystemComponent();
		}

		// Fallback: Try to get from Pawn
		if (APawn* Pawn = PC->GetPawn())
		{
			if (IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Pawn))
			{
				return ASI->GetAbilitySystemComponent();
			}
		}
	}

	return nullptr;
}
