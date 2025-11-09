// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Turn/TurnSystemTypes.h"
#include "../../ProjectDiagnostics.h"
#include "EnemyAISubsystem.generated.h"

class AGridPathfindingLibrary;
class UTurnCorePhaseManager;

/**
 * UEnemyAISubsystem
 *
 * 敵AIの純粋計算層。副作用なし。
 * 原子的なレゴブロック（部品）のみを提供。
 */
UCLASS()
class LYRAGAME_API UEnemyAISubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * 全敵のObservationを生成（原子的部品）
	 * @param Enemies 敵Actor配列（BPから渡す）
	 * @param Player プレイヤーActor（BPから渡す）
	 * @param PathFinder 経路探索ライブラリ（BPから渡す）
	 * @param OutObs 出力：生成されたObservation配列
	 */
	UFUNCTION(BlueprintCallable, Category = "Turn|Enemy AI")
	void BuildObservations(
		const TArray<AActor*>& Enemies,
		AActor* Player,
		AGridPathfindingLibrary* PathFinder,
		TArray<FEnemyObservation>& OutObs);

	/**
	 * 全敵のIntentを収集（原子的部品）
	 * @param Obs 観測データ配列（BPから渡す）
	 * @param Enemies 敵Actor配列（BPから渡す）
	 * @param OutIntents 出力：生成されたIntent配列
	 */
	UFUNCTION(BlueprintCallable, Category = "Turn|Enemy AI")
	void CollectIntents(
		const TArray<FEnemyObservation>& Obs,
		const TArray<AActor*>& Enemies,
		TArray<FEnemyIntent>& OutIntents);

	/**
	 * 個別敵の意図を計算（原子的部品）
	 * @param Enemy 敵Actor（BPから渡す）
	 * @param Observation その敵の観測データ（BPから渡す）
	 * @return 計算されたIntent
	 */
	UFUNCTION(BlueprintCallable, Category = "Turn|Enemy AI")
	FEnemyIntent ComputeIntent(
		AActor* Enemy,
		const FEnemyObservation& Observation);

	/**
	 * ★★★ Phase 4: Enemy収集をSubsystemに統合（2025-11-09） ★★★
	 * 全ての敵Actorを収集（原子的部品）
	 * @param PlayerPawn プレイヤーPawn（除外用）
	 * @param OutEnemies 出力：収集された敵Actor配列
	 */
	UFUNCTION(BlueprintCallable, Category = "Turn|Enemy AI")
	void CollectAllEnemies(
		AActor* PlayerPawn,
		TArray<AActor*>& OutEnemies);
};
