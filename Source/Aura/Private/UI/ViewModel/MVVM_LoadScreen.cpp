


#include "UI/ViewModel/MVVM_LoadScreen.h"

#include "Game/AuraGameModeBase.h"
#include "Game/LoadScreenSaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "UI/ViewModel/MVVM_LoadSlot.h"
#include "Game/AuraGameInstance.h"

void UMVVM_LoadScreen::InitializedLoadSlots()
{
	LoadSlot_0 = NewObject<UMVVM_LoadSlot>(this, LoadSlotViewModelClass);
	LoadSlot_0->SetLoadSlotName(FString("LoadSlot_0"));
	LoadSlot_0->SlotIndex = 0;
	LoadSlots.Add(0, LoadSlot_0);
	LoadSlot_1 = NewObject<UMVVM_LoadSlot>(this, LoadSlotViewModelClass);
	LoadSlot_1->SetLoadSlotName(FString("LoadSlot_1"));
	LoadSlot_1->SlotIndex = 1;
	LoadSlots.Add(1, LoadSlot_1);
	LoadSlot_2 = NewObject<UMVVM_LoadSlot>(this, LoadSlotViewModelClass);
	LoadSlot_2->SetLoadSlotName(FString("LoadSlot_2"));
	LoadSlot_2->SlotIndex = 2;
	LoadSlots.Add(2, LoadSlot_2);
	
	SetNumLoadSlots(LoadSlots.Num());	
}

void UMVVM_LoadScreen::LoadData()
{
	AAuraGameModeBase* AuraGameMode = Cast<AAuraGameModeBase>(UGameplayStatics::GetGameMode(this));
	if (!IsValid(AuraGameMode))
	{
		UE_LOG(LogTemp, Warning, TEXT("Aura: cannot load slot data; AuraGameMode is unavailable."));
		return;
	}
	for (const TTuple<int32, UMVVM_LoadSlot*> LoadSlot : LoadSlots)
	{
		ULoadScreenSaveGame* SaveObject = AuraGameMode->GetSaveSlotData(LoadSlot.Value->GetLoadSlotName(), LoadSlot.Key);
		if (!IsValid(SaveObject))
		{
			continue;
		}
		
		const FString PlayerName = SaveObject->PlayerName;
		TEnumAsByte<ESaveSlotStatus> SaveSlotStatus = SaveObject->SaveSlotStatus;
		int32 PlayerLevel = SaveObject->PlayerLevel;
		if (PlayerLevel <= 1 && !SaveObject->MapName.IsEmpty())
		{
			// Backward compatibility for saves created before the slot summary
			// stored the level separately.
			if (const FMapSaveData* MapData = SaveObject->SavedMaps.Find(FName(*SaveObject->MapName)))
			{
				if (MapData->PlayerData.bValid)
				{
					PlayerLevel = MapData->PlayerData.Level;
				}
			}
		}
		
		LoadSlot.Value->SlotStatus = SaveSlotStatus;
		LoadSlot.Value->SetPlayerName(PlayerName);
		LoadSlot.Value->SetPlayerLevel(PlayerLevel);
		LoadSlot.Value->InitializeSlot();
		LoadSlot.Value->SetMapName(SaveObject->MapName);
	}
}

void UMVVM_LoadScreen::SetNumLoadSlots(int32 InNumLoadSlots)
{
	UE_MVVM_SET_PROPERTY_VALUE(NumLoadSlots, InNumLoadSlots);
}

UMVVM_LoadSlot* UMVVM_LoadScreen::GetLoadSlotViewModelByIndex(int32 Index) const
{
	return LoadSlots.FindChecked(Index);
}

void UMVVM_LoadScreen::NewSlotButtonPressed(int32 Slot, const FString& EnteredName)
{
	AAuraGameModeBase* AuraGameMode = Cast<AAuraGameModeBase>(UGameplayStatics::GetGameMode(this));
	
	LoadSlots[Slot]->SetMapName(TEXT("Dungeon"));
	LoadSlots[Slot]->SetPlayerName(EnteredName);
	LoadSlots[Slot]->SetPlayerLevel(1);
	LoadSlots[Slot]->SlotStatus = Taken;
	
	AuraGameMode->SaveSlotData(LoadSlots[Slot], Slot);
	LoadSlots[Slot]->InitializeSlot();
}

void UMVVM_LoadScreen::NewGameButtonPressed(int32 Slot)
{
	LoadSlots[Slot]->SetWidgetSwitcherIndex.Broadcast(1);
}

void UMVVM_LoadScreen::SelectSlotButtonPressed(int32 Slot)
{
	SlotSelected.Broadcast();
	for (const TTuple<int32, UMVVM_LoadSlot*> LoadSlot : LoadSlots)
	{
		if (LoadSlot.Key == Slot)
		{
			LoadSlot.Value->EnableSelectSlotButton.Broadcast(false);
		}
		else
		{
			LoadSlot.Value->EnableSelectSlotButton.Broadcast(true);
		}
	}
	SelectedSlot = LoadSlots[Slot];
}

void UMVVM_LoadScreen::DeleteButtonPressed()
{
	if (IsValid(SelectedSlot))
	{
		AAuraGameModeBase::DeleteSlot(SelectedSlot->GetLoadSlotName(), SelectedSlot->SlotIndex);
		SelectedSlot->SlotStatus = Vacant;
		SelectedSlot->InitializeSlot();
		SelectedSlot->EnableSelectSlotButton.Broadcast(true);
	}
}

void UMVVM_LoadScreen::PlayButtonPressed()
{
	if (!GetWorld())
	{
		UE_LOG(LogTemp, Error, TEXT("Aura: cannot play; load screen has no world."));
		return;
	}

	const ENetMode NetMode = GetWorld()->GetNetMode();
	UE_LOG(LogTemp, Log, TEXT("Aura: PlayButtonPressed NetMode=%d"), static_cast<int32>(NetMode));

	// A network client must never open the level locally. The listen server
	// selects the save slot and travels; connected clients follow that travel.
	if (NetMode == NM_Client)
	{
		UE_LOG(LogTemp, Warning, TEXT("Aura: ignoring client load-screen play request; waiting for server travel."));
		return;
	}

	AAuraGameModeBase* AuraGameMode = Cast<AAuraGameModeBase>(UGameplayStatics::GetGameMode(this));
	if (!IsValid(AuraGameMode))
	{
		UE_LOG(LogTemp, Error, TEXT("Aura: cannot play; no authoritative AuraGameMode exists. Start this instance as the host."));
		return;
	}
	if (IsValid(SelectedSlot))
	{
		if (UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(this))
		{
			if (UAuraGameInstance* AuraGameInstance = Cast<UAuraGameInstance>(GameInstance))
			{
				AuraGameInstance->CurrentSlotName = SelectedSlot->GetLoadSlotName();
				AuraGameInstance->CurrentSlotIndex = SelectedSlot->SlotIndex;
			}
		}
		AuraGameMode->TravelToMap(SelectedSlot);
	}
}
