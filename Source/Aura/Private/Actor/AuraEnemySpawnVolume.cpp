#include "Actor/AuraEnemySpawnVolume.h"

#include "Character/AuraEnemy.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "Subsystem/AuraEnemyPoolSubsystem.h"

AAuraEnemySpawnVolume::AAuraEnemySpawnVolume()
{
	PrimaryActorTick.bCanEverTick = false;
	SetReplicates(false);
	SpawnBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("SpawnBounds"));
	SetRootComponent(SpawnBounds);
	SpawnBounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SpawnerId = FGuid::NewGuid();
}

void AAuraEnemySpawnVolume::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority())
	{
		return;
	}

	if (UAuraEnemyPoolSubsystem* Pool = GetWorld()->GetSubsystem<UAuraEnemyPoolSubsystem>())
	{
		struct FWeightedPoolClass
		{
			TSubclassOf<AAuraEnemy> EnemyClass;
			float Weight = 0.f;
			float Remainder = 0.f;
			int32 PrewarmCount = 0;
		};

		TArray<FWeightedPoolClass> WeightedClasses;
		float TotalWeight = 0.f;
		for (const FEnemySpawnEntry& Entry : EnemyPool)
		{
			if (!Entry.EnemyClass || Entry.Weight <= 0.f)
			{
				continue;
			}

			FWeightedPoolClass* Existing = WeightedClasses.FindByPredicate([&Entry](const FWeightedPoolClass& Item)
			{
				return Item.EnemyClass == Entry.EnemyClass;
			});
			if (!Existing)
			{
				Existing = &WeightedClasses.AddDefaulted_GetRef();
				Existing->EnemyClass = Entry.EnemyClass;
			}
			Existing->Weight += Entry.Weight;
			TotalWeight += Entry.Weight;
		}

		int32 AssignedPrewarmCount = 0;
		if (TotalWeight > 0.f)
		{
			for (FWeightedPoolClass& Item : WeightedClasses)
			{
				const float ExactCount = MaxAliveEnemies * Item.Weight / TotalWeight;
				Item.PrewarmCount = FMath::FloorToInt(ExactCount);
				Item.Remainder = ExactCount - Item.PrewarmCount;
				AssignedPrewarmCount += Item.PrewarmCount;
			}
		}

		while (AssignedPrewarmCount < MaxAliveEnemies && WeightedClasses.Num() > 0)
		{
			FWeightedPoolClass* BestRemainder = &WeightedClasses[0];
			for (FWeightedPoolClass& Item : WeightedClasses)
			{
				if (Item.Remainder > BestRemainder->Remainder)
				{
					BestRemainder = &Item;
				}
			}
			++BestRemainder->PrewarmCount;
			BestRemainder->Remainder = -1.f;
			++AssignedPrewarmCount;
		}

		for (const FWeightedPoolClass& Item : WeightedClasses)
		{
			FEnemyPoolClassConfig Config;
			Config.EnemyClass = Item.EnemyClass;
			Config.PrewarmCount = Item.PrewarmCount;
			Config.HardPoolLimit = MaxAliveEnemies;
			Config.bAllowPoolExpansion = true;
			Pool->RegisterPoolConfig(Config);
		}
		Pool->PrewarmPools();
	}
}

void AAuraEnemySpawnVolume::StartSpawning()
{
	if (!HasAuthority() || GetWorldTimerManager().IsTimerActive(SpawnTimer))
	{
		return;
	}
	if (!bSpawnSessionInitialized)
	{
		if (!bHasImportedState)
		{
			SpawnSequence = 0;
			TimeUntilNextSpawn = 0.f;
			if (!bUseFixedRandomSeed)
			{
				RandomSeed = static_cast<int32>(GetTypeHash(FGuid::NewGuid()));
			}
		}
		bSpawnSessionInitialized = true;
	}

	const int32 DesiredInitialCount = bHasImportedState ? 0 : InitialSpawnCount;
	for (int32 Index = 0; Index < DesiredInitialCount && ActiveEnemies.Num() < MaxAliveEnemies; ++Index)
	{
		SpawnOne();
	}
	GetWorldTimerManager().SetTimer(SpawnTimer, this, &AAuraEnemySpawnVolume::SpawnTick, 1.f, true);
}

void AAuraEnemySpawnVolume::StopSpawning()
{
	GetWorldTimerManager().ClearTimer(SpawnTimer);
}

void AAuraEnemySpawnVolume::SpawnTick()
{
	ActiveEnemies.RemoveAll([](const TObjectPtr<AAuraEnemy>& Enemy)
	{
		return !IsValid(Enemy) || Enemy->GetPoolState() != EEnemyPoolState::Active;
	});

	if (ActiveEnemies.Num() >= MaxAliveEnemies)
	{
		return;
	}

	if (TimeUntilNextSpawn > 0.f)
	{
		TimeUntilNextSpawn -= 1.f;
		return;
	}

	if (SpawnOne())
	{
		TimeUntilNextSpawn = FMath::FRandRange(RespawnDelayMin, RespawnDelayMax);
	}
}

AAuraEnemy* AAuraEnemySpawnVolume::SpawnOne()
{
	if (ActiveEnemies.Num() >= MaxAliveEnemies || EnemyPool.Num() == 0)
	{
		return nullptr;
	}

	// Advance on every attempt. Otherwise a failed navigation query retries the
	// exact same random points forever.
	const int32 LocalSeed = HashCombine(GetTypeHash(RandomSeed), GetTypeHash(SpawnSequence++));
	FRandomStream Stream(LocalSeed);
	int32 EnemyLevel = 1;
	TSubclassOf<AAuraEnemy> EnemyClass = PickEnemy(Stream, EnemyLevel);
	FVector SpawnLocation;
	if (!EnemyClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("Aura: spawn volume %s has no valid weighted enemy entry."), *GetName());
		return nullptr;
	}
	if (!FindSpawnLocation(Stream, EnemyClass, SpawnLocation))
	{
		UE_LOG(LogTemp, Warning, TEXT("Aura: spawn volume %s found no valid spawn point. Check ground collision, slope, NavMesh, player distance, and capsule clearance."), *GetName());
		return nullptr;
	}

	UAuraEnemyPoolSubsystem* Pool = GetWorld()->GetSubsystem<UAuraEnemyPoolSubsystem>();
	AAuraEnemy* Enemy = Pool ? Pool->AcquireEnemy(EnemyClass, FTransform(FRotator::ZeroRotator, SpawnLocation)) : nullptr;
	if (!Enemy)
	{
		UE_LOG(LogTemp, Warning, TEXT("Aura: spawn volume %s could not acquire enemy class %s from the pool."),
			*GetName(), *GetNameSafe(EnemyClass));
		return nullptr;
	}

	Enemy->SetPoolLevel(EnemyLevel);
	Enemy->SetPoolIdentity(FGuid::NewGuid(), SpawnerId);
	Enemy->OnEnemyDyingDelegate.AddUObject(this, &AAuraEnemySpawnVolume::OnEnemyDying);
	ActiveEnemies.Add(Enemy);
	return Enemy;
}

TSubclassOf<AAuraEnemy> AAuraEnemySpawnVolume::PickEnemy(FRandomStream& Stream, int32& OutLevel) const
{
	float TotalWeight = 0.f;
	for (const FEnemySpawnEntry& Entry : EnemyPool)
	{
		if (Entry.EnemyClass && Entry.Weight > 0.f)
		{
			TotalWeight += Entry.Weight;
		}
	}
	if (TotalWeight <= 0.f)
	{
		return nullptr;
	}

	float Roll = Stream.FRandRange(0.f, TotalWeight);
	for (const FEnemySpawnEntry& Entry : EnemyPool)
	{
		if (!Entry.EnemyClass || Entry.Weight <= 0.f)
		{
			continue;
		}
		Roll -= Entry.Weight;
		if (Roll <= 0.f)
		{
			OutLevel = Stream.RandRange(FMath::Max(1, Entry.MinLevel), FMath::Max(Entry.MinLevel, Entry.MaxLevel));
			return Entry.EnemyClass;
		}
	}
	return nullptr;
}

bool AAuraEnemySpawnVolume::FindSpawnLocation(FRandomStream& Stream, TSubclassOf<AAuraEnemy> EnemyClass, FVector& OutLocation) const
{
	if (!SpawnBounds || !EnemyClass)
	{
		return false;
	}

	const FVector UnscaledExtent = SpawnBounds->GetUnscaledBoxExtent();
	const FTransform BoundsTransform = SpawnBounds->GetComponentTransform();
	for (int32 Attempt = 0; Attempt < MaxSpawnAttempts; ++Attempt)
	{
		const FVector LocalCandidate(
			Stream.FRandRange(-UnscaledExtent.X, UnscaledExtent.X),
			Stream.FRandRange(-UnscaledExtent.Y, UnscaledExtent.Y),
			0.f);
		const FVector Candidate = BoundsTransform.TransformPosition(LocalCandidate);
		if (ResolveSpawnLocationAtXY(Candidate, EnemyClass, OutLocation))
		{
			return true;
		}
	}
	return false;
}

bool AAuraEnemySpawnVolume::ResolveSpawnLocationAtXY(const FVector& SampleLocation,
	TSubclassOf<AAuraEnemy> EnemyClass, FVector& OutLocation) const
{
	if (!SpawnBounds || !EnemyClass || !GetWorld())
	{
		return false;
	}

	UNavigationSystemV1* NavigationSystem = UNavigationSystemV1::GetCurrent(GetWorld());
	const AAuraEnemy* EnemyDefaults = EnemyClass->GetDefaultObject<AAuraEnemy>();
	const UCapsuleComponent* Capsule = EnemyDefaults ? EnemyDefaults->GetCapsuleComponent() : nullptr;
	if (!NavigationSystem || !Capsule)
	{
		return false;
	}

	const float CapsuleRadius = Capsule->GetUnscaledCapsuleRadius();
	const float CapsuleHalfHeight = Capsule->GetUnscaledCapsuleHalfHeight();
	const FBoxSphereBounds Bounds = SpawnBounds->Bounds;
	const FVector TraceStart(SampleLocation.X, SampleLocation.Y, Bounds.Origin.Z + Bounds.BoxExtent.Z + GroundTracePadding);
	const FVector TraceEnd(SampleLocation.X, SampleLocation.Y, Bounds.Origin.Z - Bounds.BoxExtent.Z - GroundTracePadding);

	FCollisionObjectQueryParams GroundObjects;
	GroundObjects.AddObjectTypesToQuery(ECC_WorldStatic);
	GroundObjects.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(AuraEnemySpawnGround), false, this);
	TArray<FHitResult> GroundHits;
	if (!GetWorld()->LineTraceMultiByObjectType(GroundHits, TraceStart, TraceEnd, GroundObjects, TraceParams))
	{
		return false;
	}
	// Prefer the lowest valid surface. This prevents an upper floor or an
	// elevated building roof with NavMesh from winning over the actual ground.
	GroundHits.Sort([](const FHitResult& A, const FHitResult& B)
	{
		return A.ImpactPoint.Z < B.ImpactPoint.Z;
	});

	const float MinimumGroundNormalZ = FMath::Cos(FMath::DegreesToRadians(MaxGroundSlopeDegrees));
	const FVector UnscaledExtent = SpawnBounds->GetUnscaledBoxExtent();
	const FTransform BoundsTransform = SpawnBounds->GetComponentTransform();
	const FVector NavQueryExtent(FMath::Max(50.f, CapsuleRadius), FMath::Max(50.f, CapsuleRadius), 100.f);
	for (const FHitResult& GroundHit : GroundHits)
	{
		if (!GroundHit.bBlockingHit || GroundHit.ImpactNormal.Z < MinimumGroundNormalZ)
		{
			continue;
		}

		FNavLocation Projected;
		if (!NavigationSystem->ProjectPointToNavigation(GroundHit.ImpactPoint, Projected, NavQueryExtent))
		{
			continue;
		}
		if (FVector::DistSquared2D(GroundHit.ImpactPoint, Projected.Location) > FMath::Square(NavQueryExtent.X)
			|| FMath::Abs(GroundHit.ImpactPoint.Z - Projected.Location.Z) > NavQueryExtent.Z)
		{
			continue;
		}

		const FVector LocalNavLocation = BoundsTransform.InverseTransformPosition(Projected.Location);
		if (FMath::Abs(LocalNavLocation.X) > UnscaledExtent.X
			|| FMath::Abs(LocalNavLocation.Y) > UnscaledExtent.Y
			|| FMath::Abs(LocalNavLocation.Z) > UnscaledExtent.Z)
		{
			continue;
		}

		bool bTooCloseToPlayer = false;
		const int32 PlayerCount = UGameplayStatics::GetNumPlayerControllers(GetWorld());
		for (int32 PlayerIndex = 0; PlayerIndex < PlayerCount; ++PlayerIndex)
		{
			if (const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), PlayerIndex))
			{
				if (FVector::DistSquared2D(PlayerPawn->GetActorLocation(), Projected.Location) < FMath::Square(MinDistanceToPlayer))
				{
					bTooCloseToPlayer = true;
					break;
				}
			}
		}
		if (bTooCloseToPlayer)
		{
			continue;
		}

		const FVector ActorLocation = Projected.Location + FVector::UpVector * (CapsuleHalfHeight + 2.f);
		const float ClearanceRadius = CapsuleRadius + SpawnCollisionPadding;
		const float ClearanceHalfHeight = FMath::Max(CapsuleHalfHeight - 1.f, ClearanceRadius);
		const FCollisionShape ClearanceShape = FCollisionShape::MakeCapsule(ClearanceRadius, ClearanceHalfHeight);
		FCollisionQueryParams ClearanceParams(SCENE_QUERY_STAT(AuraEnemySpawnClearance), false, this);
		if (GetWorld()->OverlapBlockingTestByChannel(
			ActorLocation, FQuat::Identity, ECC_Pawn, ClearanceShape, ClearanceParams))
		{
			continue;
		}

		OutLocation = ActorLocation;
		return true;
	}

	return false;
}

bool AAuraEnemySpawnVolume::IsSavedSpawnLocationValid(const FTransform& SavedTransform,
	TSubclassOf<AAuraEnemy> EnemyClass) const
{
	if (!SpawnBounds || !EnemyClass || !GetWorld())
	{
		return false;
	}

	UNavigationSystemV1* NavigationSystem = UNavigationSystemV1::GetCurrent(GetWorld());
	const AAuraEnemy* EnemyDefaults = EnemyClass->GetDefaultObject<AAuraEnemy>();
	const UCapsuleComponent* Capsule = EnemyDefaults ? EnemyDefaults->GetCapsuleComponent() : nullptr;
	if (!NavigationSystem || !Capsule)
	{
		return false;
	}

	const FVector UnscaledExtent = SpawnBounds->GetUnscaledBoxExtent();
	const FVector LocalLocation = SpawnBounds->GetComponentTransform().InverseTransformPosition(SavedTransform.GetLocation());
	if (FMath::Abs(LocalLocation.X) > UnscaledExtent.X
		|| FMath::Abs(LocalLocation.Y) > UnscaledExtent.Y
		|| FMath::Abs(LocalLocation.Z) > UnscaledExtent.Z + 250.f)
	{
		return false;
	}

	const float CapsuleRadius = Capsule->GetUnscaledCapsuleRadius();
	const float CapsuleHalfHeight = Capsule->GetUnscaledCapsuleHalfHeight();
	const FVector ExpectedGroundLocation = SavedTransform.GetLocation()
		- FVector::UpVector * (CapsuleHalfHeight + 2.f);
	FNavLocation Projected;
	if (!NavigationSystem->ProjectPointToNavigation(
		ExpectedGroundLocation, Projected, FVector(FMath::Max(50.f, CapsuleRadius), FMath::Max(50.f, CapsuleRadius), 100.f)))
	{
		return false;
	}
	if (FVector::DistSquared2D(ExpectedGroundLocation, Projected.Location) > FMath::Square(75.f)
		|| FMath::Abs(ExpectedGroundLocation.Z - Projected.Location.Z) > 100.f)
	{
		return false;
	}

	const float ClearanceRadius = CapsuleRadius + SpawnCollisionPadding;
	const float ClearanceHalfHeight = FMath::Max(CapsuleHalfHeight - 1.f, ClearanceRadius);
	const FCollisionShape ClearanceShape = FCollisionShape::MakeCapsule(ClearanceRadius, ClearanceHalfHeight);
	FCollisionQueryParams ClearanceParams(SCENE_QUERY_STAT(AuraEnemyRestoreClearance), false, this);
	return !GetWorld()->OverlapBlockingTestByChannel(
		SavedTransform.GetLocation(), SavedTransform.GetRotation(), ECC_Pawn, ClearanceShape, ClearanceParams);
}

void AAuraEnemySpawnVolume::OnEnemyDying(AAuraEnemy* Enemy)
{
	ActiveEnemies.Remove(Enemy);
	TimeUntilNextSpawn = FMath::FRandRange(RespawnDelayMin, RespawnDelayMax);
}

FSpawnerSaveData AAuraEnemySpawnVolume::ExportSaveData() const
{
	FSpawnerSaveData Data;
	Data.SpawnerId = SpawnerId;
	Data.RandomSeed = RandomSeed;
	Data.SpawnSequence = SpawnSequence;
	Data.TimeUntilNextSpawn = TimeUntilNextSpawn;
	return Data;
}

bool AAuraEnemySpawnVolume::ImportSaveData(const FSpawnerSaveData& Data, const bool bAllowIdMismatch)
{
	if (!bAllowIdMismatch && Data.SpawnerId != SpawnerId)
	{
		return false;
	}
	RandomSeed = Data.RandomSeed;
	SpawnSequence = Data.SpawnSequence;
	TimeUntilNextSpawn = Data.TimeUntilNextSpawn;
	bHasImportedState = true;
	return true;
}

void AAuraEnemySpawnVolume::ExportEnemies(TArray<FEnemySaveData>& OutEnemies) const
{
	for (AAuraEnemy* Enemy : ActiveEnemies)
	{
		if (!IsValid(Enemy) || Enemy->GetPoolState() != EEnemyPoolState::Active)
		{
			continue;
		}
		FEnemySaveData Data;
		Data.SpawnInstanceId = Enemy->GetSpawnInstanceId();
		Data.SpawnerId = SpawnerId;
		Data.EnemyClass = FSoftClassPath(Enemy->GetClass()->GetPathName());
		Data.Transform = Enemy->GetActorTransform();
		Data.Level = Enemy->GetEnemyLevel();
		Data.Health = Enemy->GetCurrentHealth();
		OutEnemies.Add(Data);
	}
}

int32 AAuraEnemySpawnVolume::RestoreEnemies(const TArray<FEnemySaveData>& SavedEnemies, const bool bAllowIdMismatch)
{
	if (!HasAuthority())
	{
		return 0;
	}
	UAuraEnemyPoolSubsystem* Pool = GetWorld()->GetSubsystem<UAuraEnemyPoolSubsystem>();
	if (!Pool)
	{
		return 0;
	}
	int32 RestoredCount = 0;
	for (const FEnemySaveData& Data : SavedEnemies)
	{
		if ((!bAllowIdMismatch && Data.SpawnerId != SpawnerId) || Data.EnemyClass.IsNull())
		{
			continue;
		}
		TSubclassOf<AAuraEnemy> EnemyClass = Data.EnemyClass.TryLoadClass<AAuraEnemy>();
		FTransform RestoreTransform = Data.Transform;
		FVector RestoredLocation;
		if (IsSavedSpawnLocationValid(Data.Transform, EnemyClass))
		{
			// Preserve the saved position and rotation whenever the original
			// location is still navigable and has enough clearance.
			RestoredLocation = Data.Transform.GetLocation();
		}
		else if (!ResolveSpawnLocationAtXY(Data.Transform.GetLocation(), EnemyClass, RestoredLocation))
		{
			FRandomStream RestoreStream(HashCombine(GetTypeHash(RandomSeed), GetTypeHash(Data.SpawnInstanceId)));
			if (!FindSpawnLocation(RestoreStream, EnemyClass, RestoredLocation))
			{
				UE_LOG(LogTemp, Warning, TEXT("Aura: could not restore enemy %s to a valid location in spawn volume %s."),
					*Data.SpawnInstanceId.ToString(), *GetName());
				continue;
			}
		}
		RestoreTransform.SetLocation(RestoredLocation);
		AAuraEnemy* Enemy = Pool->AcquireEnemy(EnemyClass, RestoreTransform);
		if (!Enemy)
		{
			continue;
		}
		Enemy->SetPoolLevel(Data.Level);
		Enemy->SetPoolIdentity(Data.SpawnInstanceId, SpawnerId);
		Enemy->RestoreHealth(Data.Health);
		Enemy->OnEnemyDyingDelegate.AddUObject(this, &AAuraEnemySpawnVolume::OnEnemyDying);
		ActiveEnemies.Add(Enemy);
		++RestoredCount;
	}
	return RestoredCount;
}
