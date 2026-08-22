


#include "AI/BTService_FindNearestPlayer.h"

#include "AIController.h"
#include "BehaviorTree/BTFunctionLibrary.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

void UBTService_FindNearestPlayer::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);
	
	APawn* OwningPawn = AIOwner->GetPawn();
	
	float ClosestDistance = TNumericLimits<float>::Max();
	AActor* ClosestActor = nullptr;
	// Do not rely on Blueprint Actor Tags here. Pooled enemies are spawned at
	// runtime and may not inherit the same tag setup as level-placed enemies.
	for (int32 PlayerIndex = 0; ; ++PlayerIndex)
	{
		APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(OwningPawn, PlayerIndex);
		if (!PlayerPawn)
		{
			break;
		}
		if (PlayerPawn != OwningPawn)
		{
			const float Distance = OwningPawn->GetDistanceTo(PlayerPawn);
			if (Distance < ClosestDistance)
			{
				ClosestDistance = Distance;
				ClosestActor = PlayerPawn;
			}
		}
	}
	UBTFunctionLibrary::SetBlackboardValueAsObject(this, TargetToFollow, ClosestActor);
	UBTFunctionLibrary::SetBlackboardValueAsFloat(this, DistanceToTarget, ClosestDistance);
}
