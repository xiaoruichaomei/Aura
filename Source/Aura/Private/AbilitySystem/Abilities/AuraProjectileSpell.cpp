



#include "AbilitySystem/Abilities/AuraProjectileSpell.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AuraGameplayTags.h"
#include "Actor/AuraProjectile.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Interface/CombatInterface.h"
#include "Subsystem/AuraProjectilePoolSubsystem.h"

FString UAuraProjectileSpell::GetResolvedDescription(int32 Level, const FAuraAbilityInfo& AbilityInfo)
{
	FString Description = Super::GetResolvedDescription(Level, AbilityInfo);
	return Description.Replace(TEXT("{NumProjectiles}"), *FString::FromInt(FMath::Min(Level, ProjectileSpread)));
}

void UAuraProjectileSpell::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);


}

float UAuraProjectileSpell::GetSpread(int32 Index, int32 NumToSpawn) const
{
	const bool bShouldSpread = NumToSpawn > 1;
	const float Half = (NumToSpawn - 1) * 0.5f;
	return bShouldSpread ? (Index - Half) * SpreadYaw : 0.f;
}

void UAuraProjectileSpell::SpawnProjectile(const FVector& ProjectileTargetLocation, const FGameplayTag& SocketTag, bool bOverridePitch, float PitchOverride)
{
	const bool bIsServer = GetAvatarActorFromActorInfo()->HasAuthority();;
	if (!bIsServer)
	{
		return;
	}

	const FVector SocketLocation = ICombatInterface::Execute_GetCombatSockettLocation(GetAvatarActorFromActorInfo(), SocketTag);
	FRotator BaseRotation = (ProjectileTargetLocation - SocketLocation).Rotation();
	BaseRotation.Pitch = 0.f;
	if (bOverridePitch)
	{
		BaseRotation.Pitch = PitchOverride;
	}

	// 扇形散射：弹体数量按等级缩放（1级1颗…上限ProjectileSpread颗），
	// 每颗围绕目标方向偏转 GetSpread(i) 的 Yaw
	const int32 NumToSpawn = FMath::Min(GetAbilityLevel(), ProjectileSpread);
	UAuraProjectilePoolSubsystem* ProjectilePool = GetWorld()->GetSubsystem<UAuraProjectilePoolSubsystem>();
	if (!ProjectilePool)
	{
		return;
	}
	ProjectilePool->PrewarmProjectiles(ProjectileClass, ProjectilePoolPrewarmCount);
	for (int32 i = 0; i < NumToSpawn; ++i)
	{
		FRotator Rotation = BaseRotation;
		Rotation.Yaw += GetSpread(i, NumToSpawn);

		FTransform SpawnTransform;
		SpawnTransform.SetLocation(SocketLocation);
		SpawnTransform.SetRotation(Rotation.Quaternion());

		AAuraProjectile* Projectile = ProjectilePool->AcquireProjectile(
			ProjectileClass, SpawnTransform, GetOwningActorFromActorInfo(), Cast<APawn>(GetOwningActorFromActorInfo()));
		if (!Projectile)
		{
			continue;
		}

		FAuraDamageEffectParams Params = DamageEffectParams;
		const FVector Direction = (ProjectileTargetLocation - SocketLocation).GetSafeNormal2D();
		// 加一点 Z 上抛分量：纯水平冲量会被倒地尸体与地面的摩擦吸收，几乎看不到
		Params.DeathImpulse = Direction * Params.DeathImpulseMagnitude + FVector(0.f, 0.f, Params.DeathImpulseMagnitude * 0.3f);

		const FGameplayEffectSpecHandle SpecHandle = MakeDamageEffectSpec(Params);

		Projectile->DamageEffectSpecHandle = SpecHandle;

	}
}
