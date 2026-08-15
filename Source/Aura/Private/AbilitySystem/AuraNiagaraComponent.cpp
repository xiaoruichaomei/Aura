// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/AuraNiagaraComponent.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AuraGameplayTags.h"

UAuraNiagaraComponent::UAuraNiagaraComponent()
{
	bAutoActivate = false;
}

void UAuraNiagaraComponent::BeginPlay()
{
	Super::BeginPlay();

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
		Activate(true);
	}
	else
	{
		Deactivate();
	}
}
