#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "AuraEnemyPoolSubsystem.generated.h"

class AAuraEnemy;

USTRUCT(BlueprintType)
struct FEnemyPoolClassConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSubclassOf<AAuraEnemy> EnemyClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0"))
	int32 PrewarmCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1"))
	int32 HardPoolLimit = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bAllowPoolExpansion = true;
};

USTRUCT(BlueprintType)
struct FEnemyPoolStats
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	int32 Total = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 Available = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 Active = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 Dying = 0;
};

UCLASS()
class AURA_API UAuraEnemyPoolSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;

	void RegisterPoolConfig(const FEnemyPoolClassConfig& Config);
	void PrewarmPools();

	AAuraEnemy* AcquireEnemy(TSubclassOf<AAuraEnemy> EnemyClass, const FTransform& Transform);
	void ReleaseEnemy(AAuraEnemy* Enemy);
	void NotifyEnemyDying(AAuraEnemy* Enemy);

	FEnemyPoolStats GetPoolStats(TSubclassOf<AAuraEnemy> EnemyClass) const;
	bool IsPoolReady() const { return bPoolReady; }

private:
	struct FPoolBucket
	{
		FEnemyPoolClassConfig Config;
		TArray<TObjectPtr<AAuraEnemy>> All;
		TArray<TObjectPtr<AAuraEnemy>> Available;
		TSet<TObjectPtr<AAuraEnemy>> Active;
		TSet<TObjectPtr<AAuraEnemy>> Dying;
	};

	FPoolBucket* FindBucket(UClass* EnemyClass);
	const FPoolBucket* FindBucket(UClass* EnemyClass) const;
	AAuraEnemy* CreatePooledEnemy(FPoolBucket& Bucket);
	void AddToAvailable(FPoolBucket& Bucket, AAuraEnemy* Enemy);

	TMap<UClass*, FPoolBucket> Buckets;
	bool bPoolReady = false;
};
