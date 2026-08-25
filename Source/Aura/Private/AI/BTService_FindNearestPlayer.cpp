


#include "AI/BTService_FindNearestPlayer.h"

#include "AIController.h"
#include "BehaviorTree/BTFunctionLibrary.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Interface/CombatInterface.h"

void UBTService_FindNearestPlayer::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);
	
	APawn* OwningPawn = AIOwner->GetPawn();
	
	float ClosestDistance = TNumericLimits<float>::Max();
	AActor* ClosestActor = nullptr;
	// Iterate controllers instead of stopping at the first missing Pawn. During
	// respawn one player's controller temporarily has no Pawn, while other players
	// are still valid targets.
	for (FConstPlayerControllerIterator It = OwningPawn->GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PlayerController = It->Get();
		APawn* PlayerPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
		if (!IsValid(PlayerPawn) || PlayerPawn == OwningPawn)
		{
			continue;
		}
		if (PlayerPawn->Implements<UCombatInterface>() && ICombatInterface::Execute_IsDead(PlayerPawn))
		{
			continue;
		}

		const float Distance = OwningPawn->GetDistanceTo(PlayerPawn);
		if (Distance < ClosestDistance)
		{
			ClosestDistance = Distance;
			ClosestActor = PlayerPawn;
		}
	}
	UBTFunctionLibrary::SetBlackboardValueAsObject(this, TargetToFollow, ClosestActor);
	UBTFunctionLibrary::SetBlackboardValueAsFloat(this, DistanceToTarget, ClosestDistance);
}
