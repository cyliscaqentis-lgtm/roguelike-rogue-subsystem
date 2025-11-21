#include "Utility/ProjectDiagnostics.h"

// ============================================================================
// ログカテゴリの定義（すべて集約）--
// ============================================================================
DEFINE_LOG_CATEGORY(LogRogueDiagnostics);
DEFINE_LOG_CATEGORY(LogEnemyAI);
DEFINE_LOG_CATEGORY(LogGridPathfinding);
DEFINE_LOG_CATEGORY(LogTurnManager);
DEFINE_LOG_CATEGORY(LogTurnPhase);

int32 GRogueDebugVerboseLogging = 1;
static FAutoConsoleVariableRef CVarRogueDebugVerboseLogging(
    TEXT("Rogue.Debug.VerboseLogging"),
    GRogueDebugVerboseLogging,
    TEXT("Enable verbose diagnostic logging.\n")
    TEXT("0: Off (default)\n")
    TEXT("1: On (forces VeryVerbose logs to be printed as Log)"),
    ECVF_Cheat
);

