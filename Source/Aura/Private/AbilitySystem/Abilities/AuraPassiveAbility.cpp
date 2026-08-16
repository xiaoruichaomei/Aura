

#include "AbilitySystem/Abilities/AuraPassiveAbility.h"

#include "AbilitySystem/AuraAbilitySystemComponent.h"
#include "AbilitySystem/AuraAbilitySystemLibrary.h"
#include "AbilitySystemComponent.h"
#include "AuraGameplayTags.h"
#include "GameplayEffect.h"

UAuraPassiveAbility::UAuraPassiveAbility()
{
	// 每个角色有独立的 Ability 实例；否则非实例化 Ability 绑定角色委托时多个角色可能共用同一个 CDO
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

void UAuraPassiveAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (UAuraAbilitySystemComponent* AuraASC = Cast<UAuraAbilitySystemComponent>(UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetAvatarActorFromActorInfo())))
	{
		// 换槽位/重新装备会再次激活同一能力实例，先移除旧绑定避免委托重复触发
		AuraASC->DeactivatePassiveAbility.RemoveAll(this);
		DeactivateDelegateHandle = AuraASC->DeactivatePassiveAbility.AddUObject(this, &UAuraPassiveAbility::ReceiveDeactivate);
	}

	// 记录自身 AbilityTag：资产标签里第一个匹配 Abilities.* 的标签
	PassiveAbilityTag = FGameplayTag();
	for (const FGameplayTag& Tag : GetAssetTags())
	{
		if (Tag.MatchesTag(FGameplayTag::RequestGameplayTag(FName("Abilities"))))
		{
			PassiveAbilityTag = Tag;
			break;
		}
	}

	// 给自己应用持续状态 GE（携带光环 GameplayCue，光环与被动共享生命周期）
	if (PassiveStateEffectClass)
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			FGameplayEffectContextHandle ContextHandle = ASC->MakeEffectContext();
			ContextHandle.AddSourceObject(GetAvatarActorFromActorInfo());
			const FGameplayEffectSpecHandle SpecHandle = ASC->MakeOutgoingSpec(PassiveStateEffectClass, GetAbilityLevel(), ContextHandle);
			if (SpecHandle.IsValid())
			{
				PassiveStateEffectHandle = ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
			}
		}
	}

	// 被动保持激活不结束（不调用 EndAbility）
}

void UAuraPassiveAbility::ReceiveDeactivate(const FGameplayTag& AbilityTag)
{
	if (PassiveAbilityTag.IsValid() && AbilityTag.MatchesTagExact(PassiveAbilityTag))
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
	}
}

void UAuraPassiveAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	// 移除状态 GE（连带移除其 GameplayCue，光环自动停止）
	if (PassiveStateEffectHandle.IsValid())
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			ASC->RemoveActiveGameplayEffect(PassiveStateEffectHandle);
		}
		PassiveStateEffectHandle = FActiveGameplayEffectHandle();
	}

	// 解绑 DeactivatePassiveAbility 委托
	if (DeactivateDelegateHandle.IsValid())
	{
		if (UAuraAbilitySystemComponent* AuraASC = Cast<UAuraAbilitySystemComponent>(GetAbilitySystemComponentFromActorInfo()))
		{
			AuraASC->DeactivatePassiveAbility.Remove(DeactivateDelegateHandle);
		}
		DeactivateDelegateHandle.Reset();
	}

	// 监听事件的 AbilityTask 由 Super::EndAbility 统一结束
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
