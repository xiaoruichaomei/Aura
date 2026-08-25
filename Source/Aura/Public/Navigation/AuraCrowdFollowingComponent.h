#pragma once

#include "CoreMinimal.h"
#include "Navigation/CrowdFollowingComponent.h"
#include "AuraCrowdFollowingComponent.generated.h"

UCLASS()
class AURA_API UAuraCrowdFollowingComponent : public UCrowdFollowingComponent
{
	GENERATED_BODY()

public:
	virtual void GetCrowdAgentCollisions(float& CylinderRadius, float& CylinderHalfHeight) const override;
	virtual void ApplyCrowdAgentVelocity(
		const FVector& NewVelocity, const FVector& DestPathCorner,
		bool bTraversingLink, bool bIsNearEndOfPath) override;
	virtual void OnPathFinished(const FPathFollowingResult& Result) override;
	void SetAvoidanceRadiusPadding(float InPadding) { AvoidanceRadiusPadding = FMath::Max(0.f, InPadding); }

private:
	float AvoidanceRadiusPadding = 10.f;
	float NextMovementDebugTime = 0.f;
};
