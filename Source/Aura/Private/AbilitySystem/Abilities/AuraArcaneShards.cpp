#include "AbilitySystem/Abilities/AuraArcaneShards.h"

#include "AbilitySystem/AuraAbilitySystemLibrary.h"
#include "AbilitySystemComponent.h"
#include "AuraGameplayTags.h"
#include "Interface/PlayerInterface.h"

UAuraArcaneShards::UAuraArcaneShards()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerExecution;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

FString UAuraArcaneShards::GetResolvedDescription(int32 Level, const FAuraAbilityInfo& AbilityInfo)
{
	FString Description = Super::GetResolvedDescription(Level, AbilityInfo);
	const int32 ShardCount = FMath::Clamp(3 + 2 * (Level - 1), 3, MaxShardPoints);
	Description = Description.Replace(TEXT("{ShardCount}"), *FString::FromInt(ShardCount));
	Description = Description.Replace(TEXT("{PointCount}"), *FString::FromInt(ShardCount));
	Description = Description.Replace(TEXT("{CastRange}"), *FString::FromInt(FMath::RoundToInt(MaxCastRange)));
	Description = Description.Replace(TEXT("{InnerRadius}"), *FString::FromInt(FMath::RoundToInt(InnerRadius)));
	Description = Description.Replace(TEXT("{OuterRadius}"), *FString::FromInt(FMath::RoundToInt(OuterRadius)));
	Description = Description.Replace(TEXT("{MinimumDamagePercent}"), *FString::FromInt(FMath::RoundToInt(MinimumDamagePercent * 100.f)));
	return Description;
}

void UAuraArcaneShards::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!ActorInfo || !ActorInfo->AvatarActor.IsValid() || !CheckCost(Handle, ActorInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ActorInfo->AbilitySystemComponent->AddLooseGameplayTag(FAuraGameplayTags::Get().Player_Targeting_MagicCircle);
	if (ActorInfo->AvatarActor->Implements<UPlayerInterface>())
	{
		IPlayerInterface::Execute_ShowMagicCircle(ActorInfo->AvatarActor.Get(), MagicCircleMaterial);
	}
}

void UAuraArcaneShards::ConfirmTargetFromServer(const FVector& Center)
{
	if (!GetAvatarActorFromActorInfo() || !GetAvatarActorFromActorInfo()->HasAuthority())
	{
		return;
	}
	if (Center.ContainsNaN() || FVector::Dist2D(Center, GetAvatarActorFromActorInfo()->GetActorLocation()) > MaxCastRange || !CommitAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo))
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}
	ClearTargeting();
	StartShardSequence(Center);
}

void UAuraArcaneShards::StartShardSequence(const FVector& Center)
{
	PendingShardPoints = BuildShardPoints(Center);
	CurrentPointIndex = 0;
	if (PendingShardPoints.IsEmpty())
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}
	TriggerNextShard();
	GetWorld()->GetTimerManager().SetTimer(ShardTimerHandle, this, &ThisClass::TriggerNextShard, SpawnInterval, true);
}

void UAuraArcaneShards::TriggerNextShard()
{
	if (!PendingShardPoints.IsValidIndex(CurrentPointIndex))
	{
		GetWorld()->GetTimerManager().ClearTimer(ShardTimerHandle);
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
		return;
	}
	TriggerShardAtPoint(PendingShardPoints[CurrentPointIndex++]);
}

TArray<FVector> UAuraArcaneShards::BuildShardPoints(const FVector& Center) const
{
	TArray<FVector> Points;
	const int32 PointCount = FMath::Clamp(3 + 2 * (GetAbilityLevel() - 1), 3, MaxShardPoints);
	Points.Add(Center);
	for (int32 Index = 1; Index < PointCount; ++Index)
	{
		const int32 Ring = (Index - 1) / 6 + 1;
		const int32 IndexOnRing = (Index - 1) % 6;
		const float Angle = (2.f * PI * IndexOnRing / 6.f) + Ring * .35f;
		const FVector Candidate = Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * RingSpacing * Ring;
		FHitResult GroundHit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ArcaneShardGround), false, GetAvatarActorFromActorInfo());
		FCollisionObjectQueryParams GroundObjects;
		GroundObjects.AddObjectTypesToQuery(ECC_WorldStatic);
		GroundObjects.AddObjectTypesToQuery(ECC_WorldDynamic);
		if (GetWorld()->LineTraceSingleByObjectType(GroundHit, Candidate + FVector(0, 0, 500), Candidate - FVector(0, 0, 500), GroundObjects, Params))
		{
			Points.Add(GroundHit.ImpactPoint);
		}
	}
	return Points;
}

float UAuraArcaneShards::ComputeRadialDamageScale(float Distance) const
{
	if (OuterRadius <= InnerRadius || Distance <= InnerRadius) return 1.f;
	if (Distance >= OuterRadius) return FMath::Clamp(MinimumDamagePercent, 0.f, 1.f);
	const float Alpha = (Distance - InnerRadius) / (OuterRadius - InnerRadius);
	return FMath::Lerp(1.f, FMath::Clamp(MinimumDamagePercent, 0.f, 1.f), FMath::Pow(Alpha, DamageFalloffExponent));
}

void UAuraArcaneShards::TriggerShardAtPoint(const FVector& Point)
{
	UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo();
	if (!SourceASC)
	{
		return;
	}

	// GameplayCueNotify_Burst evaluates its spawn condition from the cue context.
	// Supplying the caster makes the Burst valid for the owning local player.
	FGameplayCueParameters CueParameters(SourceASC->MakeEffectContext());
	CueParameters.Location = Point;
	CueParameters.Instigator = GetAvatarActorFromActorInfo();
	CueParameters.EffectCauser = GetAvatarActorFromActorInfo();
	SourceASC->ExecuteGameplayCue(FAuraGameplayTags::Get().GameplayCue_ArcaneShards, CueParameters);

	TArray<AActor*> IgnoreActors;
	IgnoreActors.Add(GetAvatarActorFromActorInfo());
	TArray<AActor*> Targets;
	UAuraAbilitySystemLibrary::GetLivePlayersWithinRadius(this, Targets, IgnoreActors, OuterRadius, Point);
	for (AActor* Target : Targets)
	{
		if (!IsValid(Target) || !UAuraAbilitySystemLibrary::IsNotFriend(GetAvatarActorFromActorInfo(), Target)) continue;
		UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target);
		if (!TargetASC) continue;
		FAuraDamageEffectParams Params = DamageEffectParams;
		Params.BaseDamage = DamageEffectParams.BaseDamage.GetValueAtLevel(GetAbilityLevel()) * ComputeRadialDamageScale(FVector::Dist2D(Target->GetActorLocation(), Point));
		const FVector Direction = (Target->GetActorLocation() - Point).GetSafeNormal2D();
		Params.KnockbackForce = (Direction + FVector::UpVector * .15f).GetSafeNormal() * Params.KnockbackMagnitude;
		Params.DeathImpulse = (Direction + FVector::UpVector * .3f).GetSafeNormal() * Params.DeathImpulseMagnitude;
		const FGameplayEffectSpecHandle Spec = MakeDamageEffectSpec(Params);
		if (Spec.Data.IsValid()) GetAbilitySystemComponentFromActorInfo()->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), TargetASC);
	}
}

void UAuraArcaneShards::ClearTargeting()
{
	if (const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo())
	{
		ActorInfo->AbilitySystemComponent->RemoveLooseGameplayTag(FAuraGameplayTags::Get().Player_Targeting_MagicCircle);
		if (ActorInfo->AvatarActor.IsValid() && ActorInfo->AvatarActor->Implements<UPlayerInterface>()) IPlayerInterface::Execute_HideMagicCircle(ActorInfo->AvatarActor.Get());
	}
}

void UAuraArcaneShards::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	ClearTargeting();
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(ShardTimerHandle);
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
