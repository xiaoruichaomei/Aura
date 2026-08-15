// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/Abilities/AuraBeamSpell.h"

#include "AbilitySystemComponent.h"
#include "AuraGameplayTags.h"
#include "Interface/CombatInterface.h"

UAuraBeamSpell::UAuraBeamSpell()
{
	// 蒙太奇/异步任务（PlayMontageAndWait、TargetDataUnderMouse）需要按次实例化
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerExecution;
}

void UAuraBeamSpell::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	// 施法期间阻塞其他技能（带 Abilities 标签的技能不能再激活）
	if (ActivationBlockedTags.IsEmpty())
	{
		ActivationBlockedTags.AddTag(FGameplayTag::RequestGameplayTag(FName("Abilities")));
	}

	// 阻塞玩家移动
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->AddLooseGameplayTag(FAuraGameplayTags::Get().Player_Block);
	}
}

void UAuraBeamSpell::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	// 解除移动阻塞
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->RemoveLooseGameplayTag(FAuraGameplayTags::Get().Player_Block);
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UAuraBeamSpell::StoreOwnerVariables()
{
	if (CurrentActorInfo)
	{
		OwnerActor = CurrentActorInfo->AvatarActor.Get();
		OwnerASC = CurrentActorInfo->AbilitySystemComponent.Get();
	}
}

void UAuraBeamSpell::StoreSingleTarget(const FVector& InTargetLocation)
{
	TargetLocation = InTargetLocation;
}

void UAuraBeamSpell::TraceFirstTarget(const FVector& BeamTargetLocation)
{
	if (!OwnerActor)
	{
		StoreOwnerVariables();
	}
	if (!OwnerActor)
	{
		return;
	}

	// 从施法者手部向目标方向线检测，找第一个敌人目标
	const FVector Start = ICombatInterface::Execute_GetCombatSockettLocation(OwnerActor, FAuraGameplayTags::Get().CombatSocket_LeftHand);
	const FVector End = BeamTargetLocation;

	FHitResult HitResult;
	GetWorld()->LineTraceSingleByChannel(HitResult, Start, End, ECC_Visibility);
	if (HitResult.bBlockingHit && HitResult.GetActor())
	{
		if (HitResult.GetActor()->Implements<UCombatInterface>() && !ICombatInterface::Execute_IsDead(HitResult.GetActor()))
		{
			TargetActor = HitResult.GetActor();
			TargetLocation = HitResult.ImpactPoint;
			return;
		}
	}

	// 没命中敌人：光束终点用目标位置
	TargetActor = nullptr;
	TargetLocation = BeamTargetLocation;
}

void UAuraBeamSpell::CauseBeamDamage()
{
	if (IsValid(TargetActor))
	{
		CauseDamage(TargetActor);
	}
}
