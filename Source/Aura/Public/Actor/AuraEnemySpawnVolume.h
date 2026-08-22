#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystem/AuraEnemyPoolSubsystem.h"
#include "Game/WorldSaveTypes.h"
#include "AuraEnemySpawnVolume.generated.h"

class AAuraEnemy;
class UBoxComponent;

USTRUCT(BlueprintType)
struct FEnemySpawnEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSubclassOf<AAuraEnemy> EnemyClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.01"))
	float Weight = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1"))
	int32 MinLevel = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1"))
	int32 MaxLevel = 1;
};

UCLASS()
class AURA_API AAuraEnemySpawnVolume : public AActor
{
	GENERATED_BODY()

public:
	AAuraEnemySpawnVolume();
	virtual void BeginPlay() override;

	void StartSpawning();
	void StopSpawning();
	FSpawnerSaveData ExportSaveData() const;
	bool ImportSaveData(const FSpawnerSaveData& Data, bool bAllowIdMismatch = false);
	void ExportEnemies(TArray<FEnemySaveData>& OutEnemies) const;
	int32 RestoreEnemies(const TArray<FEnemySaveData>& SavedEnemies, bool bAllowIdMismatch = false);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawning")
	FGuid SpawnerId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawning")
	TArray<FEnemySpawnEntry> EnemyPool;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawning", meta=(ClampMin="0"))
	int32 InitialSpawnCount = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawning", meta=(ClampMin="1"))
	int32 MaxAliveEnemies = 5;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawning", meta=(ClampMin="0"))
	float RespawnDelayMin = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawning", meta=(ClampMin="0"))
	float RespawnDelayMax = 15.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawning", meta=(ClampMin="0"))
	float MinDistanceToPlayer = 900.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawning", meta=(ClampMin="1"))
	int32 MaxSpawnAttempts = 20;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawning|Placement", meta=(ClampMin="0", ClampMax="89"))
	float MaxGroundSlopeDegrees = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawning|Placement", meta=(ClampMin="0"))
	float SpawnCollisionPadding = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawning|Placement", meta=(ClampMin="0"))
	float GroundTracePadding = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawning|Random")
	bool bUseFixedRandomSeed = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawning|Random", meta=(EditCondition="bUseFixedRandomSeed"))
	int32 RandomSeed = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Spawning")
	TObjectPtr<UBoxComponent> SpawnBounds;

private:
	void SpawnTick();
	AAuraEnemy* SpawnOne();
	TSubclassOf<AAuraEnemy> PickEnemy(FRandomStream& Stream, int32& OutLevel) const;
	bool FindSpawnLocation(FRandomStream& Stream, TSubclassOf<AAuraEnemy> EnemyClass, FVector& OutLocation) const;
	bool ResolveSpawnLocationAtXY(const FVector& SampleLocation, TSubclassOf<AAuraEnemy> EnemyClass, FVector& OutLocation) const;
	void OnEnemyDying(AAuraEnemy* Enemy);

	TArray<TObjectPtr<AAuraEnemy>> ActiveEnemies;
	FTimerHandle SpawnTimer;
	int32 SpawnSequence = 0;
	float TimeUntilNextSpawn = 0.f;
	bool bHasImportedState = false;
	bool bSpawnSessionInitialized = false;
};
