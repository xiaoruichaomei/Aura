

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "GameFramework/Character.h"
#include "Interface/CombatInterface.h"
#include "BaseCharacter.generated.h"

class UNiagaraSystem;
class UMaterialInstance;
class UGameplayAbility;
class UAbilitySystemComponent;
class UAttributeSet;
class UGameplayEffect;

UCLASS(Abstract)
class AURA_API ABaseCharacter : public ACharacter, public IAbilitySystemInterface, public ICombatInterface
{
	GENERATED_BODY()

public:
	ABaseCharacter();
	virtual void BeginPlay() override;
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	UAttributeSet* GetAttributeSet() const { return AttributeSet; }
	
	// <CombatInterface>
	virtual FVector GetCombatSockettLocation_Implementation(const FGameplayTag& MontageTag) override;
	virtual bool IsDead_Implementation() const override;
	virtual AActor* GetAvatar_Implementation() override;
	virtual UAnimMontage* GetHitReactMontage_Implementation() override;
	virtual void Die() override;
	virtual TArray<FTaggedMontage> GetAttackMontages_Implementation() override;
	virtual UNiagaraSystem* GetBloodEffect_Implementation() override;
	virtual FTaggedMontage GetTaggedMontageByTag_Implementation(const FGameplayTag& Tag) override;
	// </CombatInterface>
	
	UFUNCTION(NetMulticast, Reliable)
	virtual void MulticastHandleDeath();
	
	UPROPERTY(EditAnywhere, Category="Combat")
	TArray<FTaggedMontage> AttackMontages;

protected:
	virtual void InitAbilityActorInfo();
	virtual void InitializeDefaultAttributes() const;
	void AddCharacterAbilities();
	void Dissolve();
	
	UFUNCTION(BlueprintImplementableEvent)
	void StartDissolveTimeline(UMaterialInstanceDynamic* DynamicMaterialInstance);
	
	UFUNCTION(BlueprintImplementableEvent)
	void StartWeaponDissolveTimeline(UMaterialInstanceDynamic* DynamicMaterialInstance);
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	TObjectPtr<USkeletalMeshComponent> Weapon;
	
	UPROPERTY(EditAnywhere, Category="Combat")
	FName WeaponTipSocketName;
	
	UPROPERTY(EditAnywhere, Category="Combat")
	FName RightHandTipSocketName;
	
	UPROPERTY(EditAnywhere, Category="Combat")
	FName LeftHandTipSocketName;
	
	UPROPERTY()
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY()
	TObjectPtr<UAttributeSet> AttributeSet;
	
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category="Attributes")
	TSubclassOf<UGameplayEffect> DefaultPrimaryAttributes;
	
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category="Attributes")
	TSubclassOf<UGameplayEffect> DefaultSecondaryAttributes;
	
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category="Attributes")
	TSubclassOf<UGameplayEffect> DefaultVitalAttributes;
	
	// <Dissolve Effects>
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UMaterialInstance> DissolveMaterialInstance;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UMaterialInstance> WeaponDissolveMaterialInstance;
	// </Dissolve Effects>
	
	bool bDead = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat")
	UNiagaraSystem* BloodEffect;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat")
	USoundBase* DeathSound;
	
private:
	void ApplyEffectToSelf(TSubclassOf<UGameplayEffect> GameplayEffectClass, float Level) const;
	
	UPROPERTY(EditAnywhere, Category="Abilities")
	TArray<TSubclassOf<UGameplayAbility>> StartupAbility;
	
	UPROPERTY(EditAnywhere, Category="Combat")
	TObjectPtr<UAnimMontage> HitReactMontage;
};
