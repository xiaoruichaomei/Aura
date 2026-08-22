

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "AbilitySystem/Data/AbilityInfo.h"
#include "AuraGameplayAbility.generated.h"

/**
 * 
 */
UCLASS()
class AURA_API UAuraGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()
public:
	UPROPERTY(EditDefaultsOnly, Category="Input")
	FGameplayTag StartupInputTag;

	virtual FString GetDescription(int32 Level, const FAuraAbilityInfo& AbilityInfo);
	virtual FString GetNextLevelDescription(int32 Level, const FAuraAbilityInfo& AbilityInfo);
	static FString GetLockedDescription(int32 Level);
	static FString GetUnlockDescription();

protected:
	virtual FString GetResolvedDescription(int32 Level, const FAuraAbilityInfo& AbilityInfo);
	
	virtual int32 GetManaCost(int32 Level = 1) const;
	virtual float GetCooldown(int32 Level = 1) const;
};
