// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "AuraAttributeSet.generated.h"

class ACharacter;

USTRUCT()
struct FEffectProperties
{
	GENERATED_BODY()
	
	FEffectProperties(){}
	
	FGameplayEffectContextHandle EffectContextHandle;
	
	UPROPERTY()
	UAbilitySystemComponent* SourceASC = nullptr;
	
	UPROPERTY()
	AActor* SourceAvatarActor = nullptr;
	
	UPROPERTY()
	AController* SourceController = nullptr;
	
	UPROPERTY()
	ACharacter* SourceCharacter = nullptr;
	
	UPROPERTY()
	UAbilitySystemComponent* TargetASC = nullptr;
	
	UPROPERTY()
	AActor* TargetAvatarActor = nullptr;
	
	UPROPERTY()
	AController* TargetController = nullptr;
	
	UPROPERTY()
	ACharacter* TargetCharacter = nullptr;
};

// typedef TBaseStaticDelegateInstance<FGameplayAttribute(), FDefaultDelegateUserPolicy>::FFuncPtr FAttributeFuncPtr;
template<class T>
using TStaticFuncPtr = TBaseStaticDelegateInstance<T, FDefaultDelegateUserPolicy>::FFuncPtr;

/**
 * 
 */
UCLASS()
class AURA_API UAuraAttributeSet : public UAttributeSet
{
	GENERATED_BODY()
public:
	UAuraAttributeSet();
	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	virtual void PostGameplayEffectExecute(const struct FGameplayEffectModCallbackData& Data) override;
	virtual void PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue) override;
	
	UFUNCTION()
	void OnRep_Health(const FGameplayAttributeData& OldHealth) const;
	
	UFUNCTION()
	void OnRep_MaxHealth(const FGameplayAttributeData& OldMaxHealth) const;
	
	UFUNCTION()
	void OnRep_Mana(const FGameplayAttributeData& OldMana) const;
	
	UFUNCTION()
	void OnRep_MaxMana(const FGameplayAttributeData& OldMaxMana) const;
	
	UFUNCTION()
	void OnRep_Strength(const FGameplayAttributeData& OldStrength) const;
	
	UFUNCTION()
	void OnRep_Intelligence(const FGameplayAttributeData& OldIntelligence) const;
	
	UFUNCTION()
	void OnRep_Resilience(const FGameplayAttributeData& OldResilience) const;
	
	UFUNCTION()
	void OnRep_Vigor(const FGameplayAttributeData& OldVigor) const;
	
	UFUNCTION()
	void OnRep_Armor(const FGameplayAttributeData& OldArmor) const;
	
	UFUNCTION()
	void OnRep_ArmorPenetration(const FGameplayAttributeData& OldArmorPenetration) const;
	
	UFUNCTION()
	void OnRep_BlockChance(const FGameplayAttributeData& OldBlockChance) const;
	
	UFUNCTION()
	void OnRep_CriticalHitChance(const FGameplayAttributeData& OldCriticalHitChance) const;
	
	UFUNCTION()
	void OnRep_CriticalHitDamage(const FGameplayAttributeData& OldCriticalHitDamage) const;
	
	UFUNCTION()
	void OnRep_CriticalHitResistance(const FGameplayAttributeData& OldCriticalHitResistance) const;
	
	UFUNCTION()
	void OnRep_FireResistance(const FGameplayAttributeData& OldFireResistance) const;
	
	UFUNCTION()
	void OnRep_LightningResistance(const FGameplayAttributeData& OldLightningResistance) const;
	
	UFUNCTION()
	void OnRep_ArcaneResistance(const FGameplayAttributeData& OldArcaneResistance) const;
	
	UFUNCTION()
	void OnRep_PhysicalResistance(const FGameplayAttributeData& OldPhysicalResistance) const;
	
	UFUNCTION()
	void OnRep_HealthRegeneration(const FGameplayAttributeData& OldHealthRegeneration) const;
	
	UFUNCTION()
	void OnRep_ManaRegeneration(const FGameplayAttributeData& OldManaRegeneration) const;
	
	// <Vital Attribute>
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Health, Category="Vital Attributes")
	FGameplayAttributeData Health;
	ATTRIBUTE_ACCESSORS_BASIC(UAuraAttributeSet, Health);
	
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Mana, Category="Vital Attributes")
	FGameplayAttributeData Mana;
	ATTRIBUTE_ACCESSORS_BASIC(UAuraAttributeSet, Mana);
	// </Vital Attribute>
	
	// <Primary Attribute>
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Strength, Category="Primary Attributes")
	FGameplayAttributeData Strength;
	ATTRIBUTE_ACCESSORS_BASIC(UAuraAttributeSet, Strength);
	
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Intelligence, Category="Primary Attributes")
	FGameplayAttributeData Intelligence;
	ATTRIBUTE_ACCESSORS_BASIC(UAuraAttributeSet, Intelligence);
	
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Resilience, Category="Primary Attributes")
	FGameplayAttributeData Resilience;
	ATTRIBUTE_ACCESSORS_BASIC(UAuraAttributeSet, Resilience);
	
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Vigor, Category="Primary Attributes")
	FGameplayAttributeData Vigor;
	ATTRIBUTE_ACCESSORS_BASIC(UAuraAttributeSet, Vigor);
	// </Primary Attribute>
	
	// <Secondary Attribute>
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Armor, Category="Secondary Attributes")
	FGameplayAttributeData Armor;
	ATTRIBUTE_ACCESSORS_BASIC(UAuraAttributeSet, Armor);
	
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_ArmorPenetration, Category="Secondary Attributes")
	FGameplayAttributeData ArmorPenetration;
	ATTRIBUTE_ACCESSORS_BASIC(UAuraAttributeSet, ArmorPenetration);
	
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_BlockChance, Category="Secondary Attributes")
	FGameplayAttributeData BlockChance;
	ATTRIBUTE_ACCESSORS_BASIC(UAuraAttributeSet, BlockChance)
	
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_CriticalHitChance, Category="Secondary Attributes")
	FGameplayAttributeData CriticalHitChance;
	ATTRIBUTE_ACCESSORS_BASIC(UAuraAttributeSet, CriticalHitChance)
	
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_CriticalHitDamage, Category="Secondary Attributes")
	FGameplayAttributeData CriticalHitDamage;
	ATTRIBUTE_ACCESSORS_BASIC(UAuraAttributeSet, CriticalHitDamage)
	
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_CriticalHitResistance, Category="Secondary Attributes")
	FGameplayAttributeData CriticalHitResistance;
	ATTRIBUTE_ACCESSORS_BASIC(UAuraAttributeSet, CriticalHitResistance)
	
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_HealthRegeneration, Category="Secondary Attributes")
	FGameplayAttributeData HealthRegeneration;
	ATTRIBUTE_ACCESSORS_BASIC(UAuraAttributeSet, HealthRegeneration)
	
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_ManaRegeneration, Category="Secondary Attributes")
	FGameplayAttributeData ManaRegeneration;
	ATTRIBUTE_ACCESSORS_BASIC(UAuraAttributeSet, ManaRegeneration)
	
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_MaxHealth, Category="Secondary Attributes")
	FGameplayAttributeData MaxHealth;
	ATTRIBUTE_ACCESSORS_BASIC(UAuraAttributeSet, MaxHealth);
	
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_MaxMana, Category="Secondary Attributes")
	FGameplayAttributeData MaxMana;
	ATTRIBUTE_ACCESSORS_BASIC(UAuraAttributeSet, MaxMana);
	// </Secondary Attribute>
	
	// <Resistance Attribute>
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_FireResistance, Category="Resistance Attributes")
	FGameplayAttributeData FireResistance;
	ATTRIBUTE_ACCESSORS_BASIC(UAuraAttributeSet, FireResistance);
	
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_LightningResistance, Category="Resistance Attributes")
	FGameplayAttributeData LightningResistance;
	ATTRIBUTE_ACCESSORS_BASIC(UAuraAttributeSet, LightningResistance);
	
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_ArcaneResistance, Category="Resistance Attributes")
	FGameplayAttributeData ArcaneResistance;
	ATTRIBUTE_ACCESSORS_BASIC(UAuraAttributeSet, ArcaneResistance);
	
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_PhysicalResistance, Category="Resistance Attributes")
	FGameplayAttributeData PhysicalResistance;
	ATTRIBUTE_ACCESSORS_BASIC(UAuraAttributeSet, PhysicalResistance);
	// </Resistance Attribute>
	
	TMap<FGameplayTag, TStaticFuncPtr<FGameplayAttribute()>> TagsToAttributes;
	
	// <Meta Attribute>
	UPROPERTY(BlueprintReadOnly, Category="Meta Attributes")
	FGameplayAttributeData IncomingDamage;
	ATTRIBUTE_ACCESSORS_BASIC(UAuraAttributeSet, IncomingDamage);
	
	UPROPERTY(BlueprintReadOnly, Category="Meta Attributes")
	FGameplayAttributeData IncomingXP;
	ATTRIBUTE_ACCESSORS_BASIC(UAuraAttributeSet, IncomingXP);
	// </Meta Attribute>
	
private:
	void SetEffectProperties(const FGameplayEffectModCallbackData& Data, FEffectProperties& Props) const;
	void SendXPEvent(const FEffectProperties& Props);
	
	bool bTopOffHealth = false;
	bool bTopOffMana = false;
};