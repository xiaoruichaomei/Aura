#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "AuraProjectilePoolSubsystem.generated.h"

class AAuraProjectile;
class AAuraFireBall;

UCLASS()
class AURA_API UAuraProjectilePoolSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;

	void PrewarmProjectiles(TSubclassOf<AAuraProjectile> ProjectileClass, int32 Count, int32 HardLimit = 64);
	AAuraProjectile* AcquireProjectile(TSubclassOf<AAuraProjectile> ProjectileClass, const FTransform& Transform,
		AActor* Owner, APawn* Instigator);
	void ReleaseProjectile(AAuraProjectile* Projectile);

	void PrewarmFireBalls(TSubclassOf<AAuraFireBall> FireBallClass, int32 Count, int32 HardLimit = 32);
	AAuraFireBall* AcquireFireBall(TSubclassOf<AAuraFireBall> FireBallClass, const FTransform& Transform,
		AActor* Owner, APawn* Instigator);
	void ReleaseFireBall(AAuraFireBall* FireBall);

private:
	template <typename T>
	struct TPoolBucket
	{
		TArray<TObjectPtr<T>> All;
		TArray<TObjectPtr<T>> Available;
		TSet<TObjectPtr<T>> Active;
		int32 HardLimit = 1;
	};

	AAuraProjectile* CreateProjectile(UClass* ProjectileClass, TPoolBucket<AAuraProjectile>& Bucket);
	AAuraFireBall* CreateFireBall(UClass* FireBallClass, TPoolBucket<AAuraFireBall>& Bucket);

	TMap<UClass*, TPoolBucket<AAuraProjectile>> ProjectileBuckets;
	TMap<UClass*, TPoolBucket<AAuraFireBall>> FireBallBuckets;
	bool bIsShuttingDown = false;
};
