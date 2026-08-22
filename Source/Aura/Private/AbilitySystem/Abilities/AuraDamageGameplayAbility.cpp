


#include "AbilitySystem/Abilities/AuraDamageGameplayAbility.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AuraAbilityTypes.h"
#include "AuraGameplayTags.h"
#include "Interface/CombatInterface.h"

FGameplayEffectSpecHandle UAuraDamageGameplayAbility::MakeDamageEffectSpec(const FAuraDamageEffectParams& Params) const
{
	if (!Params.DamageEffectClass || !IsValid(GetAbilitySystemComponentFromActorInfo()))
	{
		return FGameplayEffectSpecHandle();
	}

	FGameplayEffectContextHandle ContextHandle = GetAbilitySystemComponentFromActorInfo()->MakeEffectContext();
	ContextHandle.SetAbility(this);
	ContextHandle.AddSourceObject(Params.SourceObject);
	if (FAuraGameplayEffectContext* AuraEffectContext = static_cast<FAuraGameplayEffectContext*>(ContextHandle.Get()))
	{
		AuraEffectContext->SetDeathImpulse(Params.DeathImpulse);
		AuraEffectContext->SetKnockbackMagnitude(Params.KnockbackMagnitude);
		AuraEffectContext->SetKnockbackForce(Params.KnockbackForce);
	}

	const FGameplayEffectSpecHandle DamageSpecHandle = GetAbilitySystemComponentFromActorInfo()->MakeOutgoingSpec(Params.DamageEffectClass, GetAbilityLevel(), ContextHandle);

	const float ScaledDamage = Params.BaseDamage.GetValueAtLevel(GetAbilityLevel());
	UAbilitySystemBlueprintLibrary::AssignTagSetByCallerMagnitude(DamageSpecHandle, Params.DamageType, ScaledDamage);

	const FAuraGameplayTags& Tags = FAuraGameplayTags::Get();
	UAbilitySystemBlueprintLibrary::AssignTagSetByCallerMagnitude(DamageSpecHandle, Tags.Debuff_Chance, Params.DebuffChance.GetValueAtLevel(GetAbilityLevel()));
	UAbilitySystemBlueprintLibrary::AssignTagSetByCallerMagnitude(DamageSpecHandle, Tags.Debuff_Damage, Params.DebuffDamage.GetValueAtLevel(GetAbilityLevel()));
	UAbilitySystemBlueprintLibrary::AssignTagSetByCallerMagnitude(DamageSpecHandle, Tags.Debuff_Duration, Params.DebuffDuration.GetValueAtLevel(GetAbilityLevel()));
	UAbilitySystemBlueprintLibrary::AssignTagSetByCallerMagnitude(DamageSpecHandle, Tags.Debuff_Frequency, Params.DebuffFrequency.GetValueAtLevel(GetAbilityLevel()));

	return DamageSpecHandle;
}

void UAuraDamageGameplayAbility::CauseDamage(AActor* TargetActor)
{
	if (!TargetActor || !IsValid(GetAbilitySystemComponentFromActorInfo()))
	{
		return;
	}

	UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(TargetActor);
	if (!TargetASC)
	{
		return;
	}

	FAuraDamageEffectParams Params = DamageEffectParams;
	const FVector Direction = (TargetActor->GetActorLocation() - GetAvatarActorFromActorInfo()->GetActorLocation()).GetSafeNormal2D();
	// 加一点 Z 上抛分量：纯水平冲量会被倒地尸体与地面的摩擦吸收，几乎看不到
	Params.DeathImpulse = Direction * Params.DeathImpulseMagnitude + FVector(0.f, 0.f, Params.DeathImpulseMagnitude * 0.3f);

	const FGameplayEffectSpecHandle DamageSpecHandle = MakeDamageEffectSpec(Params);
	if (!DamageSpecHandle.Data.IsValid() || !DamageSpecHandle.Data->Def)
	{
		return;
	}

	GetAbilitySystemComponentFromActorInfo()->ApplyGameplayEffectSpecToTarget(*DamageSpecHandle.Data.Get(), TargetASC);
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
	const int32 TotalDamage = FMath::RoundToInt(DamageEffectParams.BaseDamage.GetValueAtLevel(Level));
	const int32 DebuffChance = FMath::RoundToInt(DamageEffectParams.DebuffChance.GetValueAtLevel(Level));
	const int32 DebuffDamage = FMath::RoundToInt(DamageEffectParams.DebuffDamage.GetValueAtLevel(Level));
	const FString DebuffDuration = FString::SanitizeFloat(DamageEffectParams.DebuffDuration.GetValueAtLevel(Level), 1);
	const FString DebuffFrequency = FString::SanitizeFloat(DamageEffectParams.DebuffFrequency.GetValueAtLevel(Level), 1);
	Description = Description.Replace(TEXT("{Damage}"), *FString::FromInt(TotalDamage));
	Description = Description.Replace(TEXT("{DebuffChance}"), *FString::FromInt(DebuffChance));
	Description = Description.Replace(TEXT("{DebuffDamage}"), *FString::FromInt(DebuffDamage));
	Description = Description.Replace(TEXT("{DebuffDuration}"), *DebuffDuration);
	Description = Description.Replace(TEXT("{DebuffFrequency}"), *DebuffFrequency);
	return Description;
}
