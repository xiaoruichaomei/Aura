// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "ScalableFloat.h"
#include "AuraDamageTypes.generated.h"

class UGameplayEffect;

/**
 * 一次伤害所需的全部参数。由技能在类默认值里配置，或按需覆盖后传给 MakeDamageEffectSpec。
 */
USTRUCT(BlueprintType)
struct FAuraDamageEffectParams
{
	GENERATED_BODY()

	FAuraDamageEffectParams() {}

	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<UObject> SourceObject;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSubclassOf<UGameplayEffect> DamageEffectClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FGameplayTag DamageType;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FScalableFloat BaseDamage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FScalableFloat DebuffChance;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FScalableFloat DebuffDamage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FScalableFloat DebuffDuration;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FScalableFloat DebuffFrequency;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float DeathImpulseMagnitude = 0.f;

	/** 运行时由调用方按"施法者→目标"方向计算（方向 * DeathImpulseMagnitude），无需在蓝图配置 */
	UPROPERTY(BlueprintReadOnly)
	FVector DeathImpulse = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float KnockbackMagnitude = 0.f;
};
