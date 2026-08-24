#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "WorldSaveTypes.generated.h"

USTRUCT(BlueprintType)
struct FSavedAbilityData
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadWrite)
	FGameplayTag AbilityTag;

	UPROPERTY(SaveGame, BlueprintReadWrite)
	int32 AbilityLevel = 1;

	UPROPERTY(SaveGame, BlueprintReadWrite)
	FGameplayTag StatusTag;

	UPROPERTY(SaveGame, BlueprintReadWrite)
	FGameplayTag SlotTag;
};

USTRUCT(BlueprintType)
struct FPlayerSaveData
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadWrite)
	bool bValid = false;

	UPROPERTY(SaveGame, BlueprintReadWrite)
	FTransform Transform;

	UPROPERTY(SaveGame, BlueprintReadWrite)
	int32 Level = 1;

	UPROPERTY(SaveGame, BlueprintReadWrite)
	int32 XP = 1;

	UPROPERTY(SaveGame, BlueprintReadWrite)
	int32 AttributePoints = 0;

	UPROPERTY(SaveGame, BlueprintReadWrite)
	int32 SpellPoints = 0;

	UPROPERTY(SaveGame, BlueprintReadWrite)
	float Health = 0.f;

	UPROPERTY(SaveGame, BlueprintReadWrite)
	float Mana = 0.f;

	UPROPERTY(SaveGame, BlueprintReadWrite)
	TArray<FSavedAbilityData> Abilities;
};

USTRUCT(BlueprintType)
struct FSpawnerSaveData
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadWrite)
	FGuid SpawnerId;

	UPROPERTY(SaveGame, BlueprintReadWrite)
	int32 RandomSeed = 0;

	UPROPERTY(SaveGame, BlueprintReadWrite)
	int32 SpawnSequence = 0;

	UPROPERTY(SaveGame, BlueprintReadWrite)
	float TimeUntilNextSpawn = 0.f;

	UPROPERTY(SaveGame, BlueprintReadWrite)
	bool bEnabled = true;
};

USTRUCT(BlueprintType)
struct FEnemySaveData
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadWrite)
	FGuid SpawnInstanceId;

	UPROPERTY(SaveGame, BlueprintReadWrite)
	FGuid SpawnerId;

	UPROPERTY(SaveGame, BlueprintReadWrite)
	FSoftClassPath EnemyClass;

	UPROPERTY(SaveGame, BlueprintReadWrite)
	FTransform Transform;

	UPROPERTY(SaveGame, BlueprintReadWrite)
	int32 Level = 1;

	UPROPERTY(SaveGame, BlueprintReadWrite)
	float Health = 0.f;
};

USTRUCT(BlueprintType)
struct FMapSaveData
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadWrite)
	FName MapAssetName;

	UPROPERTY(SaveGame, BlueprintReadWrite)
	FPlayerSaveData PlayerData;

	/** Per-session player records. Index 0 is the listen-server player. */
	UPROPERTY(SaveGame, BlueprintReadWrite)
	TArray<FPlayerSaveData> Players;

	UPROPERTY(SaveGame, BlueprintReadWrite)
	TArray<FSpawnerSaveData> Spawners;

	UPROPERTY(SaveGame, BlueprintReadWrite)
	TArray<FEnemySaveData> Enemies;
};
