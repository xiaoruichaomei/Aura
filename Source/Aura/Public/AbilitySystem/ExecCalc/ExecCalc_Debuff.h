// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectExecutionCalculation.h"
#include "ExecCalc_Debuff.generated.h"

/**
 * 持续伤害（DoT）debuff 的执行计算。
 * 每跳从 spec 的 SetByCaller（Debuff.Damage）读取伤害，输出到 IncomingDamage。
 * 之所以用执行计算而不是 SetByCaller modifier：GE 的 CDO 在游戏模块加载时构造（早于
 * GameplayTag 初始化），modifier 里烘焙的 SetByCaller 标签会是空的；执行计算在运行时
 * （标签已注册）从 spec 读取，不受该时序问题影响。
 */
UCLASS()
class AURA_API UExecCalc_Debuff : public UGameplayEffectExecutionCalculation
{
	GENERATED_BODY()

public:
	virtual void Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams, FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const override;
};
