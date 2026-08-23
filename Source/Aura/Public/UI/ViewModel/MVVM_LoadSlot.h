

#pragma once

#include "CoreMinimal.h"
#include "MVVMViewModelBase.h"
#include "MVVM_LoadSlot.generated.h"

enum ESaveSlotStatus : uint8;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSetWidgetSwitcherIndex, int32, WidgetSwitcherIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEnableSelectSlotButton, bool, bEnable);

/**
 * 
 */
UCLASS()
class AURA_API UMVVM_LoadSlot : public UMVVMViewModelBase
{
	GENERATED_BODY()
	
public:
	void SetPlayerName(FString InPlayerName);
	void SetLoadSlotName(FString InLoadSlotName);
	void SetMapName(FString InMapName);
	void SetPlayerLevel(int32 InPlayerLevel);
	
	FString GetPlayerName() const { return PlayerName; }
	FString GetLoadSlotName() const { return LoadSlotName; }
	FString GetMapName() const { return MapName; }
	FText GetPlayerLevel() const { return PlayerLevel; }
	
	void InitializeSlot();
	
	UPROPERTY(BlueprintAssignable)
	FSetWidgetSwitcherIndex SetWidgetSwitcherIndex;
	
	UPROPERTY(BlueprintAssignable)
	FEnableSelectSlotButton EnableSelectSlotButton;
	
	UPROPERTY()
	int32 SlotIndex;
	
	UPROPERTY()
	TEnumAsByte<ESaveSlotStatus> SlotStatus;
	
	// Field Notifies
	UPROPERTY(EditAnywhere, BlueprintReadWrite, FieldNotify, Setter, Getter)
	FString PlayerName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, FieldNotify, Getter)
	FText PlayerLevel;
	
private:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, FieldNotify, Setter, Getter, meta = (AllowPrivateAccess = "true"))
	FString LoadSlotName;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, FieldNotify, Setter, Getter, meta = (AllowPrivateAccess = "true"))
	FString MapName;
};
