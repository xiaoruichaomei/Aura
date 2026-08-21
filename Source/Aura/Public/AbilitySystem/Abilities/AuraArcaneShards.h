#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/AuraDamageGameplayAbility.h"
#include "AuraArcaneShards.generated.h"

class UAbilityTask_WaitInputPress;
class UTargetDataMagicCircle;
class UMaterialInterface;

UCLASS()
class AURA_API UAuraArcaneShards : public UAuraDamageGameplayAbility
{
	GENERATED_BODY()

public:
	UAuraArcaneShards();
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	UFUNCTION(BlueprintPure, Category="Arcane Shards")
	float ComputeRadialDamageScale(float Distance) const;
	void ConfirmTargetFromServer(const FVector& Center);

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Arcane Shards|Targeting")
	TObjectPtr<UMaterialInterface> MagicCircleMaterial;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Arcane Shards|Targeting")
	float MaxCastRange = 1500.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Arcane Shards|Sequence")
	int32 MaxShardPoints = 11;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Arcane Shards|Sequence")
	float RingSpacing = 180.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Arcane Shards|Sequence")
	float SpawnInterval = .15f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Arcane Shards|Damage")
	float InnerRadius = 75.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Arcane Shards|Damage")
	float OuterRadius = 250.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Arcane Shards|Damage", meta=(ClampMin="0", ClampMax="1"))
	float MinimumDamagePercent = .25f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Arcane Shards|Damage", meta=(ClampMin="0.01"))
	float DamageFalloffExponent = 1.f;

private:
	void StartShardSequence(const FVector& Center);
	void TriggerNextShard();
	TArray<FVector> BuildShardPoints(const FVector& Center) const;
	void TriggerShardAtPoint(const FVector& Point);
	void ClearTargeting();

	TArray<FVector> PendingShardPoints;
	int32 CurrentPointIndex = 0;
	FTimerHandle ShardTimerHandle;
};
