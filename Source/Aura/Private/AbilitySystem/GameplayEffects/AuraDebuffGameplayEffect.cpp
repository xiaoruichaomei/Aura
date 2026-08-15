// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/GameplayEffects/AuraDebuffGameplayEffect.h"

#include "AbilitySystem/ExecCalc/ExecCalc_Debuff.h"

UAuraDebuffGameplayEffect::UAuraDebuffGameplayEffect()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;

	// 时长/频率在施加时通过 Spec->SetDuration() / Spec->Period 覆盖（见 AuraAttributeSet）。
	// 注意：不要在这里读取 FAuraGameplayTags 来配置 SetByCaller 标签——GE 的 CDO 在游戏模块
	// 加载时构造（早于 GameplayTag 初始化），读到的标签会是空的。
	DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(1.f));
	Period = FScalableFloat(1.f);

	// 每跳伤害由执行计算在运行时从 spec 的 SetByCaller（Debuff.Damage）读取。
	FGameplayEffectExecutionDefinition DebuffExecution;
	DebuffExecution.CalculationClass = UExecCalc_Debuff::StaticClass();
	Executions.Add(DebuffExecution);

	// 禁止施加时立即执行一次（默认 true）：燃烧应在首个周期后才跳伤，命中时不额外跳一次
	bExecutePeriodicEffectOnApplication = false;
}

UAuraDebuffBurn::UAuraDebuffBurn()
{
	// 标签（Effects.Debuff 门控标签 + Effects.Debuff.Burn 授予标签）在施加时以 spec 动态标签写入，
	// 见 AuraAttributeSet::PostGameplayEffectExecute。
}
