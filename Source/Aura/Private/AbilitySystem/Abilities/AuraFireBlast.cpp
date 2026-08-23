#include "AbilitySystem/Abilities/AuraFireBlast.h"

#include "Actor/AuraFireBall.h"
#include "AbilitySystem/AuraAbilitySystemLibrary.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AuraGameplayTags.h"
#include "Engine/World.h"
#include "GameplayEffect.h"
#include "GameFramework/Pawn.h"
#include "UObject/ConstructorHelpers.h"
#include "Subsystem/AuraProjectilePoolSubsystem.h"

UAuraFireBlast::UAuraFireBlast()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerExecution;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(FAuraGameplayTags::Get().Abilities_Fire_FireBlast);
	SetAssetTags(AssetTags);

	static ConstructorHelpers::FClassFinder<UGameplayEffect> CostClass(
		TEXT("/Game/Blueprints/AbilitySystem/GameplayAbilities/Attack/Ranged/FireBlast/GE_Cost_FireBlast"));
	static ConstructorHelpers::FClassFinder<UGameplayEffect> CooldownClass(
		TEXT("/Game/Blueprints/AbilitySystem/GameplayAbilities/Attack/Ranged/FireBlast/GE_Cooldown_FireBlast"));
	if (CostClass.Succeeded())
	{
		CostGameplayEffectClass = CostClass.Class;
	}
	if (CooldownClass.Succeeded())
	{
		CooldownGameplayEffectClass = CooldownClass.Class;
	}

	FireBallClass = TSoftClassPtr<AAuraFireBall>(
		FSoftObjectPath(TEXT("/Game/Blueprints/Actor/FireBlast/BP_FireBall.BP_FireBall_C")));
}

FString UAuraFireBlast::GetResolvedDescription(int32 Level, const FAuraAbilityInfo& AbilityInfo)
{
	FString Description = Super::GetResolvedDescription(Level, AbilityInfo);
	const int32 BaseDamage = FMath::RoundToInt(DamageEffectParams.BaseDamage.GetValueAtLevel(Level));
	const int32 ExplosionDamage = FMath::RoundToInt(BaseDamage * ExplosionDamageMultiplier);
	Description = Description.Replace(TEXT("{FireBallCount}"), *FString::FromInt(FMath::Clamp(NumFireBalls, 1, 24)));
	Description = Description.Replace(TEXT("{ExplosionDamage}"), *FString::FromInt(ExplosionDamage));
	Description = Description.Replace(TEXT("{ExplosionRadius}"), *FString::FromInt(FMath::RoundToInt(ExplosionRadius)));
	return Description;
}

void UAuraFireBlast::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITOR
	const FGameplayTag FireBlastTag = FAuraGameplayTags::Get().Abilities_Fire_FireBlast;
	if (IsTemplate() && !GetAssetTags().HasTagExact(FireBlastTag))
	{
		Modify();
		EditorGetAssetTags().AddTag(FireBlastTag);
		MarkPackageDirty();
	}
#endif
}

void UAuraFireBlast::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	SpawnFireBalls();
	if (!ActorInfo || !ActorInfo->IsNetAuthority())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
	}
}

void UAuraFireBlast::SpawnFireBalls()
{
	AActor* AvatarActor = GetAvatarActorFromActorInfo();
	if (!AvatarActor)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}
	if (!AvatarActor->HasAuthority())
	{
		return;
	}
	TSubclassOf<AAuraFireBall> SpawnClass = FireBallClass.LoadSynchronous();
	if (!SpawnClass)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	const int32 FireBallCount = FMath::Clamp(NumFireBalls, 1, 24);
	UAuraProjectilePoolSubsystem* ProjectilePool = GetWorld()->GetSubsystem<UAuraProjectilePoolSubsystem>();
	if (!ProjectilePool)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}
	ProjectilePool->PrewarmFireBalls(SpawnClass, FMath::Max(FireBallPoolPrewarmCount, FireBallCount));
	SpawnedFireBallCount = 0;
	ReturnedFireBallCount = 0;
	bExplosionTriggered = false;
	ActiveFireBalls.Reset();
	const FVector Center = AvatarActor->GetActorLocation();
	for (int32 Index = 0; Index < FireBallCount; ++Index)
	{
		const float AngleRadians = (2.f * PI * Index) / FireBallCount;
		const FVector Direction(FMath::Cos(AngleRadians), FMath::Sin(AngleRadians), 0.f);
		FTransform SpawnTransform(Direction.Rotation(), Center + Direction * SpawnRadius);

		AAuraFireBall* FireBall = ProjectilePool->AcquireFireBall(
			SpawnClass, SpawnTransform, GetOwningActorFromActorInfo(), Cast<APawn>(GetOwningActorFromActorInfo()));
		if (FireBall)
		{
			FireBall->SetSourceActor(AvatarActor);
			FireBall->SetDamageEffectSpecHandle(MakeDamageEffectSpec(DamageEffectParams));
			FireBall->ConfigureFlight(Direction, MaxTravelDistance, OutgoingDuration, ReturnDuration);
			FireBall->OnFireBallFinished.AddDynamic(this, &ThisClass::OnFireBallFinished);
			++SpawnedFireBallCount;
			ActiveFireBalls.Add(FireBall);
		}
	}

	if (SpawnedFireBallCount == 0)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}
	GetWorld()->GetTimerManager().SetTimer(ReturnTimeoutHandle, this, &ThisClass::HandleReturnTimeout, ReturnTimeout, false);
}

void UAuraFireBlast::OnFireBallFinished(AAuraFireBall* FireBall)
{
	if (!GetAvatarActorFromActorInfo() || !GetAvatarActorFromActorInfo()->HasAuthority() || bExplosionTriggered)
	{
		return;
	}

	++ReturnedFireBallCount;
	if (ReturnedFireBallCount >= SpawnedFireBallCount)
	{
		ExplodeAtOwner();
	}
}

void UAuraFireBlast::HandleReturnTimeout()
{
	ExplodeAtOwner();
}

void UAuraFireBlast::ExplodeAtOwner()
{
	if (bExplosionTriggered)
	{
		return;
	}
	bExplosionTriggered = true;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ReturnTimeoutHandle);
	}

	AActor* AvatarActor = GetAvatarActorFromActorInfo();
	UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo();
	if (AvatarActor && SourceASC)
	{
		FGameplayCueParameters CueParameters(SourceASC->MakeEffectContext());
		CueParameters.Location = AvatarActor->GetActorLocation();
		CueParameters.Instigator = AvatarActor;
		CueParameters.EffectCauser = AvatarActor;
		SourceASC->ExecuteGameplayCue(FAuraGameplayTags::Get().GameplayCue_FireBlast_Explosion, CueParameters);

		TArray<AActor*> IgnoreActors;
		IgnoreActors.Add(AvatarActor);
		TArray<AActor*> Targets;
		UAuraAbilitySystemLibrary::GetLivePlayersWithinRadius(this, Targets, IgnoreActors, ExplosionRadius, AvatarActor->GetActorLocation());
		for (AActor* Target : Targets)
		{
			if (!IsValid(Target) || Target == AvatarActor || Target == GetAvatarActorFromActorInfo() ||
				!UAuraAbilitySystemLibrary::IsNotFriend(AvatarActor, Target))
			{
				continue;
			}
			UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target);
			if (!TargetASC)
			{
				continue;
			}
			FAuraDamageEffectParams Params = DamageEffectParams;
			Params.BaseDamage = DamageEffectParams.BaseDamage.GetValueAtLevel(GetAbilityLevel()) * ExplosionDamageMultiplier;
			const FVector Direction = (Target->GetActorLocation() - AvatarActor->GetActorLocation()).GetSafeNormal2D();
			Params.KnockbackForce = (Direction + FVector::UpVector * .15f).GetSafeNormal() * Params.KnockbackMagnitude;
			Params.DeathImpulse = (Direction + FVector::UpVector * .3f).GetSafeNormal() * Params.DeathImpulseMagnitude;
			const FGameplayEffectSpecHandle Spec = MakeDamageEffectSpec(Params);
			if (Spec.Data.IsValid())
			{
				SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), TargetASC);
			}
		}
	}

	for (const TWeakObjectPtr<AAuraFireBall>& FireBall : ActiveFireBalls)
	{
		if (FireBall.IsValid())
		{
			FireBall->ReturnToPool();
		}
	}
	ActiveFireBalls.Reset();
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UAuraFireBlast::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ReturnTimeoutHandle);
	}
	if (!bExplosionTriggered)
	{
		for (const TWeakObjectPtr<AAuraFireBall>& FireBall : ActiveFireBalls)
		{
			if (FireBall.IsValid())
			{
				FireBall->ReturnToPool();
			}
		}
		ActiveFireBalls.Reset();
	}
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
