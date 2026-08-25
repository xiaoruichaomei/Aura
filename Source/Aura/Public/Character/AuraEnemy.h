

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimTypes.h"
#include "AbilitySystem/Data/CharacterClassInfo.h"
#include "Character/BaseCharacter.h"
#include "Interface/EnemyInterface.h"
#include "Navigation/CrowdAgentInterface.h"
#include "UI/WidgetController/OverlayWidgetController.h"
#include "AuraEnemy.generated.h"

UENUM(BlueprintType)
enum class EEnemyPoolState : uint8
{
	Inactive,
	Active,
	Dying
};

enum class ECharacterClass : uint8;
class UWidgetComponent;
class UMaterialInterface;
class UAnimInstance;
class AAuraAIController;
class UBehaviorTree;
class AAuraEnemy;

DECLARE_MULTICAST_DELEGATE_OneParam(FEnemyDyingSignature, AAuraEnemy*);

/**
 * 
 */
UCLASS()
class AURA_API AAuraEnemy : public ABaseCharacter, public IEnemyInterface, public ICrowdAgentInterface
{
	GENERATED_BODY()
	
public:
	AAuraEnemy();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	
	void HitReactTagChanged(const FGameplayTag CallbackTag, int32 NewCount);
	void StunTagChanged(const FGameplayTag CallbackTag, int32 NewCount);
	virtual void Die() override;
	virtual void MulticastHandleDeath_Implementation() override;

	void SetPoolManaged(bool bInPoolManaged) { bPoolManaged = bInPoolManaged; }
	void ActivateFromPool(const FTransform& InTransform, int32 InLevel = 1);
	void DeactivateToPool();
	bool IsPoolManaged() const { return bPoolManaged; }
	EEnemyPoolState GetPoolState() const { return PoolState; }
	FGuid GetSpawnInstanceId() const { return SpawnInstanceId; }
	FGuid GetSpawnerId() const { return SpawnerId; }
	void SetPoolIdentity(const FGuid& InSpawnInstanceId, const FGuid& InSpawnerId);
	void SetPoolLevel(int32 InLevel);
	int32 GetEnemyLevel() const { return Level; }
	float GetCurrentHealth() const;
	void RestoreHealth(float InHealth);
	void HandleTargetActorInvalidated(AActor* InvalidTarget);

	FEnemyDyingSignature OnEnemyDyingDelegate;

	/** Stop/reset Blueprint death timelines here. Native mesh/material state is restored afterwards. */
	UFUNCTION(BlueprintImplementableEvent, Category="Pool", DisplayName="Reset Pool Blueprint State")
	void ResetPoolBlueprintState();

	/** 眩晕或受击任一生效都停走，都解除才恢复（避免受击结束提前放行眩晕中的敌人） */
	void UpdateMovementSpeed();
	
	// <Enemy Interface>
	virtual void SetActorHighlight(bool IsHighlight) override;
	// </Enemy Interface>
	
	// <Combat Interface>
	virtual int32 GetLevel_Implementation() override;
	virtual void SetCombatTarget_Implementation(AActor* InCombatTarget) override;
	virtual AActor* GetCombatTarget_Implementation() const override;
	// </Combat Interface>

	// Replicated enemy pawns act as avoidance agents on remote clients, where AI controllers do not exist.
	virtual FVector GetCrowdAgentLocation() const override;
	virtual FVector GetCrowdAgentVelocity() const override;
	virtual void GetCrowdAgentCollisions(float& CylinderRadius, float& CylinderHalfHeight) const override;
	virtual float GetCrowdAgentMaxSpeed() const override;
	
	UPROPERTY(BlueprintAssignable)
	FOnAttributeChangedSignature OnHealthChanged;
	
	UPROPERTY(BlueprintAssignable)
	FOnAttributeChangedSignature OnMaxHealthChanged;
	
protected:
	virtual void InitAbilityActorInfo() override;
	virtual void InitializeDefaultAttributes() const override;
	
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category="Character Class Defaults")
	int32 Level = 1;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<UWidgetComponent> HealthBar;
	
	UPROPERTY(BlueprintReadOnly, Category="Combat")
	bool bHitReacting = false;

	/** 是否处于眩晕状态（Effects.Debuff.Stun 标签数 > 0） */
	UPROPERTY(BlueprintReadOnly, Category="Combat")
	bool bStunned = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat")
	float BaseWalkSpeed = 250.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat")
	float LifeSpan = 5.f;
	
	UPROPERTY(EditAnywhere, Category="AI")
	TObjectPtr<UBehaviorTree> BehaviorTree;
	
	UPROPERTY()
	TObjectPtr<AAuraAIController> AuraAIController;
	
	UPROPERTY(BlueprintReadWrite, Category="Combat")
	TObjectPtr<AActor> CombatTarget;

	UPROPERTY(ReplicatedUsing=OnRep_PoolState, BlueprintReadOnly, Category="Pool")
	EEnemyPoolState PoolState = EEnemyPoolState::Inactive;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Pool")
	FGuid SpawnInstanceId;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Pool")
	FGuid SpawnerId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pool")
	bool bPoolManaged = false;

	UFUNCTION()
	void OnRep_PoolState();

	FTimerHandle PoolReturnTimer;
	FTimerHandle StunAnimationTimer;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> InitialMeshMaterials;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> InitialWeaponMaterials;

	UPROPERTY(Transient)
	TSubclassOf<UAnimInstance> InitialAnimClass;

	FTransform InitialMeshRelativeTransform;
	FTransform InitialWeaponRelativeTransform;
	FName InitialWeaponAttachSocket;
	FCollisionResponseContainer InitialMeshCollisionResponses;
	TEnumAsByte<ECollisionEnabled::Type> InitialMeshCollisionEnabled = ECollisionEnabled::QueryOnly;
	ERootMotionMode::Type InitialRootMotionMode = ERootMotionMode::RootMotionFromMontagesOnly;
	bool bPoolDefaultsCaptured = false;
	bool bClientCrowdRegistered = false;

	void SetPoolState(EEnemyPoolState NewState);
	void CapturePoolDefaults();
	void ResetNativePoolState();
	void UpdateRootMotionMode();
	void EnsureStunAnimation();
	void UpdateClientCrowdRegistration(bool bShouldRegister);
};
