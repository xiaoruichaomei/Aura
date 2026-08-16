// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "AuraElectrocuteStunGameplayEffect.generated.h"

/**
 * 电击眩晕 GE 基类。
 * 授予的标签（Effects.Debuff.Stun / Effects.Debuff.Electrocute）与门控标签（Effects.Debuff）
 * 不在 CDO 里配置，而是在施加时以 spec 动态标签写入（见 UAuraBeamSpell），
 * 避免 GE CDO 早于 GameplayTag 初始化的问题。
 *
 * 两个子类共享堆叠设置：按源聚合、上限 1 —— 同一施法者重复施加（换目标重进链、多段施法）
 * 只保留一个效果，防止堆叠大量 GE；不同施法者各自独立，互不影响对方的眩晕。
 */
UCLASS(Abstract)
class AURA_API UAuraElectrocuteStunGameplayEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UAuraElectrocuteStunGameplayEffect();
};

/**
 * 电击眩晕 - Channel：被电击期间持续存在（Infinite）。
 * 无限时长，按源聚合上限 1。
 */
UCLASS()
class AURA_API UAuraElectrocuteStunChannel : public UAuraElectrocuteStunGameplayEffect
{
	GENERATED_BODY()

public:
	UAuraElectrocuteStunChannel();
};

/**
 * 电击眩晕 - Tail：电击离开后继续眩晕 2 秒（Has Duration，2.0）。
 * 重复施加刷新持续时间（堆叠策略由基类设置）。
 */
UCLASS()
class AURA_API UAuraElectrocuteStunTail : public UAuraElectrocuteStunGameplayEffect
{
	GENERATED_BODY()

public:
	UAuraElectrocuteStunTail();
};
