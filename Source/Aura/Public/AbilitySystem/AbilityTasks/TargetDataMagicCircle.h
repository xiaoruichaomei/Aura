#pragma once

#include "CoreMinimal.h"
#include "Abilities/Tasks/AbilityTask.h"
#include "TargetDataMagicCircle.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMagicCircleTargetDataSignature, const FGameplayAbilityTargetDataHandle&, DataHandle);

UCLASS()
class AURA_API UTargetDataMagicCircle : public UAbilityTask
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="Ability|Task", meta=(HidePin="OwningAbility", DefaultToSelf="OwningAbility", BlueprintInternalUseOnly="true"))
	static UTargetDataMagicCircle* WaitForMagicCircleTarget(UGameplayAbility* OwningAbility);

	void SendTargetData();

	UPROPERTY(BlueprintAssignable)
	FMagicCircleTargetDataSignature ValidData;

protected:
	virtual void Activate() override;

private:
	void OnTargetDataReplicatedCallback(const FGameplayAbilityTargetDataHandle& DataHandle, FGameplayTag ActivationTag);
};
