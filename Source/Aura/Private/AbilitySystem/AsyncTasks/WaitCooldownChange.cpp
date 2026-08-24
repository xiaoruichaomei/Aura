


#include "AbilitySystem/AsyncTasks/WaitCooldownChange.h"
#include "AbilitySystemComponent.h"

UWaitCooldownChange* UWaitCooldownChange::WaitForCooldownChange(UAbilitySystemComponent* ASC, const FGameplayTag& CooldownTag)
{
	UWaitCooldownChange* WaitCooldownChange = NewObject<UWaitCooldownChange>();
	WaitCooldownChange->ASC = ASC;
	WaitCooldownChange->CooldownTag = CooldownTag;
	
	if (!IsValid(ASC) || !CooldownTag.IsValid())
	{
		WaitCooldownChange->EndTask();
		return nullptr;
	}
	
	ASC->RegisterGameplayTagEvent(CooldownTag, EGameplayTagEventType::NewOrRemoved).AddUObject(WaitCooldownChange, &UWaitCooldownChange::CooldownTagChanged);
	
	ASC->OnActiveGameplayEffectAddedDelegateToSelf.AddUObject(WaitCooldownChange, &UWaitCooldownChange::OnActiveEffectAdded);
	return WaitCooldownChange; 
}

void UWaitCooldownChange::Activate()
{
	Super::Activate();
	// Blueprint async output delegates are bound after the factory returns and
	// before Activate is called. Broadcasting in the factory loses this event,
	// which briefly resets an already-cooling icon to its bright brush.
	if (ASC && ASC->GetTagCount(CooldownTag) > 0)
	{
		BroadcastCooldownStart();
	}
}

void UWaitCooldownChange::EndTask()
{
	if (!ASC)
	{
		return;
	}
	
	ASC->RegisterGameplayTagEvent(CooldownTag, EGameplayTagEventType::NewOrRemoved).RemoveAll(this);
	ASC->OnActiveGameplayEffectAddedDelegateToSelf.RemoveAll(this);
	
	SetReadyToDestroy();
	MarkAsGarbage();
}

void UWaitCooldownChange::CooldownTagChanged(FGameplayTag InCooldownTag, int32 NewCount)
{
	if (NewCount > 0)
	{
		BroadcastCooldownStart();
	}
	else
	{
		bCooldownActive = false;
		CooldownEnd.Broadcast(0.f);
	}
}

void UWaitCooldownChange::BroadcastCooldownStart()
{
	if (!ASC || !CooldownTag.IsValid())
	{
		return;
	}
	FGameplayEffectQuery Query = FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(CooldownTag.GetSingleTagContainer());
	TArray<float> TimesRemaining = ASC->GetActiveEffectsTimeRemaining(Query);
	float TimeRemaining = 0.f;
	for (const float Time : TimesRemaining)
	{
		TimeRemaining = FMath::Max(TimeRemaining, Time);
	}
	if (TimeRemaining > 0.f)
	{
		if (bCooldownActive)
		{
			return;
		}
		bCooldownActive = true;
		CooldownStart.Broadcast(TimeRemaining);
	}
}

void UWaitCooldownChange::OnActiveEffectAdded(UAbilitySystemComponent* TargetASC, const FGameplayEffectSpec& SpecApplied, FActiveGameplayEffectHandle ActiveEffectHandle)
{
	FGameplayTagContainer AssetTags;
	SpecApplied.GetAllAssetTags(AssetTags);
	
	FGameplayTagContainer GrantedTags;
	SpecApplied.GetAllGrantedTags(GrantedTags);
	
	if (AssetTags.HasTagExact(CooldownTag) || GrantedTags.HasTagExact(CooldownTag))
	{
		BroadcastCooldownStart();
	}
}
