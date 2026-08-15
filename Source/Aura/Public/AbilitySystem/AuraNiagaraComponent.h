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

private:
	void OnTagChanged(const FGameplayTag Tag, int32 NewCount);
};
