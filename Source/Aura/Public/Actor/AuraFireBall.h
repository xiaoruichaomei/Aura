#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectTypes.h"
#include "GameFramework/Actor.h"
#include "AuraFireBall.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;
class USphereComponent;
class AAuraFireBall;
class APawn;

UENUM(BlueprintType)
enum class EFireBallState : uint8
{
	Outgoing,
	Returning,
	Arrived,
	Destroyed
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FFireBallStateChangedSignature, EFireBallState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FFireBallFinishedSignature, AAuraFireBall*, FireBall);

/** FireBlast's replicated projectile shell. Movement and damage are added in later steps. */
UCLASS()
class AURA_API AAuraFireBall : public AActor
{
	GENERATED_BODY()

public:
	AAuraFireBall();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void Destroyed() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	void SetPoolManaged(bool bManaged) { bPoolManaged = bManaged; }
	void ActivateFromPool(const FTransform& Transform, AActor* NewOwner, APawn* NewInstigator);
	void DeactivateToPool();
	void ReturnToPool();

	UFUNCTION(BlueprintCallable)
	void SetSourceActor(AActor* NewSourceActor);

	UFUNCTION(BlueprintCallable)
	void ConfigureFlight(const FVector& NewDirection, float NewMaxTravelDistance, float NewOutgoingDuration, float NewReturnDuration);

	void SetDamageEffectSpecHandle(const FGameplayEffectSpecHandle& InDamageEffectSpecHandle);

	UFUNCTION(BlueprintPure)
	AActor* GetSourceActor() const { return SourceActor.Get(); }

	UFUNCTION(BlueprintCallable)
	void SetFireBallState(EFireBallState NewState);

	UFUNCTION(BlueprintPure)
	EFireBallState GetFireBallState() const { return State; }

	UPROPERTY(BlueprintAssignable)
	FFireBallStateChangedSignature OnStateChanged;

	/** Broadcast once when this fireball arrives or is destroyed before arriving. */
	UPROPERTY(BlueprintAssignable)
	FFireBallFinishedSignature OnFireBallFinished;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<USphereComponent> Sphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<UNiagaraComponent> FireEffect;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="FireBall")
	TObjectPtr<UNiagaraSystem> FireEffectAsset;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="FireBall")
	float LifeSpan = 15.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="FireBall|Flight", meta=(ClampMin="1"))
	float ArrivalRadius = 50.f;

protected:
	UFUNCTION()
	void OnRep_State(EFireBallState PreviousState);

	UPROPERTY(Replicated)
	TObjectPtr<AActor> SourceActor;

	UPROPERTY(ReplicatedUsing=OnRep_State)
	EFireBallState State = EFireBallState::Outgoing;

	UFUNCTION()
	void OnRep_PoolActive();

	UPROPERTY(ReplicatedUsing=OnRep_PoolActive)
	bool bPoolActive = false;

	FVector OutgoingStartLocation;
	FVector OutgoingTargetLocation;
	float OutgoingElapsed = 0.f;
	float ReturningElapsed = 0.f;
	float OutgoingDuration = 0.8f;
	float ReturnDuration = 0.6f;

	FGameplayEffectSpecHandle DamageEffectSpecHandle;
	TSet<TWeakObjectPtr<AActor>> OutgoingHitActors;
	TSet<TWeakObjectPtr<AActor>> ReturningHitActors;
	TSet<TWeakObjectPtr<AActor>> LocalVisualHitActors;
	bool bFinishReported = false;
	bool bPoolManaged = true;
	FTimerHandle PoolLifeTimer;

	void HandleLifeExpired();

	void ReportFinished();
	void InvokeLocalHitCue(AActor* OtherActor, const FHitResult& SweepResult);

	UFUNCTION()
	void OnSphereOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
};
