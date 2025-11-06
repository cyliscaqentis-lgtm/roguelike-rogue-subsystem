// LyraTBSAIController.h
#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"
#include "GenericTeamAgentInterface.h"
#include "LyraTBSAIController.generated.h"

UCLASS()
class LYRAGAME_API ALyraTBSAIController : public AAIController
{
    GENERATED_BODY()

public:
    ALyraTBSAIController();

protected:
    virtual void BeginPlay() override;
    virtual void OnRep_PlayerState() override;

    // ★★★ OnPossessをオーバーライド ★★★
    virtual void OnPossess(APawn* InPawn) override;

    // IGenericTeamAgentInterfaceのメソッドをオーバーライド
    virtual FGenericTeamId GetGenericTeamId() const override;
    virtual void SetGenericTeamId(const FGenericTeamId& NewTeamID) override;

private:
    // チームID（内部管理用）
    FGenericTeamId TeamId = FGenericTeamId::NoTeam;
};
