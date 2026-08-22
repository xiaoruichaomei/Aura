#include "AbilitySystem/Abilities/AuraGameplayAbility.h"

#include "AbilitySystem/AuraAttributeSet.h"
#include "AuraGameplayTags.h"
#include "GameplayEffect.h"

FString UAuraGameplayAbility::GetDescription(int32 Level, const FAuraAbilityInfo& AbilityInfo)
{
	if (AbilityInfo.AbilityType.MatchesTagExact(FAuraGameplayTags::Get().Abilities_Type_Passive))
	{
		return FString::Printf(TEXT(
			"<Title>%s</>  \n\n"
			"<Small>Level:</><Level>%d</>  \n\n"
			"%s"),
			*AbilityInfo.AbilityName.ToString(), Level, *GetResolvedDescription(Level, AbilityInfo));
	}
	return FString::Printf(TEXT(
		"<Title>%s</>  \n\n"
		"<Small>Level:</><Level>%d</>  \n"
		"<Small>Mana Cost:</><Level>%d</>  \n"
		"<Small>Cooldown:</><Level>%.1f</><Small> s</>  \n\n"
		"%s"),
		*AbilityInfo.AbilityName.ToString(),
		Level,
		GetManaCost(Level),
		GetCooldown(Level),
		*GetResolvedDescription(Level, AbilityInfo));
}

FString UAuraGameplayAbility::GetNextLevelDescription(int32 Level, const FAuraAbilityInfo& AbilityInfo)
{
	if (AbilityInfo.AbilityType.MatchesTagExact(FAuraGameplayTags::Get().Abilities_Type_Passive))
	{
		return FString::Printf(TEXT(
			"<Title>%s - Next Level</>  \n\n"
			"<Small>Level:</><Level>%d</>  \n\n"
			"%s"),
			*AbilityInfo.AbilityName.ToString(), Level, *GetResolvedDescription(Level, AbilityInfo));
	}
	return FString::Printf(TEXT(
		"<Title>%s - Next Level</>  \n\n"
		"<Small>Level:</><Level>%d</>  \n"
		"<Small>Mana Cost:</><Level>%d</>  \n"
		"<Small>Cooldown:</><Level>%.1f</><Small> s</>  \n\n"
		"%s"),
		*AbilityInfo.AbilityName.ToString(),
		Level,
		GetManaCost(Level),
		GetCooldown(Level),
		*GetResolvedDescription(Level, AbilityInfo));
}

FString UAuraGameplayAbility::GetLockedDescription(int32 Level)
{
	return FString::Printf(TEXT("<Default>Unlocks at character level </><Level>%d</>"), Level);
}

FString UAuraGameplayAbility::GetUnlockDescription()
{
	return FString(TEXT("<Default>Spend 1 skill point to unlock this ability.</>"));
}

FString UAuraGameplayAbility::GetResolvedDescription(int32 Level, const FAuraAbilityInfo& AbilityInfo)
{
	FString Description = AbilityInfo.AbilityDescription.ToString();
	Description = Description.Replace(TEXT("{Level}"), *FString::FromInt(Level));
	return Description;
}

int32 UAuraGameplayAbility::GetManaCost(int32 Level) const
{
	float ManaCost = 0.f;
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
