// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"

/**
 * Actorを検索・取得するためのユーティリティ関数群
 * キャッシュ機能を持つテンプレート関数により、重複したゲッター実装を削減
 */
namespace RogueUtils
{
    /**
     * ワールドから指定クラスのActorを取得（キャッシュ付き）
     * GameplayStatics::GetActorOfClassを使用してActorを検索
     *
     * @param World 検索対象のワールド
     * @param CachedPtr キャッシュ用のWeakObjectPtr（呼び出し側で保持）
     * @return 見つかったActor、見つからなければnullptr
     *
     * 使用例:
     *   TWeakObjectPtr<AGridPathfindingLibrary> CachedPathFinder;
     *   AGridPathfindingLibrary* PF = RogueUtils::GetCachedActor(GetWorld(), CachedPathFinder);
     */
    template<typename TActorClass>
    static TActorClass* GetCachedActor(const UWorld* World, TWeakObjectPtr<TActorClass>& CachedPtr)
    {
        // キャッシュが有効ならそれを返す
        if (CachedPtr.IsValid())
        {
            return CachedPtr.Get();
        }

        if (!World)
        {
            return nullptr;
        }

        // GameplayStatics経由でActorを検索
        if (TActorClass* Found = Cast<TActorClass>(
            UGameplayStatics::GetActorOfClass(World, TActorClass::StaticClass())))
        {
            CachedPtr = Found;
            return Found;
        }

        return nullptr;
    }

    /**
     * ワールドから指定クラスのActorを取得（イテレーター版、キャッシュ付き）
     * TActorIteratorを使用してActorを検索
     *
     * @param World 検索対象のワールド
     * @param CachedPtr キャッシュ用のWeakObjectPtr（呼び出し側で保持）
     * @return 見つかったActor、見つからなければnullptr
     *
     * 使用例:
     *   TWeakObjectPtr<AGameTurnManagerBase> CachedTurnManager;
     *   AGameTurnManagerBase* TM = RogueUtils::GetCachedActorByIterator(GetWorld(), CachedTurnManager);
     */
    template<typename TActorClass>
    static TActorClass* GetCachedActorByIterator(const UWorld* World, TWeakObjectPtr<TActorClass>& CachedPtr)
    {
        // キャッシュが有効ならそれを返す
        if (CachedPtr.IsValid())
        {
            return CachedPtr.Get();
        }

        if (!World)
        {
            return nullptr;
        }

        // TActorIteratorでワールド内のActorを走査
        for (TActorIterator<TActorClass> It(World); It; ++It)
        {
            CachedPtr = *It;
            return *It;
        }

        return nullptr;
    }
}
