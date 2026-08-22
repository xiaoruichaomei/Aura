#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "AuraGameInstance.generated.h"

UCLASS()
class AURA_API UAuraGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, SaveGame)
	FString CurrentSlotName;

	UPROPERTY(BlueprintReadWrite, SaveGame)
	int32 CurrentSlotIndex = -1;
};
