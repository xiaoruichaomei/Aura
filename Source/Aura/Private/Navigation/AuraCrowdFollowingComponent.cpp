#include "Navigation/AuraCrowdFollowingComponent.h"

#include "Player/AuraPlayerController.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraCrowdMove, Log, All);

void UAuraCrowdFollowingComponent::GetCrowdAgentCollisions(float& CylinderRadius, float& CylinderHalfHeight) const
{
	Super::GetCrowdAgentCollisions(CylinderRadius, CylinderHalfHeight);
	CylinderRadius += AvoidanceRadiusPadding;
}

void UAuraCrowdFollowingComponent::ApplyCrowdAgentVelocity(
	const FVector& NewVelocity, const FVector& DestPathCorner,
	bool bTraversingLink, bool bIsNearEndOfPath)
{
	Super::ApplyCrowdAgentVelocity(NewVelocity, DestPathCorner, bTraversingLink, bIsNearEndOfPath);
	if (AAuraPlayerController* Controller = Cast<AAuraPlayerController>(GetOwner()))
	{
		Controller->HandleCrowdSteeringVelocity(NewVelocity);
	}

	const UWorld* World = GetWorld();
	if (World && GetStatus() == EPathFollowingStatus::Moving && World->GetTimeSeconds() >= NextMovementDebugTime)
	{
		NextMovementDebugTime = World->GetTimeSeconds() + 0.25f;
		UE_LOG(LogAuraCrowdMove, Display,
			TEXT("Velocity owner=%s crowd=%s actual=%s corner=%s simulation=%d avoidance=%d nearEnd=%d"),
			*GetNameSafe(GetOwner()), *NewVelocity.ToCompactString(),
			*GetCrowdAgentVelocity().ToCompactString(), *DestPathCorner.ToCompactString(),
			static_cast<uint8>(GetCrowdSimulationState()), IsCrowdObstacleAvoidanceActive(), bIsNearEndOfPath);
	}
}

void UAuraCrowdFollowingComponent::OnPathFinished(const FPathFollowingResult& Result)
{
	UE_LOG(LogAuraCrowdMove, Display, TEXT("PathFinished owner=%s status=%d result=%s"),
		*GetNameSafe(GetOwner()), static_cast<uint8>(GetStatus()), *Result.ToString());
	Super::OnPathFinished(Result);

	if (AAuraPlayerController* Controller = Cast<AAuraPlayerController>(GetOwner()))
	{
		Controller->HandleAutoMovePathFinished(Result);
	}
}
