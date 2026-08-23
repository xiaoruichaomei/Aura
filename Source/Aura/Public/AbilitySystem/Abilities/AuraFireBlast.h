#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/AuraDamageGameplayAbility.h"
#include "AuraFireBlast.generated.h"

/**
 * FireBlast ability skeleton. Fireball spawning, damage, cost, and cooldown
 * are deliberately added in later implementation steps.
 */
UCLASS()
class AURA_API UAuraFireBlast : public UAuraDamageGameplayAbility
{
	GENERATED_BODY()

public:
	UAuraFireBlast();
	virtual void PostLoad() override;

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	UFUNCTION(BlueprintCallable, Category="FireBlast")
	void SpawnFireBalls();

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

protected:
	virtual FString GetResolvedDescription(int32 Level, const FAuraAbilityInfo& AbilityInfo) override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="FireBlast")
	TSoftClassPtr<class AAuraFireBall> FireBallClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="FireBlast", meta=(ClampMin="1", ClampMax="24"))
	int32 NumFireBalls = 12;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="FireBlast|Pool", meta=(ClampMin="0"))
	int32 FireBallPoolPrewarmCount = 12;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="FireBlast", meta=(ClampMin="0"))
	float SpawnRadius = 80.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="FireBlast|Flight", meta=(ClampMin="0"))
	float MaxTravelDistance = 650.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="FireBlast|Flight", meta=(ClampMin="0.01"))
	float OutgoingDuration = 0.8f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="FireBlast|Flight", meta=(ClampMin="0.01"))
	float ReturnDuration = 0.6f;

	/** Applied once after every fireball returns. It scales the regular FireBlast damage parameters. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="FireBlast|Explosion", meta=(ClampMin="0"))
	float ExplosionDamageMultiplier = 1.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="FireBlast|Explosion", meta=(ClampMin="0"))
	float ExplosionRadius = 350.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="FireBlast|Explosion", meta=(ClampMin="0.1"))
	float ReturnTimeout = 5.f;

	UFUNCTION()
	void OnFireBallFinished(class AAuraFireBall* FireBall);

	void ExplodeAtOwner();
	void HandleReturnTimeout();

	int32 SpawnedFireBallCount = 0;
	int32 ReturnedFireBallCount = 0;
	bool bExplosionTriggered = false;
	TArray<TWeakObjectPtr<class AAuraFireBall>> ActiveFireBalls;
	FTimerHandle ReturnTimeoutHandle;
};
