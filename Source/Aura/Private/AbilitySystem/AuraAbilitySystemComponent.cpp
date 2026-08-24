// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/AuraAbilitySystemComponent.h"

#include "AuraGameplayTags.h"
#include "AbilitySystem/AuraAbilitySystemLibrary.h"
#include "AbilitySystem/Abilities/AuraGameplayAbility.h"
#include "AbilitySystem/Abilities/AuraArcaneShards.h"
#include "AbilitySystem/Data/AbilityInfo.h"
#include "Aura/AuraLogChannels.h"
#include "Interface/PlayerInterface.h"

void UAuraAbilitySystemComponent::AbilityActorInfoSet()
{
	OnGameplayEffectAppliedDelegateToSelf.AddUObject(this, &UAuraAbilitySystemComponent::ClientEffectApplied);

	// 角色加载/重生重新绑定 ActorInfo 后，恢复已装备被动的激活状态（能力 Spec 持久在 PlayerState 的 ASC 上）
	ActivateEquippedPassiveAbilities();
}

void UAuraAbilitySystemComponent::AddCharacterAbilities(const TArray<TSubclassOf<UGameplayAbility>>& StartupAbilities, const TArray<TSubclassOf<UGameplayAbility>>& PassiveAbilities)
{
	AddStartupAbilities(StartupAbilities);
	AddPassiveAbilities(PassiveAbilities);
}

void UAuraAbilitySystemComponent::ExportSavedAbilities(TArray<FSavedAbilityData>& OutAbilities)
{
	OutAbilities.Reset();
	FScopedAbilityListLock ActiveScopeLock(*this);
	for (const FGameplayAbilitySpec& Spec : GetActivatableAbilities())
	{
		const FGameplayTag AbilityTag = GetAbilityTagFromSpec(Spec);
		if (!AbilityTag.IsValid())
		{
			continue;
		}
		FSavedAbilityData Data;
		Data.AbilityTag = AbilityTag;
		Data.AbilityLevel = Spec.Level;
		Data.StatusTag = GetStatusFromSpec(Spec);
		Data.SlotTag = GetInputTagFromSpec(Spec);
		OutAbilities.Add(Data);
	}
}

void UAuraAbilitySystemComponent::RestoreSavedAbilities(const TArray<FSavedAbilityData>& SavedAbilities)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	// Saves created before version 2 appended the complete ability list on every
	// autosave. Keep only the newest record for each ability while restoring.
	TMap<FGameplayTag, FSavedAbilityData> UniqueAbilities;
	for (const FSavedAbilityData& Data : SavedAbilities)
	{
		if (Data.AbilityTag.IsValid())
		{
			UniqueAbilities.Add(Data.AbilityTag, Data);
		}
	}

	const UAbilityInfo* AbilityInfo = UAuraAbilitySystemLibrary::GetAbilityInfo(GetAvatarActor());
	for (const TPair<FGameplayTag, FSavedAbilityData>& Pair : UniqueAbilities)
	{
		const FSavedAbilityData& Data = Pair.Value;
		if (!Data.AbilityTag.IsValid())
		{
			continue;
		}
		FGameplayAbilitySpec* Spec = GetSpecFromAbilityTag(Data.AbilityTag);
		if (!Spec && AbilityInfo)
		{
			const FAuraAbilityInfo Info = AbilityInfo->FindAbilityInfoForTag(Data.AbilityTag);
			if (Info.Ability)
			{
				GiveAbility(FGameplayAbilitySpec(Info.Ability, FMath::Max(1, Data.AbilityLevel)));
				Spec = GetSpecFromAbilityTag(Data.AbilityTag);
			}
		}
		if (!Spec)
		{
			continue;
		}
		Spec->Level = FMath::Max(1, Data.AbilityLevel);
		const FGameplayTagContainer ExistingTags = Spec->GetDynamicSpecSourceTags();
		for (const FGameplayTag& Tag : ExistingTags)
		{
			if (Tag.MatchesTag(FGameplayTag::RequestGameplayTag(FName("Input"))) || Tag.MatchesTag(FGameplayTag::RequestGameplayTag(FName("Abilities.Status"))))
			{
				Spec->GetDynamicSpecSourceTags().RemoveTag(Tag);
			}
		}
		if (Data.SlotTag.IsValid())
		{
			Spec->GetDynamicSpecSourceTags().AddTag(Data.SlotTag);
		}
		FGameplayTag RestoredStatus = Data.StatusTag;
		if (Data.SlotTag.IsValid())
		{
			// A slotted ability is necessarily equipped. This also repairs old
			// saves that contained an input slot but no valid status tag.
			RestoredStatus = FAuraGameplayTags::Get().Abilities_Status_Equipped;
		}
		if (RestoredStatus.IsValid())
		{
			Spec->GetDynamicSpecSourceTags().AddTag(RestoredStatus);
		}
		MarkAbilitySpecDirty(*Spec);
	}

	// Startup abilities may be absent from old saves. Preserve their default
	// slot and equipped state instead of presenting them as locked in the UI.
	for (FGameplayAbilitySpec& Spec : GetActivatableAbilities())
	{
		const UAuraGameplayAbility* AuraAbility = Cast<UAuraGameplayAbility>(Spec.Ability);
		if (!AuraAbility || !AuraAbility->StartupInputTag.IsValid())
		{
			continue;
		}
		if (!GetInputTagFromSpec(Spec).IsValid())
		{
			Spec.GetDynamicSpecSourceTags().AddTag(AuraAbility->StartupInputTag);
		}
		if (!GetStatusFromSpec(Spec).IsValid())
		{
			Spec.GetDynamicSpecSourceTags().AddTag(FAuraGameplayTags::Get().Abilities_Status_Equipped);
		}
		MarkAbilitySpecDirty(Spec);
	}

	bStartupAbilitiesGiven = true;
	AbilitiesGiven.Broadcast();
	if (GetNetMode() != NM_Standalone)
	{
		ClientRefreshAbilityUI();
	}
	ActivateEquippedPassiveAbilities();
}

void UAuraAbilitySystemComponent::ClientRefreshAbilityUI_Implementation()
{
	bStartupAbilitiesGiven = true;
	AbilitiesGiven.Broadcast();
}

void UAuraAbilitySystemComponent::AbilityInputTagReleased(const FGameplayTag& InputTag)
{
	if (!InputTag.IsValid())
	{
		return;
	}
	for (auto& AbilitySpec : GetActivatableAbilities())
	{
		if (AbilitySpec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
		{
			AbilitySpecInputReleased(AbilitySpec);
		}
	}
}

void UAuraAbilitySystemComponent::AbilityInputTagPressed(const FGameplayTag& InputTag)
{
	if (!InputTag.IsValid())
	{
		return;
	}

	for (FGameplayAbilitySpec& AbilitySpec : GetActivatableAbilities())
	{
		if (AbilitySpec.GetDynamicSpecSourceTags().HasTagExact(InputTag) && AbilitySpec.IsActive())
		{
			AbilitySpecInputPressed(AbilitySpec);
		}
	}
}

void UAuraAbilitySystemComponent::AbilityInputTagHeld(const FGameplayTag& InputTag)
{
	if (!InputTag.IsValid())
	{
		return;
	}
	for (auto& AbilitySpec : GetActivatableAbilities())
	{
		if (AbilitySpec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
		{
			if (!AbilitySpec.IsActive())
			{
				TryActivateAbility(AbilitySpec.Handle);
			}
		}
	}
}

void UAuraAbilitySystemComponent::ConfirmArcaneShardsTarget(const FVector& TargetLocation)
{
	if (TargetLocation.ContainsNaN()) return;
	if (IsOwnerActorAuthoritative())
	{
		ServerConfirmArcaneShardsTarget_Implementation(TargetLocation);
	}
	else
	{
		ServerConfirmArcaneShardsTarget(TargetLocation);
	}
}

void UAuraAbilitySystemComponent::ServerConfirmArcaneShardsTarget_Implementation(FVector_NetQuantize TargetLocation)
{
	for (FGameplayAbilitySpec& Spec : GetActivatableAbilities())
	{
		if (!Spec.IsActive()) continue;
		for (UGameplayAbility* Instance : Spec.GetAbilityInstances())
		{
			if (UAuraArcaneShards* ArcaneShards = Cast<UAuraArcaneShards>(Instance))
			{
				ArcaneShards->ConfirmTargetFromServer(FVector(TargetLocation));
				return;
			}
		}
	}
}

void UAuraAbilitySystemComponent::ForEachAbility(const FForEachAbility& Delegate)
{
	FScopedAbilityListLock ActiveScopeLock(*this);
	for (const FGameplayAbilitySpec& Spec : GetActivatableAbilities())
	{
		if (!Delegate.ExecuteIfBound(Spec))
		{
			UE_LOG(LogAura, Error, TEXT("Fail to execute delegate in %hs"), __FUNCTION__);
		}
	}
}

void UAuraAbilitySystemComponent::UpgradeAttribute(const FGameplayTag& AttributeTag)
{
	if (GetAvatarActor()->Implements<UPlayerInterface>())
	{
		if (IPlayerInterface::Execute_GetAttributePoints(GetAvatarActor()) > 0)
		{
			ServerUpgradeAttribute(AttributeTag);
		}
	}
}

void UAuraAbilitySystemComponent::ServerUpgradeAttribute_Implementation(const FGameplayTag& AttributeTag)
{
	FGameplayEventData Payload;
	Payload.EventTag = AttributeTag;
	Payload.EventMagnitude = 1.f;
	
	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(GetAvatarActor(), AttributeTag, Payload);
	
	if (GetAvatarActor()->Implements<UPlayerInterface>())
	{
		IPlayerInterface::Execute_AddToAttributePoints(GetAvatarActor(), -1);
	}
}

FGameplayTag UAuraAbilitySystemComponent::GetAbilityTagFromSpec(const FGameplayAbilitySpec& AbilitySpec)
{
	if (AbilitySpec.Ability)
	{
		for (FGameplayTag Tag : AbilitySpec.Ability.Get()->GetAssetTags())
		{
			if (Tag.MatchesTag(FGameplayTag::RequestGameplayTag(FName("Abilities"))))
			{
				return Tag;
			}
		}
	}
	return FGameplayTag();
}

FGameplayTag UAuraAbilitySystemComponent::GetInputTagFromSpec(const FGameplayAbilitySpec& AbilitySpec)
{
	for (FGameplayTag Tag : AbilitySpec.GetDynamicSpecSourceTags())
	{
		if (Tag.MatchesTag(FGameplayTag::RequestGameplayTag(FName("Input"))))
		{
			return Tag;
		}
	}
	return FGameplayTag();
}

FGameplayTag UAuraAbilitySystemComponent::GetStatusFromSpec(const FGameplayAbilitySpec& AbilitySpec)
{
	for (const FGameplayTag& Tag : AbilitySpec.GetDynamicSpecSourceTags())
	{
		if (Tag.MatchesTag(FGameplayTag::RequestGameplayTag(FName("Abilities.Status"))))
		{
			return Tag;
		}
	}
	return FGameplayTag();
}

FGameplayTag UAuraAbilitySystemComponent::GetStatusFromAbilityTag(const FGameplayTag& AbilityTag)
{
	if (const FGameplayAbilitySpec* Spec = GetSpecFromAbilityTag(AbilityTag))
	{
		return GetStatusFromSpec(*Spec);
	}
	return FGameplayTag();
}

FGameplayTag UAuraAbilitySystemComponent::GetInputTagFromAbilityTag(const FGameplayTag& AbilityTag)
{
	if (const FGameplayAbilitySpec* Spec = GetSpecFromAbilityTag(AbilityTag))
	{
		return GetInputTagFromSpec(*Spec);
	}
	return FGameplayTag();
}

FGameplayAbilitySpec* UAuraAbilitySystemComponent::GetSpecFromAbilityTag(const FGameplayTag& AbilityTag)
{
	FScopedAbilityListLock ActiveScopeLock(*this);
	for (FGameplayAbilitySpec& AbilitySpec : GetActivatableAbilities())
	{
		for (FGameplayTag Tag : AbilitySpec.Ability.Get()->GetAssetTags())
		{
			if (Tag.MatchesTag(AbilityTag))
			{
				return &AbilitySpec;
			}
		}
	}
	return nullptr;
}

FGameplayAbilitySpec* UAuraAbilitySystemComponent::GetSpecWithSlot(const FGameplayTag& Slot)
{
	FScopedAbilityListLock ActiveScopeLock(*this);
	for (FGameplayAbilitySpec& AbilitySpec : GetActivatableAbilities())
	{
		if (AbilityHasSlot(&AbilitySpec, Slot))
		{
			return &AbilitySpec;
		}
	}
	return nullptr;
}

bool UAuraAbilitySystemComponent::IsPassiveSlot(const FGameplayTag& Slot) const
{
	return Slot.MatchesTag(FGameplayTag::RequestGameplayTag(FName("Input.Passive")));
}

bool UAuraAbilitySystemComponent::IsPassiveAbility(const FGameplayTag& AbilityTag) const
{
	if (!AbilityTag.IsValid())
	{
		return false;
	}
	const UAbilityInfo* AbilityInfoData = UAuraAbilitySystemLibrary::GetAbilityInfo(GetAvatarActor());
	return AbilityInfoData && AbilityInfoData->FindAbilityInfoForTag(AbilityTag).AbilityType.MatchesTagExact(FAuraGameplayTags::Get().Abilities_Type_Passive);
}

void UAuraAbilitySystemComponent::SetAbilityStatus(FGameplayAbilitySpec* Spec, const FGameplayTag& StatusTag)
{
	if (!Spec)
	{
		return;
	}
	const FGameplayTag PrevStatus = GetStatusFromSpec(*Spec);
	if (PrevStatus.IsValid())
	{
		Spec->GetDynamicSpecSourceTags().RemoveTag(PrevStatus);
	}
	Spec->GetDynamicSpecSourceTags().AddTag(StatusTag);
	MarkAbilitySpecDirty(*Spec);
}

void UAuraAbilitySystemComponent::ActivateEquippedPassiveAbilities()
{
	if (!GetAvatarActor())
	{
		return;
	}
	const FAuraGameplayTags& GameplayTags = FAuraGameplayTags::Get();
	FScopedAbilityListLock ActiveScopeLock(*this);
	for (FGameplayAbilitySpec& Spec : GetActivatableAbilities())
	{
		if (Spec.IsActive())
		{
			continue;
		}
		const FGameplayTag AbilityTag = GetAbilityTagFromSpec(Spec);
		if (!AbilityTag.IsValid())
		{
			continue;
		}
		if (!GetStatusFromSpec(Spec).MatchesTagExact(GameplayTags.Abilities_Status_Equipped))
		{
			continue;
		}
		if (!IsPassiveAbility(AbilityTag))
		{
			continue;
		}
		if (!IsPassiveSlot(GetInputTagFromSpec(Spec)))
		{
			continue;
		}
		TryActivateAbility(Spec.Handle);
	}
}

void UAuraAbilitySystemComponent::UpdateAbilityStatus(int32 Level)
{
	UAbilityInfo* AbilitiesInfo = UAuraAbilitySystemLibrary::GetAbilityInfo(GetAvatarActor());
	for (const FAuraAbilityInfo& Info : AbilitiesInfo->AbilityInformation)
	{
		if (Level < Info.LevelRequirement || !Info.AbilityTag.IsValid())
		{
			continue;
		}
		if (!GetSpecFromAbilityTag(Info.AbilityTag))
		{
			FGameplayAbilitySpec AbilitySpec = FGameplayAbilitySpec(Info.Ability, 1);
			AbilitySpec.GetDynamicSpecSourceTags().AddTag(FAuraGameplayTags::Get().Abilities_Status_Eligible);
			GiveAbility(AbilitySpec);
			MarkAbilitySpecDirty(AbilitySpec);
			BroadcastAbilityStatus(Info.AbilityTag, FAuraGameplayTags::Get().Abilities_Status_Eligible, 1);
		}
	}
}

void UAuraAbilitySystemComponent::ServerEquipAbility_Implementation(const FGameplayTag& AbilityTag, const FGameplayTag& Slot)
{
	const FAuraGameplayTags& GameplayTags = FAuraGameplayTags::Get();

	FGameplayAbilitySpec* NewSpec = GetSpecFromAbilityTag(AbilityTag);
	if (!NewSpec || !Slot.IsValid())
	{
		return;
	}

	// 目标槽位里的旧技能
	FGameplayAbilitySpec* OldSpec = GetSpecWithSlot(Slot);

	// 记录新旧技能的旧状态，用于被动激活失败时回滚
	const FGameplayTag NewPrevSlot = GetInputTagFromSpec(*NewSpec);
	const FGameplayTag NewPrevStatus = GetStatusFromSpec(*NewSpec);
	const FGameplayTag OldPrevSlot = OldSpec ? GetInputTagFromSpec(*OldSpec) : FGameplayTag();
	const FGameplayTag OldPrevStatus = OldSpec ? GetStatusFromSpec(*OldSpec) : FGameplayTag();

	// 1) 旧技能与新技能不同：停用旧被动、释放槽位、状态回 Unlocked
	if (OldSpec && OldSpec != NewSpec)
	{
		const FGameplayTag OldAbilityTag = GetAbilityTagFromSpec(*OldSpec);
		if (OldAbilityTag.IsValid() && IsPassiveAbility(OldAbilityTag))
		{
			DeactivatePassiveAbility.Broadcast(OldAbilityTag);
		}
		ClearSlot(OldSpec);
		SetAbilityStatus(OldSpec, GameplayTags.Abilities_Status_Unlocked);
	}

	// 2) 装备新技能：清原槽位 → 加新槽位 → 状态 Equipped
	ClearSlot(NewSpec);
	NewSpec->GetDynamicSpecSourceTags().AddTag(Slot);
	SetAbilityStatus(NewSpec, GameplayTags.Abilities_Status_Equipped);

	// 3) 被动激活；同一被动换槽位时 IsActive() 仍为 true，不会结束再重启
	const bool bIsPassive = IsPassiveAbility(AbilityTag);
	if (bIsPassive && !NewSpec->IsActive())
	{
		if (!TryActivateAbility(NewSpec->Handle))
		{
			// 激活失败：回滚到装备前状态，不广播装备成功
			NewSpec->GetDynamicSpecSourceTags().RemoveTag(Slot);
			if (NewPrevSlot.IsValid())
			{
				NewSpec->GetDynamicSpecSourceTags().AddTag(NewPrevSlot);
			}
			SetAbilityStatus(NewSpec, NewPrevStatus);

			if (OldSpec)
			{
				if (OldPrevSlot.IsValid())
				{
					OldSpec->GetDynamicSpecSourceTags().AddTag(OldPrevSlot);
				}
				SetAbilityStatus(OldSpec, OldPrevStatus);
				// 旧被动之前处于激活状态，回滚时恢复其激活
				if (OldPrevStatus.MatchesTagExact(GameplayTags.Abilities_Status_Equipped) && IsPassiveAbility(GetAbilityTagFromSpec(*OldSpec)) && !OldSpec->IsActive())
				{
					TryActivateAbility(OldSpec->Handle);
				}
			}
			return;
		}
	}

	ClientEquipAbility(AbilityTag, GameplayTags.Abilities_Status_Equipped, Slot, NewPrevSlot);
}

void UAuraAbilitySystemComponent::ClientEquipAbility_Implementation(const FGameplayTag& AbilityTag, const FGameplayTag& Status, const FGameplayTag& Slot, const FGameplayTag& PreviousSlot)
{
	AbilityEquipped.Broadcast(AbilityTag, Status, Slot, PreviousSlot);
}

bool UAuraAbilitySystemComponent::GetDescriptionsByAbilityTag(const FGameplayTag& AbilityTag, FString& OutDescription, FString& OutNextLevelDescription)
{
	const UAbilityInfo* AbilityInfo = UAuraAbilitySystemLibrary::GetAbilityInfo(GetAvatarActor());
	if (!AbilityInfo)
	{
		UE_LOG(LogAura, Error, TEXT("GetDescriptionsByAbilityTag: AbilityInfo is null (ability tag %s)"), *AbilityTag.ToString());
		return false;
	}
	const FAuraAbilityInfo Info = AbilityInfo->FindAbilityInfoForTag(AbilityTag);
	if (const FGameplayAbilitySpec* AbilitySpec = GetSpecFromAbilityTag(AbilityTag))
	{
		if (UAuraGameplayAbility* AuraAbility = Cast<UAuraGameplayAbility>(AbilitySpec->Ability))
		{
			// 达到解锁等级但尚未花费技能点:本级提示解锁,下级显示 1 级数值
			if (GetStatusFromSpec(*AbilitySpec).MatchesTagExact(FAuraGameplayTags::Get().Abilities_Status_Eligible))
			{
				OutDescription = UAuraGameplayAbility::GetUnlockDescription();
				OutNextLevelDescription = AuraAbility->GetDescription(AbilitySpec->Level, Info);
				return true;
			}
			OutDescription = AuraAbility->GetDescription(AbilitySpec->Level, Info);
			OutNextLevelDescription = AuraAbility->GetNextLevelDescription(AbilitySpec->Level + 1, Info);
			return true;
		}
	}
	if (!AbilityTag.IsValid() || AbilityTag.MatchesTagExact(FAuraGameplayTags::Get().Abilities_None))
	{
		OutDescription = FString();
	}
	else
	{
		OutDescription = UAuraGameplayAbility::GetLockedDescription(Info.LevelRequirement);
	}
	OutNextLevelDescription = FString();
	return false;
}

void UAuraAbilitySystemComponent::ClearSlot(FGameplayAbilitySpec* Spec)
{
	const FGameplayTag Slot = GetInputTagFromSpec(*Spec);
	Spec->GetDynamicSpecSourceTags().RemoveTag(Slot);
	MarkAbilitySpecDirty(*Spec);
}

void UAuraAbilitySystemComponent::ClearAbilitiesOfSlot(const FGameplayTag& Slot)
{
	FScopedAbilityListLock ActiveScopeLock(*this);
	for (FGameplayAbilitySpec& Spec : GetActivatableAbilities())
	{
		if (AbilityHasSlot(&Spec, Slot))
		{
			ClearSlot(&Spec);
		}
	}
}

bool UAuraAbilitySystemComponent::AbilityHasSlot(FGameplayAbilitySpec* Spec, const FGameplayTag& Slot)
{
	for (const FGameplayTag& Tag : Spec->GetDynamicSpecSourceTags())
	{
		if (Tag.MatchesTagExact(Slot))
		{
			return true;
		}
	}
	return false;
}

void UAuraAbilitySystemComponent::ServerSpendSpellPoint_Implementation(const FGameplayTag& AbilityTag)
{
	if (FGameplayAbilitySpec* AbilitySpec = GetSpecFromAbilityTag(AbilityTag))
	{
		if (GetAvatarActor()->Implements<UPlayerInterface>())
		{
			IPlayerInterface::Execute_AddToSpellPoints(GetAvatarActor(), -1);	
		}
		
		const FAuraGameplayTags GameplayTags = FAuraGameplayTags::Get();
		FGameplayTag Status = GetStatusFromSpec(*AbilitySpec);
		if (Status.MatchesTagExact(GameplayTags.Abilities_Status_Eligible))
		{
			AbilitySpec->GetDynamicSpecSourceTags().RemoveTag(GameplayTags.Abilities_Status_Eligible);
			AbilitySpec->GetDynamicSpecSourceTags().AddTag(GameplayTags.Abilities_Status_Unlocked);
			Status = GameplayTags.Abilities_Status_Unlocked;
		}
		else if (Status.MatchesTagExact(GameplayTags.Abilities_Status_Equipped) || Status.MatchesTagExact(GameplayTags.Abilities_Status_Unlocked))
		{
			AbilitySpec->Level += 1;
		}
		BroadcastAbilityStatus(AbilityTag, Status, AbilitySpec->Level);
		MarkAbilitySpecDirty(*AbilitySpec);
	}
}

void UAuraAbilitySystemComponent::OnRep_ActivateAbilities()
{
	Super::OnRep_ActivateAbilities();

	// The client refresh RPC can arrive before the replicated AbilitySpec list.
	// Broadcast on every list replication so the overlay cannot remain empty.
	bStartupAbilitiesGiven = true;
	AbilitiesGiven.Broadcast();

	// AbilitySpec 复制完成后，恢复已装备被动的激活状态
	ActivateEquippedPassiveAbilities();
}

void UAuraAbilitySystemComponent::ClientEffectApplied_Implementation(UAbilitySystemComponent* AbilitySystemComponent, const FGameplayEffectSpec& EffectSpec, FActiveGameplayEffectHandle ActiveEffectHandle) const
{
	FGameplayTagContainer TagContainer;
	EffectSpec.GetAllAssetTags(TagContainer);
	
	EffectAssetTags.Broadcast(TagContainer);
}

void UAuraAbilitySystemComponent::ClientUpdateAbilityStatus_Implementation(const FGameplayTag& AbilityTag, const FGameplayTag& StatusTag, int32 AbilityLevel)
{
	AbilityStatusChanged.Broadcast(AbilityTag, StatusTag, AbilityLevel);
}

void UAuraAbilitySystemComponent::BroadcastAbilityStatus(const FGameplayTag& AbilityTag, const FGameplayTag& StatusTag, int32 AbilityLevel)
{
	// 本地直接广播,保证 Standalone / Listen Server 主机也能收到状态变化通知
	AbilityStatusChanged.Broadcast(AbilityTag, StatusTag, AbilityLevel);
	// 复制到远端客户端(Standalone 无连接时该 RPC 会被安全丢弃)
	ClientUpdateAbilityStatus(AbilityTag, StatusTag, AbilityLevel);
}

void UAuraAbilitySystemComponent::AddStartupAbilities(const TArray<TSubclassOf<UGameplayAbility>>& StartupAbilities)
{
	for (const auto& AbilityClass : StartupAbilities)
	{
		FGameplayAbilitySpec AbilitySpec = FGameplayAbilitySpec(AbilityClass, 1);
		const UAuraGameplayAbility* AuraAbility = Cast<UAuraGameplayAbility>(AbilitySpec.Ability);
		if (AuraAbility)
		{
			AbilitySpec.GetDynamicSpecSourceTags().AddTag(AuraAbility->StartupInputTag);
			AbilitySpec.GetDynamicSpecSourceTags().AddTag(FAuraGameplayTags::Get().Abilities_Status_Equipped);
			GiveAbility(AbilitySpec);
		}
	}
	bStartupAbilitiesGiven = true;
	AbilitiesGiven.Broadcast();
}

void UAuraAbilitySystemComponent::AddPassiveAbilities(const TArray<TSubclassOf<UGameplayAbility>>& PassiveAbilities)
{
	for (const auto& AbilityClass : PassiveAbilities)
	{
		FGameplayAbilitySpec AbilitySpec = FGameplayAbilitySpec(AbilityClass, 1);
		if (const UAuraGameplayAbility* AuraAbility = Cast<UAuraGameplayAbility>(AbilitySpec.Ability))
		{
			if (AuraAbility->StartupInputTag.IsValid())
			{
				AbilitySpec.GetDynamicSpecSourceTags().AddTag(AuraAbility->StartupInputTag);
			}
			AbilitySpec.GetDynamicSpecSourceTags().AddTag(FAuraGameplayTags::Get().Abilities_Status_Equipped);
		}
		GiveAbilityAndActivateOnce(AbilitySpec);
	}
}
