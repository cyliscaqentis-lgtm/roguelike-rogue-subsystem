#pragma once
#include "CoreMinimal.h"

// 必要に応じて .Build.cs から PublicDefinitions で上書き可
#ifndef ENABLE_DIAGNOSTICS
#define ENABLE_DIAGNOSTICS 1  // 開発中は有効化
#endif

// ============================================================================
// ログカテゴリの宣言（すべて集約）
// ============================================================================
DECLARE_LOG_CATEGORY_EXTERN(LogRogueDiagnostics, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogEnemyAI, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogGridPathfinding, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogTurnManager, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogTurnPhase, Log, All);

// ラッパーマクロ：Shipping では沈黙、開発時もフラグで制御
extern int32 GRogueDebugVerboseLogging;

#if !UE_BUILD_SHIPPING
  #define DIAG_LOG(Verbosity, Format, ...) \
    do { \
        if (ENABLE_DIAGNOSTICS) { \
            if (GRogueDebugVerboseLogging > 0) { \
                UE_LOG(LogRogueDiagnostics, Warning, Format, ##__VA_ARGS__); \
            } else { \
                UE_LOG(LogRogueDiagnostics, Verbosity, Format, ##__VA_ARGS__); \
            } \
        } \
    } while(0)
#else
  #define DIAG_LOG(Verbosity, Format, ...)
#endif

