// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "AuraGameModeBase.generated.h"

class ULoadScreenSaveGame;
class USaveGame;
class UMVVM_LoadSlot;
class UAbilityInfo;
class UCharacterClassInfo;
class AAuraCharacter;
struct FMapSaveData;
/**
 * 
 */
UCLASS()
class AURA_API AAuraGameModeBase : public AGameModeBase
{
	GENERATED_BODY()

public:
	void SaveSlotData(UMVVM_LoadSlot* LoadSlot, int32 SlotIndex);
	ULoadScreenSaveGame* GetSaveSlotData(const FString& SlotName, int32 SlotIndex);
	static void DeleteSlot(const FString& SlotName, int32 SlotIndex);
	void TravelToMap(UMVVM_LoadSlot* Slot);

	UFUNCTION(BlueprintCallable)
	bool SaveCurrentWorld();

	/** Saves the authoritative world and returns every connected player to MainMenu. */
	UFUNCTION(BlueprintCallable, Category="Save|Travel")
	void SaveAndReturnToMainMenu();
	void RestoreCurrentWorld();
	void HandlePlayerDeath(AAuraCharacter* DeadCharacter);
	
	UPROPERTY(EditDefaultsOnly, Category="Character Class Defaults")
	TObjectPtr<UCharacterClassInfo> CharacterClassInfo;
	
	UPROPERTY(EditDefaultsOnly, Category="Ability Info")
	TObjectPtr<UAbilityInfo> AbilityInfo;
	
	UPROPERTY(EditDefaultsOnly)
	TSubclassOf<USaveGame> LoadScreenSaveGameClass;
	
	UPROPERTY(EditDefaultsOnly)
	FString DefaultMapName;
	
	UPROPERTY(EditDefaultsOnly)
	TSoftObjectPtr<UWorld> DefaultMap;
	
	UPROPERTY(EditDefaultsOnly)
	TMap<FString, TSoftObjectPtr<UWorld>> Maps;
	
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;

private:
	void ExecutePendingMapTravel();
	void AutoSaveCurrentWorld();
	bool SaveCurrentWorldInternal();
	void EnsureDefaultEnemySpawner();
	void RespawnPlayer(AController* Controller);
	void ScheduleRespawnRetry(AController* Controller);
	FTransform ResolveRespawnTransform(AController* Controller, const APawn* DeadPawn) const;
	bool FindNearestValidRespawnTransform(AController* Controller, const FTransform& DesiredTransform,
		FTransform& OutTransform);
	ULoadScreenSaveGame* GetCurrentWorldSave() const;
	bool bWorldEnemiesRestored = false;
	bool bSkipEndPlaySave = false;
	TSet<int32> RestoredPlayerIndices;
	TMap<TWeakObjectPtr<APlayerController>, int32> PlayerSaveIndices;
	TMap<TWeakObjectPtr<AController>, FTransform> PlayerRespawnTransforms;
	TSet<TWeakObjectPtr<AController>> PendingRespawnControllers;
	TMap<TWeakObjectPtr<AController>, FTimerHandle> PlayerRespawnTimers;
	FTimerHandle PlayerRestoreTimer;
	FTimerHandle AutoSaveTimer;
	FTimerHandle PendingMapTravelTimer;
	FString PendingMapTravelPath;

	UPROPERTY(EditDefaultsOnly, Category="Player Respawn", meta=(ClampMin="0.0"))
	float PlayerRespawnDelay = 3.f;

	UPROPERTY(EditDefaultsOnly, Category="Player Respawn", meta=(ClampMin="0.0", ClampMax="1.0"))
	float RespawnHealthFraction = 0.5f;

	UPROPERTY(EditDefaultsOnly, Category="Player Respawn", meta=(ClampMin="0.0", ClampMax="1.0"))
	float RespawnManaFraction = 0.5f;

	UPROPERTY(EditDefaultsOnly, Category="Player Respawn", meta=(ClampMin="100.0"))
	float RespawnSearchRadius = 1500.f;

	UPROPERTY(EditDefaultsOnly, Category="Player Respawn", meta=(ClampMin="25.0"))
	float RespawnSearchStep = 100.f;

	UPROPERTY(EditDefaultsOnly, Category="Player Respawn", meta=(ClampMin="4", ClampMax="96"))
	int32 RespawnSamplesPerRing = 16;

	UPROPERTY(EditDefaultsOnly, Category="Player Respawn", meta=(ClampMin="0.05"))
	float RespawnRetryDelay = 0.5f;

	UPROPERTY(EditDefaultsOnly, Category="Player Respawn", meta=(ClampMin="0.0"))
	float RespawnCollisionPadding = 5.f;
};
