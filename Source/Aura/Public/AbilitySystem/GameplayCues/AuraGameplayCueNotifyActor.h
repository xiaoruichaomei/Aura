// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayCueNotify_Actor.h"
#include "AuraGameplayCueNotifyActor.generated.h"

class UNiagaraComponent;
class UAudioComponent;

/**
 * 电击光束的 GameplayCue 表现基类：挂一个 Niagara（电束）+ 一个循环音组件。
 * 基于它创建 GC notify 蓝图，在 WhileActive/OnExecute 激活，OnRemove 停用。
 */
UCLASS()
class AURA_API AAuraGameplayCueNotifyActor : public AGameplayCueNotify_Actor
{
	GENERATED_BODY()

public:
	AAuraGameplayCueNotifyActor();

	virtual bool WhileActive_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) override;
	virtual bool OnRemove_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) override;
	virtual bool Recycle() override;
	virtual void ReuseAfterRecycle() override;

	/** 每帧刷新光束终点/起点，让电束跟随移动的目标（否则终点固定在施法瞬间，怪物走开会从身上穿过） */
	virtual void Tick(float DeltaSeconds) override;

	/** 电束 Niagara（蓝图里指定资产，或运行时动态设置） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="GameplayCue")
	TObjectPtr<UNiagaraComponent> BeamNiagaraComponent;

	/** 电击循环音 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="GameplayCue")
	TObjectPtr<UAudioComponent> LoopingSoundComponent;

protected:
	/** 光束终点要跟随的目标（WhileActive 记录；无目标=朝鼠标方向时为空） */
	UPROPERTY()
	TWeakObjectPtr<AActor> BeamTargetActor;

	/** 链式光束的起点目标 = 上一个目标（AddShockLoopCue 通过 CueParams.SourceObject 传入；第一条/方向光束为空，起点跟随法杖附着点） */
	UPROPERTY()
	TWeakObjectPtr<AActor> BeamStartActor;

	UPROPERTY()
	TWeakObjectPtr<AActor> BeamSourceActor;

	FVector FixedBeamStart = FVector::ZeroVector;
	FVector FixedBeamEnd = FVector::ZeroVector;
	bool bUsesCursorEnd = false;
	bool bCueActive = false;
};
