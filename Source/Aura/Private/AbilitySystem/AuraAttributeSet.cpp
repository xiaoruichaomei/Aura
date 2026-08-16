// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/AuraAttributeSet.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AIController.h"
#include "GameplayEffect.h"
#include "GameplayEffectExtension.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Navigation/PathFollowingComponent.h"
#include "Net/UnrealNetwork.h"
#include "AuraGameplayTags.h"
#include "AbilitySystem/AuraAbilitySystemComponent.h"
#include "AbilitySystem/AuraAbilitySystemLibrary.h"
#include "Interface/CombatInterface.h"
#include "Interface/PlayerInterface.h"
#include "Player/AuraPlayerController.h"

UAuraAttributeSet::UAuraAttributeSet()
{
    const FAuraGameplayTags& GameplayTags = FAuraGameplayTags::Get();
	
	TagsToAttributes.Add(GameplayTags.Attributes_Primary_Strength, GetStrengthAttribute);
	TagsToAttributes.Add(GameplayTags.Attributes_Primary_Intelligence, GetIntelligenceAttribute);
	TagsToAttributes.Add(GameplayTags.Attributes_Primary_Resilience, GetResilienceAttribute);
	TagsToAttributes.Add(GameplayTags.Attributes_Primary_Vigor, GetVigorAttribute);
	
	TagsToAttributes.Add(GameplayTags.Attributes_Secondary_Armor, GetArmorAttribute);
	TagsToAttributes.Add(GameplayTags.Attributes_Secondary_ArmorPenetration, GetArmorPenetrationAttribute);
	TagsToAttributes.Add(GameplayTags.Attributes_Secondary_BlockChance, GetBlockChanceAttribute);
	TagsToAttributes.Add(GameplayTags.Attributes_Secondary_CriticalHitChance, GetCriticalHitChanceAttribute);
	TagsToAttributes.Add(GameplayTags.Attributes_Secondary_CriticalHitDamage, GetCriticalHitDamageAttribute);
	TagsToAttributes.Add(GameplayTags.Attributes_Secondary_CriticalHitResistance, GetCriticalHitResistanceAttribute);
	TagsToAttributes.Add(GameplayTags.Attributes_Secondary_HealthRegeneration, GetHealthRegenerationAttribute);
	TagsToAttributes.Add(GameplayTags.Attributes_Secondary_ManaRegeneration, GetManaRegenerationAttribute);
	TagsToAttributes.Add(GameplayTags.Attributes_Secondary_MaxHealth, GetMaxHealthAttribute);
	TagsToAttributes.Add(GameplayTags.Attributes_Secondary_MaxMana, GetMaxManaAttribute);
	
	TagsToAttributes.Add(GameplayTags.Attributes_Resistance_Fire, GetFireResistanceAttribute);
	TagsToAttributes.Add(GameplayTags.Attributes_Resistance_Lightning, GetLightningResistanceAttribute);
	TagsToAttributes.Add(GameplayTags.Attributes_Resistance_Arcane, GetArcaneResistanceAttribute);
	TagsToAttributes.Add(GameplayTags.Attributes_Resistance_Physical, GetPhysicalResistanceAttribute);
}

void UAuraAttributeSet::GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	
	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, Health, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, Mana, COND_None, REPNOTIFY_Always);
	
	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, Strength, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, Intelligence, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, Resilience, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, Vigor, COND_None, REPNOTIFY_Always);
	
	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, Armor, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, ArmorPenetration, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, BlockChance, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, CriticalHitChance, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, CriticalHitDamage, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, CriticalHitResistance, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, HealthRegeneration, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, ManaRegeneration, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, MaxHealth, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, MaxMana, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, FireResistance, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, LightningResistance, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, ArcaneResistance, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, PhysicalResistance, COND_None, REPNOTIFY_Always);
}

void UAuraAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);
	
	if (Attribute == GetHealthAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxHealth());
	}
	if (Attribute == GetManaAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxMana());
	}
}

void UAuraAttributeSet::PostGameplayEffectExecute(const struct FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);
	
	FEffectProperties Props;
	SetEffectProperties(Data, Props);
	
	if (Data.EvaluatedData.Attribute == GetHealthAttribute())
	{
		SetHealth(FMath::Clamp(GetHealth(), 0, GetMaxHealth()));
	}
	if (Data.EvaluatedData.Attribute == GetManaAttribute())
	{
		SetMana(FMath::Clamp(GetMana(), 0, GetMaxMana()));
	}
	if (Data.EvaluatedData.Attribute == GetIncomingDamageAttribute())
	{
		const float LocalIncomingDamage = GetIncomingDamage();
		SetIncomingDamage(0.f);

		// 目标已死亡：忽略后续伤害（例如死亡后仍在 tick 的燃烧），
		// 防止重复触发 Die() 和 SendXPEvent。
		if (Props.TargetAvatarActor && Props.TargetAvatarActor->Implements<UCombatInterface>() && ICombatInterface::Execute_IsDead(Props.TargetAvatarActor))
		{
			return;
		}

		if (LocalIncomingDamage > 0.f)
		{
			const FAuraGameplayTags& Tags = FAuraGameplayTags::Get();

			// 防递归 + 防硬直门控：燃烧 tick 的 GE 带 Effects.Debuff 资产标签，普通伤害 GE_Damage 不带。
			FGameplayTagContainer EffectAssetTags;
			Data.EffectSpec.GetAllAssetTags(EffectAssetTags);
			const bool bIsDebuffEffect = EffectAssetTags.HasTag(Tags.Effects_Debuff);
			const bool bIsDebuffHit = UAuraAbilitySystemLibrary::IsDebuff(Props.EffectContextHandle);

			const float NewHealth = GetHealth() - LocalIncomingDamage;
			SetHealth(FMath::Clamp(NewHealth, 0.f, NewHealth));

			const bool bFatal = NewHealth <= 0.f;
			if (bFatal)
			{
				// 目标死亡：移除其身上的所有 debuff，避免死亡后继续 tick（也会随之停止施加/刷经验）
				FGameplayTagContainer DebuffTags;
				DebuffTags.AddTag(Tags.Effects_Debuff);
				Props.TargetASC->RemoveActiveEffectsWithTags(DebuffTags);

				ICombatInterface* CombatInterface = Cast<ICombatInterface>(Props.TargetAvatarActor);
				if (CombatInterface)
				{
					// 只有直接命中的致死才把尸体击飞；燃烧 tick 击杀不触发死亡冲量
					if (!bIsDebuffEffect)
					{
						CombatInterface->SetDeathImpulse(UAuraAbilitySystemLibrary::GetDeathImpulse(Props.EffectContextHandle));
					}
					CombatInterface->Die();
				}
				SendXPEvent(Props);
			}
			else
			{
				// 命中且掷骰成功 → 施加 debuff GE（仅服务器执行，避免客户端重复施加）
				if (bIsDebuffHit && !bIsDebuffEffect && Props.TargetAvatarActor->HasAuthority())
				{
					const FGameplayTag DebuffDamageType = UAuraAbilitySystemLibrary::GetDebuffDamageType(Props.EffectContextHandle);
					const TSubclassOf<UGameplayEffect>* DebuffEffectClass = Tags.DamageTypesToDebuffEffects.Find(DebuffDamageType);
					if (DebuffEffectClass && *DebuffEffectClass && IsValid(Props.SourceASC))
					{
						const FGameplayEffectSpecHandle DebuffSpecHandle = Props.SourceASC->MakeOutgoingSpec(*DebuffEffectClass, 1.f, Props.EffectContextHandle);
						if (DebuffSpecHandle.Data.IsValid())
						{
							FGameplayEffectSpec* MutableSpec = DebuffSpecHandle.Data.Get();
							MutableSpec->AddDynamicAssetTag(Tags.Effects_Debuff); // 门控标签
							if (const FGameplayTag* DebuffTag = Tags.DamageTypesToDebuffTags.Find(DebuffDamageType))
							{
								MutableSpec->DynamicGrantedTags.AddTag(*DebuffTag); // Effects.Debuff.Burn 授予目标
							}
							// 每跳伤害由 ExecCalc_Debuff 从 spec 的 SetByCaller 读取（执行时标签已注册）
							UAbilitySystemBlueprintLibrary::AssignTagSetByCallerMagnitude(DebuffSpecHandle, Tags.Debuff_Damage, UAuraAbilitySystemLibrary::GetDebuffDamage(Props.EffectContextHandle));
							// 锁定时长 + 直接设周期：GE CDO 无法在构造时烘焙 SetByCaller 标签，
							// 且 UE5.8 的 FScalableFloat 不支持 SetByCaller，所以直接在 spec 上覆盖。
							DebuffSpecHandle.Data->SetDuration(UAuraAbilitySystemLibrary::GetDebuffDuration(Props.EffectContextHandle), true);
							DebuffSpecHandle.Data->Period = UAuraAbilitySystemLibrary::GetDebuffFrequency(Props.EffectContextHandle);
							Props.TargetASC->ApplyGameplayEffectSpecToSelf(*DebuffSpecHandle.Data.Get());
						}
					}
				}

				// 燃烧 tick 不触发硬直；眩晕中的目标也不触发硬直（避免打断眩晕动画）
				if (!bIsDebuffEffect && !Props.TargetASC->HasMatchingGameplayTag(Tags.Effects_Debuff_Stun))
				{
					FGameplayTagContainer TagContainer;
					TagContainer.AddTag(Tags.Effects_HitReact);
					Props.TargetASC->TryActivateAbilitiesByTag(TagContainer);

					// 击退：把目标沿 施法者→目标 方向击退（仅服务器执行，避免重复施加）
					const float KnockbackMagnitude = UAuraAbilitySystemLibrary::GetKnockbackMagnitude(Props.EffectContextHandle);
					if (KnockbackMagnitude > 0.f && Props.TargetCharacter && Props.SourceAvatarActor && Props.TargetAvatarActor->HasAuthority())
					{
						const FVector Direction = (Props.TargetAvatarActor->GetActorLocation() - Props.SourceAvatarActor->GetActorLocation()).GetSafeNormal2D();
						const FVector KnockbackVelocity = Direction * KnockbackMagnitude;
						Props.TargetCharacter->LaunchCharacter(KnockbackVelocity, true, true);

						// 打断敌人追击：短暂暂停路径跟随，避免 AI 每帧重设速度把击退覆盖掉
						if (AAIController* TargetAIController = Cast<AAIController>(Props.TargetCharacter->GetController()))
						{
							if (UPathFollowingComponent* PathFollowing = TargetAIController->GetPathFollowingComponent())
							{
								PathFollowing->PauseMove(FAIRequestID::CurrentRequest, EPathFollowingVelocityMode::Keep);
								FTimerHandle KnockbackTimer;
								Props.TargetCharacter->GetWorldTimerManager().SetTimer(KnockbackTimer, [TargetAIController]()
								{
									if (IsValid(TargetAIController))
									{
										if (UPathFollowingComponent* PathFollowing = TargetAIController->GetPathFollowingComponent())
										{
											PathFollowing->ResumeMove(FAIRequestID::CurrentRequest);
										}
									}
								}, 0.2f, false);
							}
						}
					}
				}
			}

			if (Props.SourceCharacter != Props.TargetCharacter)
			{
				const bool bBlockedHit = UAuraAbilitySystemLibrary::IsBlockedHit(Props.EffectContextHandle);
				const bool bCriticalHit = UAuraAbilitySystemLibrary::IsCriticalHit(Props.EffectContextHandle);
				if (AAuraPlayerController* PC = Cast<AAuraPlayerController>(Props.SourceCharacter->Controller))
				{
					PC->ShowDamageNumber(LocalIncomingDamage, Props.TargetCharacter, bBlockedHit, bCriticalHit);
					return;
				}
				if (AAuraPlayerController* PC = Cast<AAuraPlayerController>(Props.TargetCharacter->Controller))
				{
					PC->ShowDamageNumber(LocalIncomingDamage, Props.TargetCharacter, bBlockedHit, bCriticalHit);
				}
			}
		}
	}
	
	if (Data.EvaluatedData.Attribute == GetIncomingXPAttribute())
	{
		const float LocalIncomingXP = GetIncomingXP();
		SetIncomingXP(0.f);
		
		if (Props.SourceCharacter->Implements<UCombatInterface>() && Props.SourceCharacter->Implements<UPlayerInterface>())
		{
			const int32 CurrentLevel = ICombatInterface::Execute_GetLevel(Props.SourceCharacter);
			const int32 CurrentXP = IPlayerInterface::Execute_GetXP(Props.SourceCharacter);
			
			const int32 NewLevel = IPlayerInterface::Execute_FindLevelForXP(Props.SourceCharacter, CurrentXP + LocalIncomingXP);
			const int32 TimesOfLevelUp = NewLevel - CurrentLevel;
			
			if (TimesOfLevelUp > 0)
			{
				// 每升一级各给一次奖励（一次升多级时不能只给一次）
				for (int32 i = CurrentLevel; i < NewLevel; ++i)
				{
					const int32 AttributePointsReward = IPlayerInterface::Execute_GetAttributePointsReward(Props.SourceCharacter, i);
					const int32 SpellPointsReward = IPlayerInterface::Execute_GetSpellPointsReward(Props.SourceCharacter, i);
					IPlayerInterface::Execute_AddToAttributePoints(Props.SourceCharacter, AttributePointsReward);
					IPlayerInterface::Execute_AddToSpellPoints(Props.SourceCharacter, SpellPointsReward);
				}

				IPlayerInterface::Execute_AddToPlayerLevel(Props.SourceCharacter, TimesOfLevelUp);

				bTopOffHealth = true;
				bTopOffMana = true;

				IPlayerInterface::Execute_LevelUp(Props.SourceCharacter);
			}
				
			IPlayerInterface::Execute_AddToXP(Props.SourceCharacter, LocalIncomingXP);
		}
	}
}

void UAuraAttributeSet::PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue)
{
	Super::PostAttributeChange(Attribute, OldValue, NewValue);
	
	if (Attribute == GetMaxHealthAttribute() && bTopOffHealth)
	{
		SetHealth(GetMaxHealth());
		bTopOffHealth = false;
	}
	if (Attribute == GetMaxManaAttribute() && bTopOffMana)
	{
		SetMana(GetMaxMana());
		bTopOffMana = false;
	}
}

void UAuraAttributeSet::OnRep_Health(const FGameplayAttributeData& OldHealth) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, Health, OldHealth);
}

void UAuraAttributeSet::OnRep_Mana(const FGameplayAttributeData& OldMana) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, Mana, OldMana);
}

void UAuraAttributeSet::OnRep_Strength(const FGameplayAttributeData& OldStrength) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, Strength, OldStrength);
}

void UAuraAttributeSet::OnRep_Intelligence(const FGameplayAttributeData& OldIntelligence) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, Intelligence, OldIntelligence);
}

void UAuraAttributeSet::OnRep_Resilience(const FGameplayAttributeData& OldResilience) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, Resilience, OldResilience);
}

void UAuraAttributeSet::OnRep_Vigor(const FGameplayAttributeData& OldVigor) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, Vigor, OldVigor);
}

void UAuraAttributeSet::OnRep_Armor(const FGameplayAttributeData& OldArmor) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, Armor, OldArmor);
}

void UAuraAttributeSet::OnRep_ArmorPenetration(const FGameplayAttributeData& OldArmorPenetration) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, ArmorPenetration, OldArmorPenetration);
}

void UAuraAttributeSet::OnRep_BlockChance(const FGameplayAttributeData& OldBlockChance) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, BlockChance, OldBlockChance);
}

void UAuraAttributeSet::OnRep_CriticalHitChance(const FGameplayAttributeData& OldCriticalHitChance) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, CriticalHitChance, OldCriticalHitChance);
}

void UAuraAttributeSet::OnRep_CriticalHitDamage(const FGameplayAttributeData& OldCriticalHitDamage) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, CriticalHitDamage, OldCriticalHitDamage);
}

void UAuraAttributeSet::OnRep_CriticalHitResistance(const FGameplayAttributeData& OldCriticalHitResistance) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, CriticalHitResistance, OldCriticalHitResistance);
}

void UAuraAttributeSet::OnRep_FireResistance(const FGameplayAttributeData& OldFireResistance) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, FireResistance, OldFireResistance);
}

void UAuraAttributeSet::OnRep_LightningResistance(const FGameplayAttributeData& OldLightningResistance) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, LightningResistance, OldLightningResistance);
}

void UAuraAttributeSet::OnRep_ArcaneResistance(const FGameplayAttributeData& OldArcaneResistance) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, ArcaneResistance, OldArcaneResistance);
}

void UAuraAttributeSet::OnRep_PhysicalResistance(const FGameplayAttributeData& OldPhysicalResistance) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, PhysicalResistance, OldPhysicalResistance);
}

void UAuraAttributeSet::OnRep_HealthRegeneration(const FGameplayAttributeData& OldHealthRegeneration) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, HealthRegeneration, OldHealthRegeneration);
}

void UAuraAttributeSet::OnRep_ManaRegeneration(const FGameplayAttributeData& OldManaRegeneration) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, ManaRegeneration, OldManaRegeneration);
}

void UAuraAttributeSet::OnRep_MaxHealth(const FGameplayAttributeData& OldMaxHealth) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, MaxHealth, OldMaxHealth);
}

void UAuraAttributeSet::OnRep_MaxMana(const FGameplayAttributeData& OldMaxMana) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, MaxMana, OldMaxMana);
}

void UAuraAttributeSet::SetEffectProperties(const FGameplayEffectModCallbackData& Data, FEffectProperties& Props) const
{
	// Source is causer of the effect, Target is target of the effect (owner of this AS)
	
	Props.EffectContextHandle = Data.EffectSpec.GetContext();
	Props.SourceASC = Props.EffectContextHandle.GetOriginalInstigatorAbilitySystemComponent();

	if (IsValid(Props.SourceASC) && Props.SourceASC->AbilityActorInfo.IsValid() && Props.SourceASC->AbilityActorInfo->AvatarActor.IsValid())
	{
		Props.SourceAvatarActor = Props.SourceASC->AbilityActorInfo->AvatarActor.Get();
		Props.SourceController = Props.SourceASC->AbilityActorInfo->PlayerController.Get();
		if (!Props.SourceController && Props.SourceAvatarActor)
		{
			const APawn* Pawn = Cast<APawn>(Props.SourceAvatarActor);
			if (Pawn)
			{
				Props.SourceController = Pawn->GetController();
			}
		}
		if (Props.SourceController)
		{
			Props.SourceCharacter = Cast<ACharacter>(Props.SourceController->GetPawn());
		}
	}
	
	if (Data.Target.AbilityActorInfo.IsValid() && Data.Target.AbilityActorInfo->AvatarActor.IsValid())
	{
		Props.TargetAvatarActor = Data.Target.AbilityActorInfo->AvatarActor.Get();
		Props.TargetController = Data.Target.AbilityActorInfo->PlayerController.Get();
		Props.TargetCharacter = Cast<ACharacter>(Props.TargetAvatarActor);
		Props.TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Props.TargetAvatarActor);
	}
}

void UAuraAttributeSet::SendXPEvent(const FEffectProperties& Props)
{
	if (Props.TargetCharacter->Implements<UCombatInterface>())
	{
		const int32 TargetLevel = ICombatInterface::Execute_GetLevel(Props.TargetCharacter);
		const ECharacterClass TargetClass = ICombatInterface::Execute_GetCharacterClass(Props.TargetCharacter);
		const int32 XPReward = UAuraAbilitySystemLibrary::GetXPRewardForClassAndLevel(Props.TargetCharacter, TargetClass, TargetLevel);
		
		const FAuraGameplayTags& GameplayTags = FAuraGameplayTags::Get(); 
		FGameplayEventData Payload;
		Payload.EventTag = GameplayTags.Attributes_Meta_InComingXP;
		Payload.EventMagnitude = XPReward;
		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Props.SourceCharacter, GameplayTags.Attributes_Meta_InComingXP, Payload);
	}
	
}
