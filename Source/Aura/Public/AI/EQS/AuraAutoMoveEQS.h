#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryGenerator.h"
#include "EnvironmentQuery/EnvQueryTest.h"
#include "AuraAutoMoveEQS.generated.h"

UCLASS()
class AURA_API UAuraEnvQueryGenerator_AutoMove : public UEnvQueryGenerator
{
	GENERATED_BODY()

public:
	UAuraEnvQueryGenerator_AutoMove();
	virtual void GenerateItems(FEnvQueryInstance& QueryInstance) const override;

	void SetRecoveryQuery(bool bInRecoveryQuery) { bRecoveryQuery = bInRecoveryQuery; }

private:
	UPROPERTY()
	bool bRecoveryQuery = false;
};

UCLASS()
class AURA_API UAuraEnvQueryTest_AutoMove : public UEnvQueryTest
{
	GENERATED_BODY()

public:
	UAuraEnvQueryTest_AutoMove();
	virtual void RunTest(FEnvQueryInstance& QueryInstance) const override;

	void SetRecoveryQuery(bool bInRecoveryQuery) { bRecoveryQuery = bInRecoveryQuery; }

private:
	UPROPERTY()
	bool bRecoveryQuery = false;
};
