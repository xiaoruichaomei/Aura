// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/AuraNiagaraComponent.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AuraGameplayTags.h"
#include "NiagaraSystemInstanceController.h"
#include "TimerManager.h"

UAuraNiagaraComponent::UAuraNiagaraComponent()
{
	bAutoActivate = false;
}

void UAuraNiagaraComponent::BeginPlay()
{
	Super::BeginPlay();

	// Component BeginPlay runs while the owning actor is still inside Super::BeginPlay().
	// Defer binding so ABaseCharacter can assign the final tag for each component first.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(this, &UAuraNiagaraComponent::InitializeTagBinding);
	}
}

void UAuraNiagaraComponent::InitializeTagBinding()
{
	// 默认标签：燃烧（避免依赖外部设置的时序；可在蓝图里覆盖）
	if (!GameplayTag.IsValid())
	{
		GameplayTag = FAuraGameplayTags::Get().Effects_Debuff_Burn;
	}

	if (AActor* Owner = GetOwner())
	{
		UAbilitySystemComponent* ASC = const_cast<UAbilitySystemComponent*>(UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Owner));
		if (ASC)
		{
			ASC->RegisterGameplayTagEvent(GameplayTag, EGameplayTagEventType::NewOrRemoved)
				.AddUObject(this, &UAuraNiagaraComponent::OnTagChanged);

			// 初始状态：如果标签已经存在则立即激活
			OnTagChanged(GameplayTag, ASC->GetTagCount(GameplayTag));
		}
	}
}

void UAuraNiagaraComponent::OnTagChanged(const FGameplayTag Tag, int32 NewCount)
{
	if (NewCount > 0)
	{
		ActivateEffect();
		// 一次性特效（如眩晕星星）播完会消失：标签持续期间周期检查，播完自动重启形成持续显示。
		// 循环资产（如 NS_Fire）IsComplete 恒为 false，不会被打断重播。
		if (bLoopWhileActive)
		{
			if (UWorld* World = GetWorld())
			{
				World->GetTimerManager().SetTimer(LoopRestartTimerHandle, this, &UAuraNiagaraComponent::RestartEffectIfComplete, LoopRestartInterval, true);
			}
		}
	}
	else
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(LoopRestartTimerHandle);
		}
		if (bDeactivateImmediately)
		{
			DeactivateImmediate();
		}
		else
		{
			Deactivate();
		}
	}
}

void UAuraNiagaraComponent::ActivateEffect()
{
	Activate(true);
	if (InitialSimulationTime > 0.f)
	{
		AdvanceSimulationByTime(InitialSimulationTime, FMath::Max(InitialSimulationTickDelta, UE_SMALL_NUMBER));
	}
}

void UAuraNiagaraComponent::RestartEffectIfComplete()
{
	// 系统实例已播完才重启（一次性）；循环播放时 IsComplete() 为 false，保持原样
	if (FNiagaraSystemInstanceControllerPtr SystemController = GetSystemInstanceController())
	{
		if (SystemController->IsComplete())
		{
			ActivateEffect();
		}
	}
}
