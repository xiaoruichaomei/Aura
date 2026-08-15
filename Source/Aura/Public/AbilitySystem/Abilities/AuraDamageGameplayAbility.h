

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/AuraGameplayAbility.h"
#include "AbilitySystem/Data/AuraDamageTypes.h"
#include "AuraDamageGameplayAbility.generated.h"

struct FTaggedMontage;
/**
 *
 */
UCLASS()
class AURA_API UAuraDamageGameplayAbility : public UAuraGameplayAbility
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable)
	void CauseDamage(AActor* TargetActor);

	UFUNCTION(BlueprintPure)
	FGameplayEffectSpecHandle MakeDamageEffectSpec(const FAuraDamageEffectParams& Params) const;

protected:
	UFUNCTION(BlueprintPure)
	FTaggedMontage GetRandomTaggedMontageFromArray(const TArray<FTaggedMontage>& TaggedMontages);

	virtual FString GetResolvedDescription(int32 Level, const FAuraAbilityInfo& AbilityInfo) override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat")
	FAuraDamageEffectParams DamageEffectParams;
};
