

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Game/WorldSaveTypes.h"
#include "LoadScreenSaveGame.generated.h"

UENUM(BlueprintType)
enum ESaveSlotStatus : uint8
{
	Vacant,
	EnterName,
	Taken
};


/**
 * 
 */
UCLASS()
class AURA_API ULoadScreenSaveGame : public USaveGame
{
	GENERATED_BODY()
	
public:
	UPROPERTY(SaveGame)
	int32 SaveVersion = 3;

	UPROPERTY()
	FString SlotName = FString();
	
	UPROPERTY()
	int32 SlotIndex = 0;
	
	UPROPERTY()
	FString PlayerName = FString("Default Name");
	
	UPROPERTY()
	FString MapName = FString("Default Map Name");

	UPROPERTY(SaveGame, BlueprintReadWrite)
	int32 PlayerLevel = 1;
	
	UPROPERTY()
	TEnumAsByte<ESaveSlotStatus> SaveSlotStatus = Vacant;

	UPROPERTY(SaveGame)
	TMap<FName, FMapSaveData> SavedMaps;
};
