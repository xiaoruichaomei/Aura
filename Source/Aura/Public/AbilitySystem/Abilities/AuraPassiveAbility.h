
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/AuraGameplayAbility.h"
#include "AuraPassiveAbility.generated.h"

class UGameplayEffect;
struct FActiveGameplayEffectHandle;

/**
 * 可装备被动的基类：
 * - 激活时给自己挂一个 Infinite 状态 GE（负责携带光环 GameplayCue），保持激活不结束；
 * - 被 ASC 的 DeactivatePassiveAbility 广播匹配到自身 AbilityTag 时结束；
 * - EndAbility 时移除状态 GE、解绑委托，让装备状态、实际功能与光环表现共享同一生命周期。
 */
UCLASS()
class AURA_API UAuraPassiveAbility : public UAuraGameplayAbility
{
	GENERATED_BODY()

public:
	UAuraPassiveAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	void ReceiveDeactivate(const FGameplayTag& AbilityTag);

	/** 被动激活期间持续存在的状态 GE（Infinite，用于携带光环 GameplayCue），在 GA 蓝图里配置 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Passive")
	TSubclassOf<UGameplayEffect> PassiveStateEffectClass;

	/** 激活时给自己应用的状态 GE 句柄（EndAbility 时移除） */
	FActiveGameplayEffectHandle PassiveStateEffectHandle;

	/** 该被动的 AbilityTag（资产标签里第一个 Abilities.* 标签），用于 DeactivatePassiveAbility 匹配 */
	FGameplayTag PassiveAbilityTag;

	/** DeactivatePassiveAbility 委托的绑定句柄，EndAbility 时解绑 */
	FDelegateHandle DeactivateDelegateHandle;
};
