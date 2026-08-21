#include "AbilitySystem/Abilities/AuraPassiveAbility.h"

#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "AbilitySystem/AuraAbilitySystemComponent.h"
#include "AbilitySystem/AuraAttributeSet.h"
#include "AbilitySystem/GameplayEffects/AuraPassiveGameplayEffect.h"
#include "AbilitySystemComponent.h"
#include "AuraGameplayTags.h"
#include "GameplayEffect.h"
#include "TimerManager.h"

UAuraPassiveAbility::UAuraPassiveAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

void UAuraPassiveAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	UAuraAbilitySystemComponent* AuraASC = Cast<UAuraAbilitySystemComponent>(GetAbilitySystemComponentFromActorInfo());
	if (AuraASC)
	{
		AuraASC->DeactivatePassiveAbility.RemoveAll(this);
		DeactivateDelegateHandle = AuraASC->DeactivatePassiveAbility.AddUObject(this, &UAuraPassiveAbility::ReceiveDeactivate);
	}

	PassiveAbilityTag = FGameplayTag();
	for (const FGameplayTag& Tag : GetAssetTags())
	{
		if (Tag.MatchesTag(FGameplayTag::RequestGameplayTag(FName("Abilities"))))
		{
			PassiveAbilityTag = Tag;
			break;
		}
	}

	AActor* AvatarActor = GetAvatarActorFromActorInfo();
	if (!AvatarActor || !AvatarActor->HasAuthority())
	{
		return;
	}

	const FAuraGameplayTags& Tags = FAuraGameplayTags::Get();
	if (PassiveAbilityTag.MatchesTagExact(Tags.Abilities_Passive_HaloOfProtection))
	{
		PassiveEventTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, Tags.Event_Passive_Halo_ShieldConsumed);
		PassiveEventTask->EventReceived.AddDynamic(this, &UAuraPassiveAbility::HandleHaloShieldConsumed);
		PassiveEventTask->ReadyForActivation();
		StartHaloRecharge();
	}
	else if (PassiveAbilityTag.MatchesTagExact(Tags.Abilities_Passive_LifeSiphon))
	{
		ApplyPassiveStateEffect(Tags.Effects_Passive_LifeSiphon);
		PassiveEventTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, Tags.Event_Combat_DamageDealt);
		PassiveEventTask->EventReceived.AddDynamic(this, &UAuraPassiveAbility::HandleDamageDealt);
		PassiveEventTask->ReadyForActivation();
	}
	else if (PassiveAbilityTag.MatchesTagExact(Tags.Abilities_Passive_ManaSiphon))
	{
		ApplyPassiveStateEffect(Tags.Effects_Passive_ManaSiphon);
		PassiveEventTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, Tags.Event_Combat_DamageDealt);
		PassiveEventTask->EventReceived.AddDynamic(this, &UAuraPassiveAbility::HandleDamageDealt);
		PassiveEventTask->ReadyForActivation();
	}

	const bool bBuiltInPassive = PassiveAbilityTag.MatchesTagExact(Tags.Abilities_Passive_HaloOfProtection)
		|| PassiveAbilityTag.MatchesTagExact(Tags.Abilities_Passive_LifeSiphon)
		|| PassiveAbilityTag.MatchesTagExact(Tags.Abilities_Passive_ManaSiphon);
	if (PassiveStateEffectClass && !bBuiltInPassive && !PassiveStateEffectHandle.IsValid())
	{
		UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
		FGameplayEffectContextHandle ContextHandle = ASC->MakeEffectContext();
		ContextHandle.AddSourceObject(AvatarActor);
		const FGameplayEffectSpecHandle SpecHandle = ASC->MakeOutgoingSpec(PassiveStateEffectClass, GetAbilityLevel(), ContextHandle);
		if (SpecHandle.IsValid())
		{
			PassiveStateEffectHandle = ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
		}
	}
}

void UAuraPassiveAbility::ApplyPassiveStateEffect(const FGameplayTag& GrantedTag)
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC || !GrantedTag.IsValid())
	{
		return;
	}

	FGameplayEffectContextHandle ContextHandle = ASC->MakeEffectContext();
	ContextHandle.AddSourceObject(GetAvatarActorFromActorInfo());
	FGameplayEffectSpecHandle SpecHandle = ASC->MakeOutgoingSpec(UAuraPassiveGameplayEffect::StaticClass(), GetAbilityLevel(), ContextHandle);
	if (SpecHandle.IsValid())
	{
		SpecHandle.Data->DynamicGrantedTags.AddTag(GrantedTag);
		PassiveStateEffectHandle = ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
	}
}

void UAuraPassiveAbility::StartHaloRecharge()
{
	if (UWorld* World = GetWorld())
	{
		const float RechargeSeconds = FMath::Max(3.f, ShieldRechargeTime.GetValueAtLevel(GetAbilityLevel()));
		World->GetTimerManager().SetTimer(HaloRechargeTimerHandle, this, &UAuraPassiveAbility::ApplyHaloShield, RechargeSeconds, false);
	}
}

void UAuraPassiveAbility::ApplyHaloShield()
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	const FGameplayTag ShieldTag = FAuraGameplayTags::Get().Effects_Passive_Halo_ShieldReady;
	if (!ASC || ASC->HasMatchingGameplayTag(ShieldTag))
	{
		return;
	}

	FGameplayEffectContextHandle ContextHandle = ASC->MakeEffectContext();
	ContextHandle.AddSourceObject(GetAvatarActorFromActorInfo());
	FGameplayEffectSpecHandle SpecHandle = ASC->MakeOutgoingSpec(UAuraPassiveGameplayEffect::StaticClass(), GetAbilityLevel(), ContextHandle);
	if (SpecHandle.IsValid())
	{
		SpecHandle.Data->DynamicGrantedTags.AddTag(ShieldTag);
		HaloShieldEffectHandle = ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
	}
}

void UAuraPassiveAbility::HandleDamageDealt(FGameplayEventData Payload)
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC || Payload.EventMagnitude <= 0.f)
	{
		return;
	}

	const FAuraGameplayTags& Tags = FAuraGameplayTags::Get();
	const bool bLifeSiphon = PassiveAbilityTag.MatchesTagExact(Tags.Abilities_Passive_LifeSiphon);
	const float DefaultPercent = bLifeSiphon ? 0.05f + 0.02f * (GetAbilityLevel() - 1) : 0.03f + 0.01f * (GetAbilityLevel() - 1);
	const float ConfiguredPercent = RestorePercent.GetValueAtLevel(GetAbilityLevel());
	const float MaxPercent = bLifeSiphon ? 0.25f : 0.15f;
	const float Percent = FMath::Clamp(ConfiguredPercent > 0.f ? ConfiguredPercent : DefaultPercent, 0.f, MaxPercent);
	const FGameplayAttribute Attribute = bLifeSiphon ? UAuraAttributeSet::GetHealthAttribute() : UAuraAttributeSet::GetManaAttribute();
	const FGameplayAttribute MaxAttribute = bLifeSiphon ? UAuraAttributeSet::GetMaxHealthAttribute() : UAuraAttributeSet::GetMaxManaAttribute();
	const float MissingAmount = FMath::Max(0.f, ASC->GetNumericAttribute(MaxAttribute) - ASC->GetNumericAttribute(Attribute));
	const float RestoreAmount = FMath::Min(Payload.EventMagnitude * Percent, MissingAmount);
	if (RestoreAmount > 0.f)
	{
		ASC->ApplyModToAttribute(Attribute, EGameplayModOp::Additive, RestoreAmount);
	}
}

void UAuraPassiveAbility::HandleHaloShieldConsumed(FGameplayEventData Payload)
{
	HaloShieldEffectHandle = FActiveGameplayEffectHandle();
	StartHaloRecharge();
}

void UAuraPassiveAbility::ReceiveDeactivate(const FGameplayTag& AbilityTag)
{
	if (PassiveAbilityTag.IsValid() && AbilityTag.MatchesTagExact(PassiveAbilityTag))
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
	}
}

void UAuraPassiveAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HaloRechargeTimerHandle);
	}
	if (PassiveEventTask)
	{
		PassiveEventTask->EndTask();
		PassiveEventTask = nullptr;
	}

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		if (PassiveStateEffectHandle.IsValid())
		{
			ASC->RemoveActiveGameplayEffect(PassiveStateEffectHandle);
		}
		if (HaloShieldEffectHandle.IsValid())
		{
			ASC->RemoveActiveGameplayEffect(HaloShieldEffectHandle);
		}
	}
	PassiveStateEffectHandle = FActiveGameplayEffectHandle();
	HaloShieldEffectHandle = FActiveGameplayEffectHandle();

	if (DeactivateDelegateHandle.IsValid())
	{
		if (UAuraAbilitySystemComponent* AuraASC = Cast<UAuraAbilitySystemComponent>(GetAbilitySystemComponentFromActorInfo()))
		{
			AuraASC->DeactivatePassiveAbility.Remove(DeactivateDelegateHandle);
		}
		DeactivateDelegateHandle.Reset();
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
