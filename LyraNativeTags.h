#pragma once
#include "NativeGameplayTags.h"

// ネイティブ定義の Gameplay Tags（モジュール初期化時に安全に登録される）
namespace LyraTags
{
	// 例：必要に応じて増やしてください
	LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Attack);
	LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Wait);
	LYRAGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Melee);
}
