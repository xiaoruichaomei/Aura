

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "AuraPlayerController.generated.h"

class UDamageTextComponent;
class UAuraInputConfig;
class UInputMappingContext;
class UInputAction;
class UNiagaraSystem;
class IEnemyInterface;
class UAuraAbilitySystemComponent;
class USplineComponent;
struct FHitResult;
struct FGameplayTag;
struct FInputActionValue;

/**
 * 
 */
UCLASS()
class AURA_API AAuraPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AAuraPlayerController();
	virtual void Tick(float DeltaSeconds) override;

	/** Returns the latest cursor world position on the local client or server. */
	bool GetBeamCursorLocation(FVector& OutLocation) const;

	/** Caches the local cursor position and forwards it to the server for beam targeting. */
	void SubmitBeamCursorLocation(const FVector& InLocation);
	
	UFUNCTION(Client, Reliable)
	void ShowDamageNumber(float DamageAmount, ACharacter* TargetCharacter, bool bBlockedHit, bool bCriticalHit);

	/** 在所有客户端生成点击地面特效（点击移动/施法时） */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastSpawnClickEffect(const FVector& CursorLocation);

	UFUNCTION(Server, Unreliable)
	void ServerSetBeamCursorLocation(FVector_NetQuantize InLocation);

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

private:
	void Move(const FInputActionValue& InputActionValue);
	void CursorTrace();
	
	void AbilityInputTagPressed(FGameplayTag InputTag);
	void AbilityInputTagReleased(FGameplayTag InputTag);
	void AbilityInputTagHeld(FGameplayTag InputTag);
	void AutoRun();

	/** 玩家身上是否有 Player.Block 标签（如施法期间阻塞移动） */
	bool IsInputBlocked() const;
	
	void SnapCameraToPlayer();
	
	virtual void GetPlayerViewPoint(FVector& Location, FRotator& Rotation) const override;
	
	UAuraAbilitySystemComponent* GetASC();
	
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputMappingContext> AuraContext;
	
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> MoveAction;
	
	IEnemyInterface* LastActor;
	IEnemyInterface* ThisActor;
	FHitResult CursorHit;
	FVector BeamCursorLocation = FVector::ZeroVector;
	bool bHasBeamCursorLocation = false;
	
	UPROPERTY(EditDefaultsOnly, Category="Input")
	TObjectPtr<UAuraInputConfig> InputConfig;
	
	UPROPERTY()
	TObjectPtr<UAuraAbilitySystemComponent> AuraAbilitySystemComponent;
	
	FVector CachedDestination = FVector::ZeroVector;
	float FollowTime = 0.f;
	float ShortPressThreshold = 0.5f;
	bool bAutoRunning = false;
	bool bTargeting = false;
	
	UPROPERTY(EditDefaultsOnly)
	float AutoRunAcceptanceRadius = 50.f;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USplineComponent> Spline;
	
	// <相机控制>
	mutable FVector FixedCameraLocation;
	mutable FRotator FixedCameraRotation;

	void UpdateFixedCameraToPlayer();

	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> CameraSnapAction;
	// </相机控制>
	
	UPROPERTY(EditDefaultsOnly)
	TSubclassOf<UDamageTextComponent> DamageTextComponentClass;

	UPROPERTY(EditDefaultsOnly, Category="Click")
	TObjectPtr<UNiagaraSystem> ClickNiagaraSystem;
};
