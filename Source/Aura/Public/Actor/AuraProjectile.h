

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectTypes.h"
#include "GameFramework/Actor.h"
#include "AuraProjectile.generated.h"

class UNiagaraSystem;
class UNiagaraComponent;
class USphereComponent;
class UProjectileMovementComponent;
class APawn;

UCLASS()
class AURA_API AAuraProjectile : public AActor
{
	GENERATED_BODY()
	
public:	
	AAuraProjectile();
	virtual void Destroyed() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void SetPoolManaged(bool bManaged) { bPoolManaged = bManaged; }
	void ActivateFromPool(const FTransform& Transform, AActor* NewOwner, APawn* NewInstigator);
	void DeactivateToPool();
	
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;
	
	UPROPERTY(BlueprintReadWrite, meta=(ExposeOnSpawn=true))
	FGameplayEffectSpecHandle DamageEffectSpecHandle;

protected:
	virtual void BeginPlay() override;
	
	UFUNCTION()
	void OnSphereOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
	
private:
	UFUNCTION()
	void OnRep_PoolActive();

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayImpactEffects(FVector_NetQuantize ImpactLocation);

	void HandleLifeExpired();
	void ReturnToPool();
	void PlayImpactEffects(const FVector& ImpactLocation);
	void StartFlightAudio();
	void SetTrailEffectsActive(bool bActive);

	bool bHit = false;
	bool bPoolManaged = true;

	UPROPERTY(ReplicatedUsing=OnRep_PoolActive)
	bool bPoolActive = false;

	FTimerHandle PoolLifeTimer;
	
	UPROPERTY(EditAnywhere)
	float LifeSpan = 15.f;
	
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USphereComponent> Sphere;
	
	UPROPERTY(EditAnywhere)
	TObjectPtr<UNiagaraSystem> ImpactEffect;

	UPROPERTY(EditAnywhere)
	TObjectPtr<USoundBase> ImpactSound;
	
	UPROPERTY(EditAnywhere)
	TObjectPtr<USoundBase> FlySound;
	
	UPROPERTY()
	TObjectPtr<UAudioComponent> FlySoundComponent;
};
