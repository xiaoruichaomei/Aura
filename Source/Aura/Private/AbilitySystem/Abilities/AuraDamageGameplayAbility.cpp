


#include "AbilitySystem/Abilities/AuraDamageGameplayAbility.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Interface/CombatInterface.h"

void UAuraDamageGameplayAbility::CauseDamage(AActor* TargetActor)
{
	const FGameplayEffectSpecHandle DamageSpecHandle = MakeOutgoingGameplayEffectSpec(DamageEffectClass, 1.f);
	for (const TTuple<FGameplayTag, FScalableFloat>& Pair : DamageType)
	{
		const float ScaledDamage = Pair.Value.GetValueAtLevel(GetAbilityLevel());
		UAbilitySystemBlueprintLibrary::AssignTagSetByCallerMagnitude(DamageSpecHandle, Pair.Key, ScaledDamage);
	}
	GetAbilitySystemComponentFromActorInfo()->ApplyGameplayEffectSpecToTarget(*DamageSpecHandle.Data.Get(), UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(TargetActor));
}

FTaggedMontage UAuraDamageGameplayAbility::GetRandomTaggedMontageFromArray(const TArray<FTaggedMontage>& TaggedMontages)
{
	if (TaggedMontages.Num() > 0)
	{
		const int32 Selection = FMath::RandRange(0, TaggedMontages.Num() - 1);
		return TaggedMontages[Selection];
	}
	
	return FTaggedMontage();
}

FString UAuraDamageGameplayAbility::GetResolvedDescription(int32 Level, const FAuraAbilityInfo& AbilityInfo)
{
	FString Description = Super::GetResolvedDescription(Level, AbilityInfo);
	int32 TotalDamage = 0;
	for (const TTuple<FGameplayTag, FScalableFloat>& Pair : DamageType)
	{
		TotalDamage += FMath::RoundToInt(Pair.Value.GetValueAtLevel(Level));
	}
	return Description.Replace(TEXT("{Damage}"), *FString::FromInt(TotalDamage));
}
