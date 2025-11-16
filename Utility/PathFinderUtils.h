// ============================================================================
// PathFinderUtils.h
// CodeRevision: INC-2025-00030-R2 (Migrate to UGridPathfindingSubsystem) (2025-11-17 00:40)
// Migrated from AGridPathfindingLibrary to UGridPathfindingSubsystem
// 作成日: 2025-11-09
// ============================================================================

#pragma once

#include "CoreMinimal.h"
#include "Grid/GridPathfindingSubsystem.h"

/**
 * PathFinder取得の共通ユーティリティ
 * UGridPathfindingSubsystem取得の統一インターフェース
 */
class FPathFinderUtils
{
public:
    /**
     * UGridPathfindingSubsystemをキャッシュ付きで取得
     * @param World ワールド
     * @param OutCached キャッシュされた参照（オプション）
     * @return UGridPathfindingSubsystem（見つからない場合はnullptr）
     */
    static UGridPathfindingSubsystem* GetCachedPathFinder(
        UWorld* World,
        TWeakObjectPtr<UGridPathfindingSubsystem>* OutCached = nullptr
    )
    {
        if (!World)
        {
            return nullptr;
        }

        // キャッシュがあれば使用
        if (OutCached && OutCached->IsValid())
        {
            return OutCached->Get();
        }

        // Get subsystem from world
        UGridPathfindingSubsystem* PathFinder = World->GetSubsystem<UGridPathfindingSubsystem>();

        // キャッシュに保存
        if (PathFinder && OutCached)
        {
            *OutCached = PathFinder;
        }

        return PathFinder;
    }
};
