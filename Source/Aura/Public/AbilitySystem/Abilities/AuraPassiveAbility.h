#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/AuraGameplayAbility.h"
#include "AuraPassiveAbility.generated.h"

class UAbilityTask_WaitGameplayEvent;
class UGameplayEffect;
struct FActiveGameplayEffectHandle;

UCLASS()
class AURA_API UAuraPassiveAbility : public UAuraGameplayAbility
{
	GENERATED_BODY()

public:
	UAuraPassiveAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	void ReceiveDeactivate(const FGameplayTag& AbilityTag);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Passive")
	TSubclassOf<UGameplayEffect> PassiveStateEffectClass;

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Passive|Siphon")
	FScalableFloat RestorePercent = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Passive|Halo")
	FScalableFloat ShieldRechargeTime = 8.f;

private:
	void ApplyPassiveStateEffect(const FGameplayTag& GrantedTag);
	void StartHaloRecharge();
	void ApplyHaloShield();

	UFUNCTION()
	void HandleDamageDealt(FGameplayEventData Payload);

	UFUNCTION()
	void HandleHaloShieldConsumed(FGameplayEventData Payload);

	UPROPERTY()
	TObjectPtr<UAbilityTask_WaitGameplayEvent> PassiveEventTask;

	FActiveGameplayEffectHandle PassiveStateEffectHandle;
	FActiveGameplayEffectHandle HaloShieldEffectHandle;
	FGameplayTag PassiveAbilityTag;
	FDelegateHandle DeactivateDelegateHandle;
	FTimerHandle HaloRechargeTimerHandle;
};
