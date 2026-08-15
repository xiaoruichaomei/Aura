

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/AuraDamageGameplayAbility.h"
#include "AuraProjectileSpell.generated.h"

class AAuraProjectile;
/**
 * 
 */
UCLASS()
class AURA_API UAuraProjectileSpell : public UAuraDamageGameplayAbility
{
	GENERATED_BODY()
public:
protected:
	virtual FString GetResolvedDescription(int32 Level, const FAuraAbilityInfo& AbilityInfo) override;

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	UFUNCTION(BlueprintCallable, Category="Projectile")
	void SpawnProjectile(const FVector& ProjectileTargetLocation, const FGameplayTag& SocketTag, bool bOverridePitch = false, float PitchOverride = 0.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSubclassOf<AAuraProjectile> ProjectileClass;

	/** 弹体数量上限（实际数量 = min(技能等级, ProjectileSpread)，1级1颗…5级5颗，之后锁定） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Projectile")
	int32 ProjectileSpread = 5;

	/** 相邻弹体之间的 Yaw 偏移角度（度），弹体数>1 时生效 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Projectile")
	float SpreadYaw = 10.f;

	UPROPERTY(EditDefaultsOnly)
	int32 NumProjectiles = 5;

private:
	/** 第 Index 颗弹体的 Yaw 偏移（以目标方向为 0，向两侧展开），按实际发射数 NumToSpawn 分布 */
	float GetSpread(int32 Index, int32 NumToSpawn) const;
};
