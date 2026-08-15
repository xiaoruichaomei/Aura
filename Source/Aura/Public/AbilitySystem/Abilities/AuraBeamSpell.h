// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/AuraDamageGameplayAbility.h"
#include "AuraBeamSpell.generated.h"

class UAbilitySystemComponent;

/**
 * 光束法术（电击）基类：从施法者向目标方向发射光束。
 * 施法期间阻塞玩家的其他技能（ActivationBlockedTags）和移动（Player.Block 标签）。
 * GA_Electrocute 蓝图应继承此类：播放蒙太奇 → StoreOwnerVariables → TraceFirstTarget → 施加伤害。
 */
UCLASS()
class AURA_API UAuraBeamSpell : public UAuraDamageGameplayAbility
{
	GENERATED_BODY()

public:
	UAuraBeamSpell();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	UFUNCTION(BlueprintCallable)
	void StoreOwnerVariables();

	UFUNCTION(BlueprintCallable)
	void StoreSingleTarget(const FVector& InTargetLocation);

	/** 从施法者向 BeamTargetLocation 做线检测，找到第一个目标（记录光束终点与目标 Actor） */
	UFUNCTION(BlueprintCallable)
	void TraceFirstTarget(const FVector& BeamTargetLocation);

	/** 对 TraceFirstTarget 记录的目标施加伤害（目标为空时安全跳过） */
	UFUNCTION(BlueprintCallable)
	void CauseBeamDamage();

protected:
	UPROPERTY(BlueprintReadWrite, Category="Beam")
	FVector TargetLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category="Beam")
	TObjectPtr<AActor> TargetActor = nullptr;

	UPROPERTY(BlueprintReadWrite, Category="Beam")
	TObjectPtr<AActor> OwnerActor = nullptr;

	UPROPERTY(BlueprintReadWrite, Category="Beam")
	TObjectPtr<UAbilitySystemComponent> OwnerASC = nullptr;

	/** 追踪时忽略的 Actor（自己、已死亡目标等） */
	UPROPERTY(BlueprintReadWrite, Category="Beam")
	TArray<TObjectPtr<AActor>> ActorsToIgnore;
};
