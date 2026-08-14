


#include "UI/WidgetController/SpellMenuWidgetController.h"

#include "AuraGameplayTags.h"
#include "AbilitySystem/AuraAbilitySystemComponent.h"
#include "AbilitySystem/Data/AbilityInfo.h"
#include "Player/AuraPlayerController.h"
#include "Player/AuraPlayerState.h"

void USpellMenuWidgetController::BroadcastInitialValues()
{
	BroadcastAbilityInfo();
	SpellPointsChanged.Broadcast(GetAuraPS()->GetSpellPoints());
}

void USpellMenuWidgetController::BindCallbacksToDependencies()
{
	GetAuraASC()->AbilityStatusChanged.AddLambda([this](const FGameplayTag& AbilityTag, const FGameplayTag& StatusTag, int32 NewLevel)
	{
		if (SelectedAbility.Ability.MatchesTagExact(AbilityTag))
		{
			SelectedAbility.Status = StatusTag;
			bool bShouldEnableSpellPointsButton = true;
			bool bShouldEnableEquipButton = true;
			ShouldEnabledButtons(StatusTag, CurrentSpellPoints, bShouldEnableSpellPointsButton, bShouldEnableEquipButton);
			FString Description;
			FString NextLevelDescription;
			GetAuraASC()->GetDescriptionsByAbilityTag(AbilityTag, Description, NextLevelDescription);
			SpellGlobeSelectedDelegate.Broadcast(bShouldEnableSpellPointsButton, bShouldEnableEquipButton, Description, NextLevelDescription);
		}
		
		if (AbilityInfo)
		{
			FAuraAbilityInfo Info = AbilityInfo->FindAbilityInfoForTag(AbilityTag);
			Info.StatusTag = StatusTag;
			AbilityInfoDelegate.Broadcast(Info);
		}
	});
	
	GetAuraASC()->AbilityEquipped.AddUObject(this, &USpellMenuWidgetController::OnAbilityEquipped);
	
	GetAuraPS()->OnSpellPointsChangedDelegate.AddLambda([this](int32 SpellPoints)
	{
		SpellPointsChanged.Broadcast(SpellPoints);
		CurrentSpellPoints = SpellPoints;
		bool bShouldEnableSpellPointsButton = true;
		bool bShouldEnableEquipButton = true;
		ShouldEnabledButtons(SelectedAbility.Status, CurrentSpellPoints, bShouldEnableSpellPointsButton, bShouldEnableEquipButton);
		FString Description;
		FString NextLevelDescription;
		GetAuraASC()->GetDescriptionsByAbilityTag(SelectedAbility.Ability, Description, NextLevelDescription);
		SpellGlobeSelectedDelegate.Broadcast(bShouldEnableSpellPointsButton, bShouldEnableEquipButton, Description, NextLevelDescription);
	});

	// 等级提升后,所选技能可能从 Locked 变为 Eligible。
	// 注意 OnLevelChangedDelegate 在 UpdateAbilityStatus 创建 Eligible spec 之前广播,
	// 因此推迟到下一帧重新选中,确保 spec 已经就绪。
	GetAuraPS()->OnLevelChangedDelegate.AddLambda([this](int32 NewLevel)
	{
		if (SelectedAbility.Ability.IsValid())
		{
			if (AAuraPlayerController* AuraPC = GetAuraPC())
			{
				if (UWorld* World = AuraPC->GetWorld())
				{
					World->GetTimerManager().SetTimerForNextTick([this]()
					{
						SpellGlobeSelected(SelectedAbility.Ability);
					});
				}
			}
		}
	});
}

void USpellMenuWidgetController::SpellGlobeSelected(const FGameplayTag& AbilityTag)
{
	if (bWaitingForEquipSelection)
	{
		const FGameplayTag SelectedAbilityType = AbilityInfo->FindAbilityInfoForTag(AbilityTag).AbilityType;
		StopWaitingEquipDelegate.Broadcast(SelectedAbilityType);
		bWaitingForEquipSelection = false;
	}
	
	const FAuraGameplayTags GameplayTag = FAuraGameplayTags::Get();
	const bool bTagValid = AbilityTag.IsValid();
	const bool bTagNone = AbilityTag.MatchesTag(GameplayTag.Abilities_None);
	const FGameplayAbilitySpec* AbilitySpec = GetAuraASC()->GetSpecFromAbilityTag(AbilityTag);
	const bool bSpecValid = AbilitySpec != nullptr;
	FGameplayTag AbilityStatus;
	
	if (!bTagValid || bTagNone || !bSpecValid)
	{
		AbilityStatus = GameplayTag.Abilities_Status_Lock;
	}
	else
	{
		AbilityStatus = GetAuraASC()->GetStatusFromSpec(*AbilitySpec);
	}
	
	SelectedAbility.Ability = AbilityTag;
	SelectedAbility.Status = AbilityStatus;
	
	bool bShouldEnableSpellPointsButton = true;
	bool bShouldEnableEquipButton = true;
	ShouldEnabledButtons(AbilityStatus, GetAuraPS()->GetSpellPoints(), bShouldEnableSpellPointsButton, bShouldEnableEquipButton);
	FString Description;
	FString NextLevelDescription;
	GetAuraASC()->GetDescriptionsByAbilityTag(AbilityTag, Description, NextLevelDescription);
	SpellGlobeSelectedDelegate.Broadcast(bShouldEnableSpellPointsButton, bShouldEnableEquipButton, Description, NextLevelDescription);
}

void USpellMenuWidgetController::SpendPointButtonPressed()
{
	if (GetAuraASC())
	{
		GetAuraASC()->ServerSpendSpellPoint(SelectedAbility.Ability);
	}
}

void USpellMenuWidgetController::GlobeDeselect()
{
	if (bWaitingForEquipSelection)
	{
		const FGameplayTag SelectedAbilityType = AbilityInfo->FindAbilityInfoForTag(SelectedAbility.Ability).AbilityType;
		StopWaitingEquipDelegate.Broadcast(SelectedAbilityType);
		bWaitingForEquipSelection = false;
	}
	
	SelectedAbility.Ability = FAuraGameplayTags::Get().Abilities_None;
	SelectedAbility.Status = FAuraGameplayTags::Get().Abilities_Status_Lock;
	SpellGlobeSelectedDelegate.Broadcast(false, false, FString(), FString());
}

void USpellMenuWidgetController::EquipButtonPressed()
{
	const FGameplayTag AbilityType = AbilityInfo->FindAbilityInfoForTag(SelectedAbility.Ability).AbilityType;
	
	WaitForEquipDelegate.Broadcast(AbilityType);
	bWaitingForEquipSelection = true;
	
	const FGameplayTag SelectedStatus = GetAuraASC()->GetStatusFromAbilityTag(SelectedAbility.Ability);
	if (SelectedStatus.MatchesTagExact(FAuraGameplayTags::Get().Abilities_Status_Equipped))
	{
		SelectedSlot = GetAuraASC()->GetInputTagFromAbilityTag(SelectedAbility.Ability);
	}
}

void USpellMenuWidgetController::SpellRowGlobePressed(const FGameplayTag& SlotTag, const FGameplayTag& AbilityType)
{
	if (!bWaitingForEquipSelection)
	{
		return;
	}
	
	const FGameplayTag& SelectedAbilityType = AbilityInfo->FindAbilityInfoForTag(SelectedAbility.Ability).AbilityType;
	if (!SelectedAbilityType.MatchesTagExact(AbilityType))
	{
		return;
	}
	
	GetAuraASC()->ServerEquipAbility(SelectedAbility.Ability, SlotTag);
}

void USpellMenuWidgetController::OnAbilityEquipped(const FGameplayTag& AbilityTag, const FGameplayTag& Status, const FGameplayTag& Slot, const FGameplayTag& PreviousSlot)
{
	bWaitingForEquipSelection = false;
	
	const FAuraGameplayTags& GameplayTags = FAuraGameplayTags::Get();
	
	FAuraAbilityInfo LastSlotInfo;
	LastSlotInfo.StatusTag = GameplayTags.Abilities_Status_Unlocked;
	LastSlotInfo.InputTag = PreviousSlot;
	LastSlotInfo.AbilityTag = GameplayTags.Abilities_None;
	
	AbilityInfoDelegate.Broadcast(LastSlotInfo);
	
	FAuraAbilityInfo Info = AbilityInfo->FindAbilityInfoForTag(AbilityTag);
	Info.StatusTag = Status;
	Info.InputTag = Slot;
	AbilityInfoDelegate.Broadcast(Info);
	
	StopWaitingEquipDelegate.Broadcast(AbilityInfo->FindAbilityInfoForTag(AbilityTag).AbilityType);
	SpellGlobeReassignedDelegate.Broadcast(AbilityTag);	
	GlobeDeselect();
}

void USpellMenuWidgetController::ShouldEnabledButtons(const FGameplayTag& AbilityStatus, int32 SpellPoints, bool& bShouldEnableSpellPointsButton, bool& bShouldEnableEquipButton)
{
	const FAuraGameplayTags GameplayTag = FAuraGameplayTags::Get();
	
	if (AbilityStatus.MatchesTagExact(GameplayTag.Abilities_Status_Eligible))
	{
		bShouldEnableEquipButton = false;
		if (SpellPoints <= 0)
		{
			bShouldEnableSpellPointsButton = false;
		}
	}
	else if (AbilityStatus.MatchesTagExact(GameplayTag.Abilities_Status_Lock))
	{
		bShouldEnableEquipButton = false;
		bShouldEnableSpellPointsButton = false;
	}
	else
	{
		if (SpellPoints <= 0)
		{
			bShouldEnableSpellPointsButton = false;
		}
	}
}
