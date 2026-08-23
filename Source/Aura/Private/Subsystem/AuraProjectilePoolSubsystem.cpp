#include "Subsystem/AuraProjectilePoolSubsystem.h"

#include "Actor/AuraFireBall.h"
#include "Actor/AuraProjectile.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

bool UAuraProjectilePoolSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->WorldType != EWorldType::Editor && World->WorldType != EWorldType::EditorPreview;
}

void UAuraProjectilePoolSubsystem::Deinitialize()
{
	bIsShuttingDown = true;
	for (TPair<UClass*, TPoolBucket<AAuraProjectile>>& Pair : ProjectileBuckets)
	{
		for (AAuraProjectile* Projectile : Pair.Value.All)
		{
			if (IsValid(Projectile))
			{
				Projectile->Destroy();
			}
		}
	}
	for (TPair<UClass*, TPoolBucket<AAuraFireBall>>& Pair : FireBallBuckets)
	{
		for (AAuraFireBall* FireBall : Pair.Value.All)
		{
			if (IsValid(FireBall))
			{
				FireBall->Destroy();
			}
		}
	}
	ProjectileBuckets.Empty();
	FireBallBuckets.Empty();
	Super::Deinitialize();
}

void UAuraProjectilePoolSubsystem::PrewarmProjectiles(TSubclassOf<AAuraProjectile> ProjectileClass, int32 Count, int32 HardLimit)
{
	if (bIsShuttingDown || !ProjectileClass || !GetWorld() || GetWorld()->GetNetMode() == NM_Client)
	{
		return;
	}
	TPoolBucket<AAuraProjectile>& Bucket = ProjectileBuckets.FindOrAdd(ProjectileClass);
	Bucket.HardLimit = FMath::Max(Bucket.HardLimit, FMath::Max(Count, HardLimit));
	while (Bucket.All.Num() < Count && Bucket.All.Num() < Bucket.HardLimit)
	{
		if (!CreateProjectile(ProjectileClass, Bucket))
		{
			break;
		}
	}
}

AAuraProjectile* UAuraProjectilePoolSubsystem::AcquireProjectile(TSubclassOf<AAuraProjectile> ProjectileClass,
	const FTransform& Transform, AActor* Owner, APawn* Instigator)
{
	if (bIsShuttingDown || !ProjectileClass || !GetWorld() || GetWorld()->GetNetMode() == NM_Client)
	{
		return nullptr;
	}
	PrewarmProjectiles(ProjectileClass, 0);
	TPoolBucket<AAuraProjectile>& Bucket = ProjectileBuckets.FindChecked(ProjectileClass);
	AAuraProjectile* Projectile = nullptr;
	while (Bucket.Available.Num() > 0 && !IsValid(Projectile))
	{
		Projectile = Bucket.Available.Pop(EAllowShrinking::No);
	}
	if (!Projectile && Bucket.All.Num() < Bucket.HardLimit)
	{
		Projectile = CreateProjectile(ProjectileClass, Bucket);
		Bucket.Available.RemoveSingle(Projectile);
	}
	if (Projectile)
	{
		Bucket.Active.Add(Projectile);
		Projectile->ActivateFromPool(Transform, Owner, Instigator);
	}
	return Projectile;
}

void UAuraProjectilePoolSubsystem::ReleaseProjectile(AAuraProjectile* Projectile)
{
	if (bIsShuttingDown || !IsValid(Projectile))
	{
		return;
	}
	TPoolBucket<AAuraProjectile>* Bucket = ProjectileBuckets.Find(Projectile->GetClass());
	if (!Bucket || Bucket->Active.Remove(Projectile) == 0)
	{
		return;
	}
	Projectile->DeactivateToPool();
	Bucket->Available.AddUnique(Projectile);
}

void UAuraProjectilePoolSubsystem::PrewarmFireBalls(TSubclassOf<AAuraFireBall> FireBallClass, int32 Count, int32 HardLimit)
{
	if (bIsShuttingDown || !FireBallClass || !GetWorld() || GetWorld()->GetNetMode() == NM_Client)
	{
		return;
	}
	TPoolBucket<AAuraFireBall>& Bucket = FireBallBuckets.FindOrAdd(FireBallClass);
	Bucket.HardLimit = FMath::Max(Bucket.HardLimit, FMath::Max(Count, HardLimit));
	while (Bucket.All.Num() < Count && Bucket.All.Num() < Bucket.HardLimit)
	{
		if (!CreateFireBall(FireBallClass, Bucket))
		{
			break;
		}
	}
}

AAuraFireBall* UAuraProjectilePoolSubsystem::AcquireFireBall(TSubclassOf<AAuraFireBall> FireBallClass,
	const FTransform& Transform, AActor* Owner, APawn* Instigator)
{
	if (bIsShuttingDown || !FireBallClass || !GetWorld() || GetWorld()->GetNetMode() == NM_Client)
	{
		return nullptr;
	}
	PrewarmFireBalls(FireBallClass, 0);
	TPoolBucket<AAuraFireBall>& Bucket = FireBallBuckets.FindChecked(FireBallClass);
	AAuraFireBall* FireBall = nullptr;
	while (Bucket.Available.Num() > 0 && !IsValid(FireBall))
	{
		FireBall = Bucket.Available.Pop(EAllowShrinking::No);
	}
	if (!FireBall && Bucket.All.Num() < Bucket.HardLimit)
	{
		FireBall = CreateFireBall(FireBallClass, Bucket);
		Bucket.Available.RemoveSingle(FireBall);
	}
	if (FireBall)
	{
		Bucket.Active.Add(FireBall);
		FireBall->ActivateFromPool(Transform, Owner, Instigator);
	}
	return FireBall;
}

void UAuraProjectilePoolSubsystem::ReleaseFireBall(AAuraFireBall* FireBall)
{
	if (bIsShuttingDown || !IsValid(FireBall))
	{
		return;
	}
	TPoolBucket<AAuraFireBall>* Bucket = FireBallBuckets.Find(FireBall->GetClass());
	if (!Bucket || Bucket->Active.Remove(FireBall) == 0)
	{
		return;
	}
	FireBall->DeactivateToPool();
	Bucket->Available.AddUnique(FireBall);
}

AAuraProjectile* UAuraProjectilePoolSubsystem::CreateProjectile(UClass* ProjectileClass,
	TPoolBucket<AAuraProjectile>& Bucket)
{
	if (!ProjectileClass || Bucket.All.Num() >= Bucket.HardLimit)
	{
		return nullptr;
	}
	AAuraProjectile* Projectile = GetWorld()->SpawnActorDeferred<AAuraProjectile>(
		ProjectileClass, FTransform::Identity, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Projectile)
	{
		Projectile->SetPoolManaged(true);
		Projectile->FinishSpawning(FTransform::Identity);
		Projectile->DeactivateToPool();
		Bucket.All.Add(Projectile);
		Bucket.Available.Add(Projectile);
	}
	return Projectile;
}

AAuraFireBall* UAuraProjectilePoolSubsystem::CreateFireBall(UClass* FireBallClass,
	TPoolBucket<AAuraFireBall>& Bucket)
{
	if (!FireBallClass || Bucket.All.Num() >= Bucket.HardLimit)
	{
		return nullptr;
	}
	AAuraFireBall* FireBall = GetWorld()->SpawnActorDeferred<AAuraFireBall>(
		FireBallClass, FTransform::Identity, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (FireBall)
	{
		FireBall->SetPoolManaged(true);
		FireBall->FinishSpawning(FTransform::Identity);
		FireBall->DeactivateToPool();
		Bucket.All.Add(FireBall);
		Bucket.Available.Add(FireBall);
	}
	return FireBall;
}
