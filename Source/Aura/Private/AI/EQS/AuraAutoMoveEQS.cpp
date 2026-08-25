#include "AI/EQS/AuraAutoMoveEQS.h"

#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"
#include "Player/AuraPlayerController.h"

UAuraEnvQueryGenerator_AutoMove::UAuraEnvQueryGenerator_AutoMove()
{
	ItemType = UEnvQueryItemType_Point::StaticClass();
	bAutoSortTests = true;
}

void UAuraEnvQueryGenerator_AutoMove::GenerateItems(FEnvQueryInstance& QueryInstance) const
{
	if (const AAuraPlayerController* Controller = Cast<AAuraPlayerController>(QueryInstance.Owner.Get()))
	{
		Controller->GenerateAutoMoveEQSItems(QueryInstance, bRecoveryQuery);
	}
}

UAuraEnvQueryTest_AutoMove::UAuraEnvQueryTest_AutoMove()
{
	Cost = EEnvTestCost::High;
	ValidItemType = UEnvQueryItemType_Point::StaticClass();
	TestPurpose = EEnvTestPurpose::Score;
	FilterType = EEnvTestFilterType::Range;
	ScoringEquation = EEnvTestScoreEquation::Linear;
	ScoringFactor.DefaultValue = 1.f;
	SetWorkOnFloatValues(true);
}

void UAuraEnvQueryTest_AutoMove::RunTest(FEnvQueryInstance& QueryInstance) const
{
	const AAuraPlayerController* Controller = Cast<AAuraPlayerController>(QueryInstance.Owner.Get());
	if (!Controller)
	{
		return;
	}

	for (FEnvQueryInstance::ItemIterator It(this, QueryInstance); It; ++It)
	{
		float Score = 0.f;
		if (Controller->ScoreAutoMoveEQSItem(GetItemLocation(QueryInstance, It), bRecoveryQuery, Score))
		{
			It.SetScore(TestPurpose, FilterType, Score, 0.f, 1.f);
		}
		else
		{
			It.ForceItemState(EEnvItemStatus::Failed);
		}
	}
}
