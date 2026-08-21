#include "AbilitySystem/AbilityTasks/TargetDataMagicCircle.h"

#include "AbilitySystemComponent.h"
#include "Interface/PlayerInterface.h"

UTargetDataMagicCircle* UTargetDataMagicCircle::WaitForMagicCircleTarget(UGameplayAbility* OwningAbility)
{
	return NewAbilityTask<UTargetDataMagicCircle>(OwningAbility);
}

void UTargetDataMagicCircle::Activate()
{
	if (Ability->GetCurrentActorInfo()->IsNetAuthority() || !Ability->GetCurrentActorInfo()->IsLocallyControlled())
	{
		const FGameplayAbilitySpecHandle SpecHandle = GetAbilitySpecHandle();
		const FPredictionKey PredictionKey = GetActivationPredictionKey();
		AbilitySystemComponent->AbilityTargetDataSetDelegate(SpecHandle, PredictionKey).AddUObject(this, &ThisClass::OnTargetDataReplicatedCallback);
		if (!AbilitySystemComponent->CallReplicatedTargetDataDelegatesIfSet(SpecHandle, PredictionKey))
		{
			SetWaitingOnRemotePlayerData();
		}
	}
}

void UTargetDataMagicCircle::SendTargetData()
{
	if (!Ability->GetCurrentActorInfo()->IsLocallyControlled()) return;
	AActor* Avatar = Ability->GetCurrentActorInfo()->AvatarActor.Get();
	if (!Avatar || !Avatar->Implements<UPlayerInterface>()) return;

	const FVector Location = IPlayerInterface::Execute_GetMagicCircleLocation(Avatar);
	FHitResult HitResult;
	HitResult.ImpactPoint = Location;
	HitResult.Location = Location;
	FGameplayAbilityTargetDataHandle DataHandle;
	FGameplayAbilityTargetData_SingleTargetHit* Data = new FGameplayAbilityTargetData_SingleTargetHit();
	Data->HitResult = HitResult;
	DataHandle.Add(Data);
	if (Ability->GetCurrentActorInfo()->IsNetAuthority())
	{
		if (ShouldBroadcastAbilityTaskDelegates()) ValidData.Broadcast(DataHandle);
		return;
	}
	FScopedPredictionWindow PredictionWindow(AbilitySystemComponent.Get());
	AbilitySystemComponent->ServerSetReplicatedTargetData(GetAbilitySpecHandle(), GetActivationPredictionKey(), DataHandle, FGameplayTag(), AbilitySystemComponent->ScopedPredictionKey);
}

void UTargetDataMagicCircle::OnTargetDataReplicatedCallback(const FGameplayAbilityTargetDataHandle& DataHandle, FGameplayTag ActivationTag)
{
	AbilitySystemComponent->ConsumeClientReplicatedTargetData(GetAbilitySpecHandle(), GetActivationPredictionKey());
	if (ShouldBroadcastAbilityTaskDelegates()) ValidData.Broadcast(DataHandle);
}
