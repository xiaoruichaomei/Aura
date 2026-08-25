


#include "AI/AuraAIController.h"

#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Navigation/CrowdFollowingComponent.h"

AAuraAIController::AAuraAIController()
{
	Blackboard = CreateDefaultSubobject<UBlackboardComponent>("BlackboardComponent");
	check(Blackboard);
	BehaviorTreeComponent = CreateDefaultSubobject<UBehaviorTreeComponent>("BehaviorTreeComponent");
	check(BehaviorTreeComponent);

	if (UCrowdFollowingComponent* CrowdFollowing = Cast<UCrowdFollowingComponent>(GetPathFollowingComponent()))
	{
		CrowdFollowing->SetCrowdAnticipateTurns(true);
		CrowdFollowing->SetCrowdObstacleAvoidance(true);
		CrowdFollowing->SetCrowdSeparation(false);
		CrowdFollowing->SetCrowdOptimizeVisibility(true);
		CrowdFollowing->SetCrowdOptimizeTopology(true);
		CrowdFollowing->SetCrowdSlowdownAtGoal(false);
		CrowdFollowing->SetCrowdSeparationWeight(2.f);
		CrowdFollowing->SetCrowdCollisionQueryRange(400.f);
		CrowdFollowing->SetCrowdPathOptimizationRange(1000.f);
		CrowdFollowing->SetCrowdAvoidanceQuality(ECrowdAvoidanceQuality::Good);
		CrowdFollowing->SetCrowdAvoidanceRangeMultiplier(1.f);
		CrowdFollowing->SetCrowdAffectFallingVelocity(false);
	}
}
