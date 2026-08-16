// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/GameplayEffects/AuraElectrocuteStunGameplayEffect.h"

UAuraElectrocuteStunGameplayEffect::UAuraElectrocuteStunGameplayEffect()
{
	// 抽象基类先给一个非 Instant 的 DurationPolicy，避免 CDO 上 "Instant GE won't Stack" 警告；
	// 具体时长由子类设置（Channel=Infinite，Tail=HasDuration）。
	DurationPolicy = EGameplayEffectDurationType::Infinite;

	// 堆叠：按源聚合、上限 1、重复施加刷新时长。
	// 同一施法者多次施加（如反复切换目标进入/离开闪电链）只保留一个 GE，防止堆叠大量眩晕效果；
	// 不同施法者各自独立，一个玩家停止只移除自己的眩晕，另一个玩家的眩晕仍然有效。
	// 注：5.8 里 StackingType 直连赋值有 C4996 弃用警告（5.9 将改私有），但 SetStackingType 是编辑器专属、
	// 运行时 CDO 不可用，故此处按需抑制该弃用警告。
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	StackingType = EGameplayEffectStackingType::AggregateBySource;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	StackLimitCount = 1;
	StackDurationRefreshPolicy = EGameplayEffectStackingDurationPolicy::RefreshOnSuccessfulApplication;
}

UAuraElectrocuteStunChannel::UAuraElectrocuteStunChannel()
{
	// 被电击期间持续存在
	DurationPolicy = EGameplayEffectDurationType::Infinite;
}

UAuraElectrocuteStunTail::UAuraElectrocuteStunTail()
{
	// 电击离开后继续眩晕 2 秒
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(2.f));
}
