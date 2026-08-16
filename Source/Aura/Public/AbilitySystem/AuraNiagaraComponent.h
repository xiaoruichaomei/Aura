// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "NiagaraComponent.h"
#include "GameplayTagContainer.h"
#include "AuraNiagaraComponent.generated.h"

/**
 * 标签驱动的 Niagara 组件：当拥有者的 AbilitySystemComponent 上出现指定 GameplayTag 时
 * 激活自身粒子系统，标签移除时停用。用于燃烧/冰冻等持续效果的表现。
 */
UCLASS()
class AURA_API UAuraNiagaraComponent : public UNiagaraComponent
{
	GENERATED_BODY()

public:
	UAuraNiagaraComponent();

	virtual void BeginPlay() override;

	/** 监听这个标签的 出现/移除 来决定激活/停用 */
	UPROPERTY(EditAnywhere, Category="Niagara")
	FGameplayTag GameplayTag;

	/** 标签持续期间周期性检查并重启特效（针对一次性不循环的资产，如眩晕星星；循环资产无需开启，开启也不会被打断） */
	UPROPERTY(EditAnywhere, Category="Niagara")
	bool bLoopWhileActive = false;

	/** 标签持续期间检查系统是否已播完的间隔（秒） */
	UPROPERTY(EditAnywhere, Category="Niagara", meta=(EditCondition="bLoopWhileActive"))
	float LoopRestartInterval = 0.2f;

private:
	void InitializeTagBinding();
	void OnTagChanged(const FGameplayTag Tag, int32 NewCount);
	void RestartEffectIfComplete();
	FTimerHandle LoopRestartTimerHandle;
};
