// ============================================================================
// PathFinderUtils.h
// 重複していたPathFinder取得処理の共通化
// 作成日: 2025-11-09
// ============================================================================

#pragma once

#include "CoreMinimal.h"
#include "Kismet/GameplayStatics.h"

class AGridPathfindingLibrary;

/**
 * PathFinder取得の共通ユーティリティ
 * 以前は9ファイルで重複していた処理を統一
 */
class FPathFinderUtils
{
public:
    /**
     * PathFinderをキャッシュ付きで取得
     * @param World ワールド
     * @param OutCached キャッシュされた参照（オプション）
     * @return PathFinderインスタンス（見つからない場合はnullptr）
     */
    static AGridPathfindingLibrary* GetCachedPathFinder(
        UWorld* World,
        TWeakObjectPtr<AGridPathfindingLibrary>* OutCached = nullptr
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

        // GetActorOfClassで検索
        AGridPathfindingLibrary* PathFinder = Cast<AGridPathfindingLibrary>(
            UGameplayStatics::GetActorOfClass(World, AGridPathfindingLibrary::StaticClass())
        );

        // キャッシュに保存
        if (PathFinder && OutCached)
        {
            *OutCached = PathFinder;
        }

        return PathFinder;
    }
};
