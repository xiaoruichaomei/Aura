

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraComponent.h"
#include "Character/BaseCharacter.h"
#include "GameFramework/SpringArmComponent.h"
#include "Interface/PlayerInterface.h"
#include "AuraCharacter.generated.h"

class UNiagaraComponent;
class UAuraNiagaraComponent;
class AAuraPlayerState;
class UMaterialInterface;
/**
 * 
 */
UCLASS()
class AURA_API AAuraCharacter : public ABaseCharacter, public IPlayerInterface
{
	GENERATED_BODY()
	
public:
	AAuraCharacter();
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;
	virtual void Die() override;
	virtual void MulticastHandleDeath_Implementation() override;
	void RefreshAttributesAfterLoading();
	
	// <Combat Interface>
	virtual int32 GetLevel_Implementation() override;
	// </Combat Interface>
	
	// <Player Interface>
	virtual void AddToXP_Implementation(int32 XP) override;
	virtual void LevelUp_Implementation() override;
	virtual int32 GetXP_Implementation() const override;
	virtual int32 FindLevelForXP_Implementation(int32 XP) const override;
	virtual int32 GetAttributePointsReward_Implementation(int32 Level) const override;
	virtual int32 GetSpellPointsReward_Implementation(int32 Level) const override;
	virtual void AddToPlayerLevel_Implementation(int32 Level) override;
	virtual void AddToAttributePoints_Implementation(int32 AttributePoints) override;
	virtual void AddToSpellPoints_Implementation(int32 SpellPoints) override;
	virtual int32 GetAttributePoints_Implementation() const override;
	virtual int32 GetSpellPoints_Implementation() const override;
	virtual void ShowMagicCircle_Implementation(UMaterialInterface* DecalMaterial) override;
	virtual void HideMagicCircle_Implementation() override;
	virtual FVector GetMagicCircleLocation_Implementation() const override;
	// </Player Interface>
	
protected:
	virtual void InitAbilityActorInfo() override;
	virtual void BeginPlay() override;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<UNiagaraComponent> LevelUpNiagaraComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Passive Effects")
	TObjectPtr<UAuraNiagaraComponent> HaloNiagaraComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Passive Effects")
	TObjectPtr<UAuraNiagaraComponent> LifeSiphonNiagaraComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Passive Effects")
	TObjectPtr<UAuraNiagaraComponent> ManaSiphonNiagaraComponent;
	
private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCameraComponent> TopDownCameraComponent;
	
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USpringArmComponent> CameraBoom;
	
	AAuraPlayerState* GetAuraPlayerState() const;
	
	UFUNCTION(NetMulticast, Reliable)
	void MulticastLevelUpParticles() const;
};
