// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "AuraDebuffGameplayEffect.generated.h"

/**
 * 持续伤害（DoT）debuff 基类。所有数值走 SetByCaller（Debuff.Damage / Debuff.Duration / Debuff.Frequency）。
 */
UCLASS()
class AURA_API UAuraDebuffGameplayEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UAuraDebuffGameplayEffect();
};

/**
 * 火系燃烧。授予标签 Effects.Debuff.Burn（施加时以 spec 动态标签写入，见 AuraAttributeSet）。
 */
UCLASS()
class AURA_API UAuraDebuffBurn : public UAuraDebuffGameplayEffect
{
	GENERATED_BODY()

public:
	UAuraDebuffBurn();
};
