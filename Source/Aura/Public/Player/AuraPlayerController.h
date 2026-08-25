

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
class UAuraCrowdFollowingComponent;
class UAuraEnvQueryGenerator_AutoMove;
class UAuraEnvQueryTest_AutoMove;
class UEnvQuery;
class AMagicCircle;
class UMaterialInterface;
struct FEnvQueryInstance;
struct FEnvQueryResult;
struct FHitResult;
struct FGameplayTag;
struct FInputActionValue;
struct FPathFollowingResult;

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
	void ShowMagicCircle(UMaterialInterface* DecalMaterial);
	void HideMagicCircle();
	bool GetMagicCircleLocation(FVector& OutLocation) const;
	
	UFUNCTION(Client, Reliable)
	void ShowDamageNumber(float DamageAmount, ACharacter* TargetCharacter, bool bBlockedHit, bool bCriticalHit);

	/** 在所有客户端生成点击地面特效（点击移动/施法时） */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastSpawnClickEffect(const FVector& CursorLocation);


	UFUNCTION(Server, Unreliable)
	void ServerSetBeamCursorLocation(FVector_NetQuantize InLocation);

	UFUNCTION(Server, Unreliable)
	void ServerSetMagicCircleLocation(FVector_NetQuantize InLocation);

	UFUNCTION(Server, Reliable)
	void ServerStartAutoMove(FVector_NetQuantize Destination);

	UFUNCTION(Server, Reliable)
	void ServerStopAutoMove(bool bStopImmediately);

	UFUNCTION(Client, Reliable)
	void ClientSetAutoMoveActive(bool bActive);

	UFUNCTION(Client, Unreliable)
	void ClientSetAutoMoveSteering(FVector_NetQuantize10 SteeringVelocity);

	/** Requests the listen server to save and move every connected player to MainMenu. */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category="Networking")
	void ServerTravelToLoadMenu();

	/** Stops local movement/targeting when the possessed character dies. */
	void HandleControlledPawnDeath();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupInputComponent() override;
	virtual void OnRep_Pawn() override;

private:
	enum class EAutoMoveState : uint8
	{
		Idle,
		ResolvingGoal,
		Moving,
		ResolvingRecovery,
		Recovering
	};

	void Move(const FInputActionValue& InputActionValue);
	void CursorTrace();
	void UpdateMagicCircleLocation();
	
	void AbilityInputTagPressed(FGameplayTag InputTag);
	void AbilityInputTagReleased(FGameplayTag InputTag);
	void AbilityInputTagHeld(FGameplayTag InputTag);
	void AutoRun(float DeltaSeconds);
	void ApplyClientAutoMoveSteering();
	void UpdateAutoMoveFacing(float DeltaSeconds);
	void HandleCrowdSteeringVelocity(const FVector& SteeringVelocity);
	void RequestAutoMove(const FVector& Destination);
	void StartAuthoritativeAutoMove(const FVector& Destination);
	void RequestStopAutoRun(bool bStopImmediately);
	void StopAutoRun(bool bStopMovement);
	bool RequestAutoRunPath(const FVector& Destination, EAutoMoveState MoveState);
	void InitializeAutoMoveQueries();
	void StartAutoMoveGoalQuery();
	void StartAutoMoveRecoveryQuery();
	void AbortAutoMoveQuery();
	void HandleAutoMoveGoalQueryFinished(TSharedPtr<FEnvQueryResult> Result);
	void HandleAutoMoveRecoveryQueryFinished(TSharedPtr<FEnvQueryResult> Result);
	void GenerateAutoMoveEQSItems(FEnvQueryInstance& QueryInstance, bool bRecoveryQuery) const;
	bool ScoreAutoMoveEQSItem(const FVector& ItemLocation, bool bRecoveryQuery, float& OutScore) const;
	bool GetAutoMoveCandidateClearance(const FVector& ItemLocation, float& OutClearance) const;
	bool GetAutoMoveSegmentClearance(const FVector& Start, const FVector& End, float& OutClearance) const;
	bool GetAutoRunPathLength(const FVector& Start, const FVector& End, double& OutPathLength) const;
	void ResetAutoMoveProgress();
	void UpdateAutoMoveProgress(float DeltaSeconds);
	void HandleBlockedAutoMove();
	void HandleAutoMovePathFinished(const FPathFollowingResult& Result);

	friend class UAuraEnvQueryGenerator_AutoMove;
	friend class UAuraEnvQueryTest_AutoMove;
	friend class UAuraCrowdFollowingComponent;

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
	FVector MagicCircleLocation = FVector::ZeroVector;
	bool bHasMagicCircleLocation = false;
	UPROPERTY()
	TObjectPtr<AMagicCircle> MagicCircle;
	UPROPERTY(EditDefaultsOnly, Category="Magic Circle")
	TSubclassOf<AMagicCircle> MagicCircleClass;
	
	UPROPERTY(EditDefaultsOnly, Category="Input")
	TObjectPtr<UAuraInputConfig> InputConfig;
	
	UPROPERTY()
	TObjectPtr<UAuraAbilitySystemComponent> AuraAbilitySystemComponent;
	
	FVector CachedDestination = FVector::ZeroVector;
	float FollowTime = 0.f;
	float ShortPressThreshold = 0.5f;
	bool bTargeting = false;
	bool bServerAutoMoveRequested = false;
	FVector AutoMoveSteeringVelocity = FVector::ZeroVector;
	FVector LastSentAutoMoveSteeringVelocity = FVector::ZeroVector;
	float NextAutoMoveSteeringSendTime = 0.f;
	EAutoMoveState AutoMoveState = EAutoMoveState::Idle;
	FVector RequestedDestination = FVector::ZeroVector;
	FVector ResolvedDestination = FVector::ZeroVector;
	FVector AutoRunMoveDestination = FVector::ZeroVector;
	FVector AutoMoveProgressTarget = FVector::ZeroVector;
	float AutoMoveBestDistanceToTarget = 0.f;
	TArray<FVector> FailedRecoveryDirections;
	int32 ConsecutiveRecoveryFailures = 0;
	float AutoMoveProgressCheckElapsed = 0.f;
	float AutoMoveBlockedTime = 0.f;
	float AutoMoveReplanCooldownRemaining = 0.f;
	int32 ActiveAutoMoveQueryId = INDEX_NONE;

	UPROPERTY(Transient)
	TObjectPtr<UEnvQuery> AutoMoveGoalQuery;

	UPROPERTY(Transient)
	TObjectPtr<UEnvQuery> AutoMoveRecoveryQuery;
	
	UPROPERTY(EditDefaultsOnly, Category="Auto Run", meta=(ClampMin="50.0"))
	float AutoRunAcceptanceRadius = 50.f;

	UPROPERTY(VisibleAnywhere, Category="Auto Run")
	TObjectPtr<UAuraCrowdFollowingComponent> CrowdPathFollowingComponent;

	UPROPERTY(EditDefaultsOnly, Category="Auto Run|EQS", meta=(ClampMin="4", ClampMax="32"))
	int32 AutoMoveSamplesPerRing = 16;

	UPROPERTY(EditDefaultsOnly, Category="Auto Run|EQS")
	TArray<float> AutoMoveGoalSearchRadii = {100.f, 180.f, 260.f, 340.f};

	UPROPERTY(EditDefaultsOnly, Category="Auto Run|EQS")
	TArray<float> AutoMoveRecoverySearchRadii = {180.f, 320.f, 500.f, 700.f};

	UPROPERTY(EditDefaultsOnly, Category="Auto Run|EQS", meta=(ClampMin="0.0"))
	float AutoMovePawnClearance = 60.f;

	UPROPERTY(EditDefaultsOnly, Category="Auto Run|EQS", meta=(ClampMin="0.0"))
	float AutoMovePathClearance = 20.f;

	UPROPERTY(EditDefaultsOnly, Category="Auto Run|EQS", meta=(ClampMin="100.0"))
	float AutoMoveRecoveryLookAhead = 500.f;

	UPROPERTY(EditDefaultsOnly, Category="Auto Run|EQS", meta=(ClampMin="0.0"))
	float AutoMoveRecoveryMaxRetreat = 450.f;

	UPROPERTY(EditDefaultsOnly, Category="Auto Run|EQS", meta=(ClampMin="-1.0", ClampMax="1.0"))
	float AutoMoveFailedDirectionDotThreshold = 0.35f;

	UPROPERTY(EditDefaultsOnly, Category="Auto Run|EQS", meta=(ClampMin="0.05"))
	float AutoMoveProgressCheckInterval = 0.15f;

	UPROPERTY(EditDefaultsOnly, Category="Auto Run|EQS", meta=(ClampMin="0.0"))
	float AutoMoveMinimumProgress = 10.f;

	UPROPERTY(EditDefaultsOnly, Category="Auto Run|EQS", meta=(ClampMin="0.25"))
	float AutoMoveBlockedTimeout = 0.45f;

	UPROPERTY(EditDefaultsOnly, Category="Auto Run|EQS", meta=(ClampMin="0.0"))
	float AutoMoveReplanCooldown = 0.25f;

	UPROPERTY(EditDefaultsOnly, Category="Auto Run|EQS", meta=(ClampMin="50.0"))
	float AutoMoveRecoveryAcceptanceRadius = 100.f;

	UPROPERTY(EditDefaultsOnly, Category="Auto Run|Avoidance", meta=(ClampMin="0.0"))
	float AutoMoveCrowdRadiusPadding = 10.f;
	
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
