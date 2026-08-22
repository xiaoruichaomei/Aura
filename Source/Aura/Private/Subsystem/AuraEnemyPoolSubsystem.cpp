#include "Subsystem/AuraEnemyPoolSubsystem.h"

#include "Character/AuraEnemy.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

bool UAuraEnemyPoolSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->WorldType != EWorldType::Editor && World->WorldType != EWorldType::EditorPreview;
}

void UAuraEnemyPoolSubsystem::Deinitialize()
{
	for (TPair<UClass*, FPoolBucket>& Pair : Buckets)
	{
		for (AAuraEnemy* Enemy : Pair.Value.All)
		{
			if (IsValid(Enemy))
			{
				Enemy->Destroy();
			}
		}
	}
	Buckets.Empty();
	bPoolReady = false;
	Super::Deinitialize();
}

void UAuraEnemyPoolSubsystem::RegisterPoolConfig(const FEnemyPoolClassConfig& Config)
{
	if (!Config.EnemyClass)
	{
		return;
	}

	if (FPoolBucket* ExistingBucket = Buckets.Find(Config.EnemyClass))
	{
		// Each spawn volume contributes independent concurrent demand for this class.
		ExistingBucket->Config.PrewarmCount += FMath::Max(0, Config.PrewarmCount);
		ExistingBucket->Config.HardPoolLimit += FMath::Max(1, Config.HardPoolLimit);
		ExistingBucket->Config.bAllowPoolExpansion =
			ExistingBucket->Config.bAllowPoolExpansion || Config.bAllowPoolExpansion;
		return;
	}

	FPoolBucket& Bucket = Buckets.Add(Config.EnemyClass);
	Bucket.Config = Config;
	Bucket.Config.PrewarmCount = FMath::Max(0, Config.PrewarmCount);
	Bucket.Config.HardPoolLimit = FMath::Max(1, Config.HardPoolLimit);
}

void UAuraEnemyPoolSubsystem::PrewarmPools()
{
	for (TPair<UClass*, FPoolBucket>& Pair : Buckets)
	{
		FPoolBucket& Bucket = Pair.Value;
		while (Bucket.All.Num() < Bucket.Config.PrewarmCount && Bucket.All.Num() < Bucket.Config.HardPoolLimit)
		{
			if (!CreatePooledEnemy(Bucket))
			{
				break;
			}
		}
	}
	bPoolReady = true;
}

AAuraEnemy* UAuraEnemyPoolSubsystem::AcquireEnemy(TSubclassOf<AAuraEnemy> EnemyClass, const FTransform& Transform)
{
	if (!EnemyClass)
	{
		return nullptr;
	}

	FPoolBucket* Bucket = FindBucket(EnemyClass);
	if (!Bucket)
	{
		FEnemyPoolClassConfig Config;
		Config.EnemyClass = EnemyClass;
		Config.PrewarmCount = 0;
		Config.HardPoolLimit = 1;
		RegisterPoolConfig(Config);
		Bucket = FindBucket(EnemyClass);
	}

	AAuraEnemy* Enemy = nullptr;
	while (Bucket && Bucket->Available.Num() > 0 && !IsValid(Enemy))
	{
		Enemy = Bucket->Available.Pop(EAllowShrinking::No);
	}

	if (!Enemy && Bucket && Bucket->All.Num() < Bucket->Config.HardPoolLimit && Bucket->Config.bAllowPoolExpansion)
	{
		Enemy = CreatePooledEnemy(*Bucket);
		if (Enemy)
		{
			Bucket->Available.RemoveSingle(Enemy);
		}
	}

	if (!Enemy || !Bucket)
	{
		return nullptr;
	}

	Bucket->Active.Add(Enemy);
	Enemy->ActivateFromPool(Transform);
	return Enemy;
}

void UAuraEnemyPoolSubsystem::ReleaseEnemy(AAuraEnemy* Enemy)
{
	if (!IsValid(Enemy))
	{
		return;
	}

	FPoolBucket* Bucket = FindBucket(Enemy->GetClass());
	if (!Bucket || !Bucket->Active.Contains(Enemy) && !Bucket->Dying.Contains(Enemy))
	{
		return;
	}

	Bucket->Active.Remove(Enemy);
	Bucket->Dying.Remove(Enemy);
	Enemy->DeactivateToPool();
	AddToAvailable(*Bucket, Enemy);
}

void UAuraEnemyPoolSubsystem::NotifyEnemyDying(AAuraEnemy* Enemy)
{
	if (!IsValid(Enemy))
	{
		return;
	}

	if (FPoolBucket* Bucket = FindBucket(Enemy->GetClass()))
	{
		if (Bucket->Active.Remove(Enemy) > 0)
		{
			Bucket->Dying.Add(Enemy);
		}
	}
}

FEnemyPoolStats UAuraEnemyPoolSubsystem::GetPoolStats(TSubclassOf<AAuraEnemy> EnemyClass) const
{
	FEnemyPoolStats Stats;
	if (const FPoolBucket* Bucket = FindBucket(EnemyClass))
	{
		Stats.Total = Bucket->All.Num();
		Stats.Available = Bucket->Available.Num();
		Stats.Active = Bucket->Active.Num();
		Stats.Dying = Bucket->Dying.Num();
	}
	return Stats;
}

UAuraEnemyPoolSubsystem::FPoolBucket* UAuraEnemyPoolSubsystem::FindBucket(UClass* EnemyClass)
{
	return EnemyClass ? Buckets.Find(EnemyClass) : nullptr;
}

const UAuraEnemyPoolSubsystem::FPoolBucket* UAuraEnemyPoolSubsystem::FindBucket(UClass* EnemyClass) const
{
	return EnemyClass ? Buckets.Find(EnemyClass) : nullptr;
}

AAuraEnemy* UAuraEnemyPoolSubsystem::CreatePooledEnemy(FPoolBucket& Bucket)
{
	if (!Bucket.Config.EnemyClass || Bucket.All.Num() >= Bucket.Config.HardPoolLimit)
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AAuraEnemy* Enemy = GetWorld()->SpawnActor<AAuraEnemy>(Bucket.Config.EnemyClass, FTransform::Identity, Params);
	if (Enemy)
	{
		Enemy->SetPoolManaged(true);
		if (!Enemy->GetController())
		{
			Enemy->SpawnDefaultController();
		}
		Enemy->DeactivateToPool();
		Bucket.All.Add(Enemy);
		Bucket.Available.Add(Enemy);
	}
	return Enemy;
}

void UAuraEnemyPoolSubsystem::AddToAvailable(FPoolBucket& Bucket, AAuraEnemy* Enemy)
{
	if (IsValid(Enemy) && !Bucket.Available.Contains(Enemy))
	{
		Bucket.Available.Add(Enemy);
	}
}
