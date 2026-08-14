


#include "AbilitySystem/Abilities/AuraGameplayAbility.h"

#include "AbilitySystem/AuraAttributeSet.h"
#include "GameplayEffect.h"

FString UAuraGameplayAbility::GetDescription(int32 Level, const FAuraAbilityInfo& AbilityInfo)
{
	return FString::Printf(TEXT(
		"<Title>%s</>  \n\n"
		
		"<Small>Level : </><Level>%d</>  \n"
		"<Small>ManaCost : </><Level>%d</>  \n"
		"<Small>Cooldown : </><Level>%.1f</><small>s</>  \n\n"
		
		"%s"),
		*AbilityInfo.AbilityName.ToString(),
		Level,
		GetManaCost(Level),
		GetCooldown(Level),
		*GetResolvedDescription(Level, AbilityInfo));
}

FString UAuraGameplayAbility::GetNextLevelDescription(int32 Level, const FAuraAbilityInfo& AbilityInfo)
{
	return FString::Printf(TEXT(
		"<Title>%s</>  \n\n"
		
		"<Small>Level : </><Level>%d</>  \n"
		"<Small>ManaCost : </><Level>%d</>  \n"
		"<Small>Cooldown : </><Level>%.1f</><small>s</>  \n\n"
		
		"%s"),
		*AbilityInfo.AbilityName.ToString(),
		Level,
		GetManaCost(Level),
		GetCooldown(Level),
		*GetResolvedDescription(Level, AbilityInfo));
}

FString UAuraGameplayAbility::GetLockedDescription(int32 Level)
{
	return FString::Printf(TEXT("<Default>Spell Locked Util Level : %d.</>"), Level);
}

FString UAuraGameplayAbility::GetUnlockDescription()
{
	return FString::Printf(TEXT("<Default>Spend 1 Spell Point to unlock this ability.</>"));
}

FString UAuraGameplayAbility::GetResolvedDescription(int32 Level, const FAuraAbilityInfo& AbilityInfo)
{
	FString Description = AbilityInfo.AbilityDescription.ToString();
	Description = Description.Replace(TEXT("{Level}"), *FString::FromInt(Level));
	return Description;
}

int32 UAuraGameplayAbility::GetManaCost(int32 Level) const
{
	float ManaCost = 1;
	if (CostGameplayEffectClass)
	{
		if (const UGameplayEffect* CostEffect = GetDefault<UGameplayEffect>(CostGameplayEffectClass))
		{
			for (const FGameplayModifierInfo& Modifier : CostEffect->Modifiers)
			{
				if (Modifier.Attribute == UAuraAttributeSet::GetManaAttribute())
				{
					Modifier.ModifierMagnitude.GetStaticMagnitudeIfPossible(Level, ManaCost);
					break;
				}
			}
		}
	}
	return -1 * ManaCost;
}

float UAuraGameplayAbility::GetCooldown(int32 Level) const
{
	float Cooldown = 0.f;
	if (CooldownGameplayEffectClass)
	{
		if (const UGameplayEffect* CooldownEffect = GetDefault<UGameplayEffect>(CooldownGameplayEffectClass))
		{
			CooldownEffect->DurationMagnitude.GetStaticMagnitudeIfPossible(Level, Cooldown);
		}
	}
	return Cooldown;
}
