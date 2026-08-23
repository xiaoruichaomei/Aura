// Fill out your copyright notice in the Description page of Project Settings.


#include "Game/AuraGameModeBase.h"

#include "Game/LoadScreenSaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "UI/ViewModel/MVVM_LoadSlot.h"
#include "Actor/AuraEnemySpawnVolume.h"
#include "Character/AuraEnemy.h"
#include "Game/AuraGameInstance.h"
#include "Player/AuraPlayerState.h"
#include "Character/AuraCharacter.h"
#include "Components/BoxComponent.h"
#include "AbilitySystem/AuraAttributeSet.h"
#include "AbilitySystem/AuraAbilitySystemComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "UObject/SoftObjectPath.h"

namespace AuraSaveConstants
{
	static const FString DungeonMapName = TEXT("Dungeon");
	static const TCHAR* DungeonMapPath = TEXT("/Game/Maps/Dungeon.Dungeon");
	static const FGuid DefaultDungeonSpawnerId(0xA17A0001, 0xD06E0001, 0x00000000, 0x00000001);

	FString GetCanonicalMapName(const UObject* WorldContextObject)
	{
		return UGameplayStatics::GetCurrentLevelName(WorldContextObject, true);
	}

	bool IsMenuMap(const FString& MapName)
	{
		return MapName.Contains(TEXT("MainMenu")) || MapName.Contains(TEXT("LoadMenu"));
	}
}

void AAuraGameModeBase::SaveSlotData(UMVVM_LoadSlot* LoadSlot, int32 SlotIndex)
{
	if (UGameplayStatics::DoesSaveGameExist(LoadSlot->GetLoadSlotName(), SlotIndex))
	{
		UGameplayStatics::DeleteGameInSlot(LoadSlot->GetLoadSlotName(), SlotIndex);
	}
	USaveGame* SaveGameObject = UGameplayStatics::CreateSaveGameObject(LoadScreenSaveGameClass);
	ULoadScreenSaveGame* LoadScreenSaveGame = Cast<ULoadScreenSaveGame>(SaveGameObject);
	LoadScreenSaveGame->PlayerName = LoadSlot->PlayerName;
	LoadScreenSaveGame->SlotName = LoadSlot->GetLoadSlotName();
	LoadScreenSaveGame->SlotIndex = SlotIndex;
	LoadScreenSaveGame->SaveSlotStatus = Taken;
	LoadScreenSaveGame->MapName = AuraSaveConstants::DungeonMapName;
	LoadScreenSaveGame->PlayerLevel = 1;
	
	UGameplayStatics::SaveGameToSlot(LoadScreenSaveGame, LoadSlot->GetLoadSlotName(), SlotIndex);
}

ULoadScreenSaveGame* AAuraGameModeBase::GetSaveSlotData(const FString& SlotName, int32 SlotIndex)
{
	USaveGame* SaveGameObject = nullptr;
	if (UGameplayStatics::DoesSaveGameExist(SlotName, SlotIndex))
	{
		SaveGameObject = UGameplayStatics::LoadGameFromSlot(SlotName, SlotIndex);
	}
	else
	{
		SaveGameObject = UGameplayStatics::CreateSaveGameObject(LoadScreenSaveGameClass);
	}
	ULoadScreenSaveGame* LoadScreenSaveGame = Cast<ULoadScreenSaveGame>(SaveGameObject);
	if (LoadScreenSaveGame && LoadScreenSaveGame->SaveSlotStatus == Taken &&
		LoadScreenSaveGame->MapName != AuraSaveConstants::DungeonMapName)
	{
		LoadScreenSaveGame->MapName = AuraSaveConstants::DungeonMapName;
		UGameplayStatics::SaveGameToSlot(LoadScreenSaveGame, SlotName, SlotIndex);
		UE_LOG(LogTemp, Log, TEXT("Aura: migrated slot '%s' map name to Dungeon."), *SlotName);
	}
	return LoadScreenSaveGame;
}

void AAuraGameModeBase::DeleteSlot(const FString& SlotName, int32 SlotIndex)
{
	if (UGameplayStatics::DoesSaveGameExist(SlotName, SlotIndex))
	{
		UGameplayStatics::DeleteGameInSlot(SlotName, SlotIndex);
	}
}

void AAuraGameModeBase::TravelToMap(UMVVM_LoadSlot* Slot)
{
	if (!IsValid(Slot))
	{
		UE_LOG(LogTemp, Error, TEXT("Aura: cannot travel because the selected load slot is invalid."));
		return;
	}

	const FString SlotName = Slot->GetLoadSlotName();
	const int32 SlotIndex = Slot->SlotIndex;
	if (UAuraGameInstance* AuraGameInstance = Cast<UAuraGameInstance>(GetGameInstance()))
	{
		AuraGameInstance->CurrentSlotName = SlotName;
		AuraGameInstance->CurrentSlotIndex = SlotIndex;
	}
	const FString RequestedMapName = Slot->GetMapName();
	TSoftObjectPtr<UWorld>* MapAsset = Maps.Find(RequestedMapName);

	// Saved map names can contain a PIE prefix or an older display name.
	if (!MapAsset)
	{
		FString NormalizedMapName = RequestedMapName;
		if (NormalizedMapName.StartsWith(TEXT("UEDPIE_")))
		{
			const int32 PrefixEnd = NormalizedMapName.Find(TEXT("_"), ESearchCase::IgnoreCase, ESearchDir::FromStart, 7);
			if (PrefixEnd != INDEX_NONE)
			{
				NormalizedMapName = NormalizedMapName.RightChop(PrefixEnd + 1);
			}
		}
		MapAsset = Maps.Find(NormalizedMapName);
	}
	if (!MapAsset && !DefaultMapName.IsEmpty())
	{
		MapAsset = Maps.Find(DefaultMapName);
	}
	if (!MapAsset)
	{
		MapAsset = Maps.Find(AuraSaveConstants::DungeonMapName);
	}
	if (!MapAsset && DefaultMap.IsValid())
	{
		MapAsset = &DefaultMap;
	}

	if (!MapAsset || MapAsset->IsNull())
	{
		UE_LOG(LogTemp, Error, TEXT("Aura: no map configured for load slot '%s'. Requested='%s', Default='%s'."),
			*SlotName, *RequestedMapName, *DefaultMapName);
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("Aura: travelling to map '%s' for slot '%s'."),
		*MapAsset->ToSoftObjectPath().ToString(), *SlotName);
	UGameplayStatics::OpenLevelBySoftObjectPtr(this, *MapAsset);
}

void AAuraGameModeBase::BeginPlay()
{
	Super::BeginPlay();
	if (!DefaultMapName.IsEmpty() && !DefaultMap.IsNull())
	{
		Maps.Add(DefaultMapName, DefaultMap);
	}
	if (!Maps.Contains(AuraSaveConstants::DungeonMapName))
	{
		Maps.Add(AuraSaveConstants::DungeonMapName,
			TSoftObjectPtr<UWorld>(FSoftObjectPath(AuraSaveConstants::DungeonMapPath)));
	}
	EnsureDefaultEnemySpawner();
	RestoreCurrentWorld();
	for (TActorIterator<AAuraEnemySpawnVolume> It(GetWorld()); It; ++It)
	{
		It->StartSpawning();
	}
	if (!AuraSaveConstants::IsMenuMap(AuraSaveConstants::GetCanonicalMapName(this)))
	{
		GetWorldTimerManager().SetTimer(AutoSaveTimer, this,
			&AAuraGameModeBase::AutoSaveCurrentWorld, 10.f, true, 2.f);
	}
}

void AAuraGameModeBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// EndPlay is reached when travelling away from the current map and when
	// the game is closed, so it is the final reliable save point for the map.
	const FString CurrentMapName = AuraSaveConstants::GetCanonicalMapName(this);
	if (HasAuthority() && !AuraSaveConstants::IsMenuMap(CurrentMapName))
	{
		const bool bSaved = SaveCurrentWorld();
		UE_LOG(LogTemp, Log, TEXT("Aura: saved world on EndPlay (%s): %s"),
			*UEnum::GetValueAsString(EndPlayReason), bSaved ? TEXT("Success") : TEXT("Failed"));
	}

	Super::EndPlay(EndPlayReason);
}

ULoadScreenSaveGame* AAuraGameModeBase::GetCurrentWorldSave() const
{
	const UAuraGameInstance* AuraGameInstance = Cast<UAuraGameInstance>(GetGameInstance());
	if (!AuraGameInstance || AuraGameInstance->CurrentSlotName.IsEmpty() || AuraGameInstance->CurrentSlotIndex < 0)
	{
		return nullptr;
	}
	return const_cast<AAuraGameModeBase*>(this)->GetSaveSlotData(AuraGameInstance->CurrentSlotName, AuraGameInstance->CurrentSlotIndex);
}

void AAuraGameModeBase::AutoSaveCurrentWorld()
{
	SaveCurrentWorld();
}

bool AAuraGameModeBase::SaveCurrentWorld()
{
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("Aura: cannot save world without authority."));
		return false;
	}
	const FString CurrentMapName = AuraSaveConstants::GetCanonicalMapName(this);
	if (AuraSaveConstants::IsMenuMap(CurrentMapName))
	{
		UE_LOG(LogTemp, Verbose, TEXT("Aura: skipped world save for menu map '%s'."), *CurrentMapName);
		return false;
	}
	UAuraGameInstance* AuraGameInstance = Cast<UAuraGameInstance>(GetGameInstance());
	ULoadScreenSaveGame* SaveGame = GetCurrentWorldSave();
	if (!AuraGameInstance || !SaveGame)
	{
		UE_LOG(LogTemp, Warning, TEXT("Aura: cannot save world; slot or SaveGame object is invalid."));
		return false;
	}

	const FName CurrentMapKey(*CurrentMapName);
	FMapSaveData MapData;
	if (const FMapSaveData* ExistingMapData = SaveGame->SavedMaps.Find(CurrentMapKey))
	{
		MapData = *ExistingMapData;
	}
	MapData.MapAssetName = CurrentMapKey;
	if (MapData.PlayerData.bValid)
	{
		SaveGame->PlayerLevel = MapData.PlayerData.Level;
	}
	const FName SavedMapName = MapData.MapAssetName;
	bool bSavedPlayer = false;
	FVector SavedPlayerLocation = FVector::ZeroVector;
	if (AAuraCharacter* Player = Cast<AAuraCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0)))
	{
		if (AAuraPlayerState* PlayerState = Player->GetPlayerState<AAuraPlayerState>())
		{
			FPlayerSaveData& PlayerData = MapData.PlayerData;
			PlayerData.bValid = true;
			PlayerData.Transform = Player->GetActorTransform();
			bSavedPlayer = true;
			SavedPlayerLocation = PlayerData.Transform.GetLocation();
			PlayerData.Level = PlayerState->GetPlayerLevel();
			PlayerData.XP = PlayerState->GetXP();
			PlayerData.AttributePoints = PlayerState->GetAttributePoints();
			PlayerData.SpellPoints = PlayerState->GetSpellPoints();
			if (UAuraAbilitySystemComponent* AuraASC = Cast<UAuraAbilitySystemComponent>(PlayerState->GetAbilitySystemComponent()))
			{
				AuraASC->ExportSavedAbilities(PlayerData.Abilities);
			}
			if (const UAuraAttributeSet* Attributes = Cast<UAuraAttributeSet>(PlayerState->GetAttributeSet()))
			{
				PlayerData.Health = Attributes->GetHealth();
				PlayerData.Mana = Attributes->GetMana();
			}
		}
	}

	TArray<AAuraEnemySpawnVolume*> SpawnVolumes;
	for (TActorIterator<AAuraEnemySpawnVolume> It(GetWorld()); It; ++It)
	{
		SpawnVolumes.Add(*It);
	}
	if (SpawnVolumes.Num() > 0)
	{
		MapData.Spawners.Reset();
		MapData.Enemies.Reset();
		for (AAuraEnemySpawnVolume* SpawnVolume : SpawnVolumes)
		{
			MapData.Spawners.Add(SpawnVolume->ExportSaveData());
			SpawnVolume->ExportEnemies(MapData.Enemies);
		}
	}
	SaveGame->SaveVersion = 1;
	SaveGame->MapName = CurrentMapName;
	const int32 SavedSpawnerCount = MapData.Spawners.Num();
	const int32 SavedEnemyCount = MapData.Enemies.Num();
	SaveGame->SavedMaps.Add(MapData.MapAssetName, MoveTemp(MapData));
	const bool bSaved = UGameplayStatics::SaveGameToSlot(SaveGame, AuraGameInstance->CurrentSlotName, AuraGameInstance->CurrentSlotIndex);
	UE_LOG(LogTemp, Log, TEXT("Aura: SaveCurrentWorld slot=%s index=%d map=%s player=%s location=%s spawners=%d enemies=%d result=%s"),
		*AuraGameInstance->CurrentSlotName,
		AuraGameInstance->CurrentSlotIndex,
		*SavedMapName.ToString(),
		bSavedPlayer ? TEXT("Yes") : TEXT("No"),
		*SavedPlayerLocation.ToCompactString(),
		SavedSpawnerCount,
		SavedEnemyCount,
		bSaved ? TEXT("Success") : TEXT("Failed"));
	return bSaved;
}

void AAuraGameModeBase::EnsureDefaultEnemySpawner()
{
	if (!GetWorld() || AuraSaveConstants::IsMenuMap(AuraSaveConstants::GetCanonicalMapName(this)))
	{
		return;
	}

	for (TActorIterator<AAuraEnemySpawnVolume> It(GetWorld()); It; ++It)
	{
		return;
	}

	TSubclassOf<AAuraEnemy> EnemyClass = LoadClass<AAuraEnemy>(nullptr, TEXT("/Game/Blueprints/Character/Enemy/Goblin/Spear/BP_Goblin_Spear.BP_Goblin_Spear_C"));
	if (!EnemyClass)
	{
		return;
	}
	FActorSpawnParameters Params;
	AAuraEnemySpawnVolume* Volume = GetWorld()->SpawnActorDeferred<AAuraEnemySpawnVolume>(AAuraEnemySpawnVolume::StaticClass(), FTransform::Identity, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Volume)
	{
		Volume->SpawnerId = AuraSaveConstants::DefaultDungeonSpawnerId;
		Volume->SpawnBounds->SetBoxExtent(FVector(1400.f, 1400.f, 100.f));
		FEnemySpawnEntry Entry;
		Entry.EnemyClass = EnemyClass;
		Entry.Weight = 1.f;
		Entry.MinLevel = 1;
		Entry.MaxLevel = 3;
		Volume->EnemyPool.Add(Entry);
		Volume->FinishSpawning(FTransform::Identity);
	}
}

void AAuraGameModeBase::RestoreCurrentWorld()
{
	if (!HasAuthority())
	{
		return;
	}
	ULoadScreenSaveGame* SaveGame = GetCurrentWorldSave();
	if (!SaveGame)
	{
		return;
	}
	const FString CurrentMapName = AuraSaveConstants::GetCanonicalMapName(this);
	if (AuraSaveConstants::IsMenuMap(CurrentMapName))
	{
		return;
	}
	const FMapSaveData* MapData = SaveGame->SavedMaps.Find(FName(*CurrentMapName));
	if (!MapData)
	{
		// Compatibility with saves created before PIE prefixes were stripped.
		for (const TPair<FName, FMapSaveData>& SavedMap : SaveGame->SavedMaps)
		{
			FString LegacyMapName = SavedMap.Key.ToString();
			if (LegacyMapName.EndsWith(CurrentMapName))
			{
				MapData = &SavedMap.Value;
				break;
			}
		}
	}
	if (!MapData)
	{
		UE_LOG(LogTemp, Log, TEXT("Aura: no saved world state found for map '%s'."), *CurrentMapName);
		if (AAuraCharacter* Player = Cast<AAuraCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0)))
		{
			if (AAuraPlayerState* PlayerState = Player->GetPlayerState<AAuraPlayerState>())
			{
				PlayerState->SetSaveRestoreInProgress(false);
			}
		}
		return;
	}

	AAuraCharacter* ExistingPlayer = Cast<AAuraCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0));
	if (ExistingPlayer)
	{
		if (AAuraPlayerState* PlayerState = ExistingPlayer->GetPlayerState<AAuraPlayerState>())
		{
			PlayerState->SetSaveRestoreInProgress(true);
		}
	}
	UE_LOG(LogTemp, Log, TEXT("Aura: restoring world state for map '%s'."), *CurrentMapName);
	if (!bWorldEnemiesRestored)
	{
		TArray<AAuraEnemySpawnVolume*> SpawnVolumes;
		for (TActorIterator<AAuraEnemySpawnVolume> It(GetWorld()); It; ++It)
		{
			SpawnVolumes.Add(*It);
		}

		const bool bSingleSpawnerCompatibility = SpawnVolumes.Num() == 1 && MapData->Spawners.Num() == 1;
		int32 RestoredEnemyCount = 0;
		for (AAuraEnemySpawnVolume* SpawnVolume : SpawnVolumes)
		{
			bool bImportedSpawner = false;
			for (const FSpawnerSaveData& SpawnerData : MapData->Spawners)
			{
				if (SpawnVolume->ImportSaveData(SpawnerData))
				{
					bImportedSpawner = true;
					break;
				}
			}
			if (!bImportedSpawner && bSingleSpawnerCompatibility)
			{
				bImportedSpawner = SpawnVolume->ImportSaveData(MapData->Spawners[0], true);
			}
			RestoredEnemyCount += SpawnVolume->RestoreEnemies(MapData->Enemies, bSingleSpawnerCompatibility);
		}
		UE_LOG(LogTemp, Log, TEXT("Aura: restored spawners=%d/%d enemies=%d/%d."),
			SpawnVolumes.Num(), MapData->Spawners.Num(), RestoredEnemyCount, MapData->Enemies.Num());
		bWorldEnemiesRestored = true;
	}
	if (MapData->PlayerData.bValid)
	{
		AAuraCharacter* Player = Cast<AAuraCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0));
		if (!Player)
		{
			GetWorldTimerManager().SetTimer(PlayerRestoreTimer, this, &AAuraGameModeBase::RestoreCurrentWorld, 0.2f, false);
			return;
		}
		if (Player)
		{
			Player->SetActorTransform(MapData->PlayerData.Transform);
			AAuraPlayerState* PlayerState = Player->GetPlayerState<AAuraPlayerState>();
			if (!PlayerState)
			{
				GetWorldTimerManager().SetTimer(PlayerRestoreTimer, this, &AAuraGameModeBase::RestoreCurrentWorld, 0.2f, false);
				return;
			}
			if (PlayerState)
			{
				PlayerState->SetLevel(MapData->PlayerData.Level);
				Player->RefreshAttributesAfterLoading();
				PlayerState->SetXP(MapData->PlayerData.XP);
				PlayerState->SetAttributePoints(MapData->PlayerData.AttributePoints);
				PlayerState->SetSpellPoints(MapData->PlayerData.SpellPoints);
				UAuraAbilitySystemComponent* AuraASC = Cast<UAuraAbilitySystemComponent>(PlayerState->GetAbilitySystemComponent());
				if (AuraASC)
				{
					AuraASC->RestoreSavedAbilities(MapData->PlayerData.Abilities);
				}
				if (AuraASC)
				{
					if (UAuraAttributeSet* Attributes = Cast<UAuraAttributeSet>(PlayerState->GetAttributeSet()))
					{
						AuraASC->SetNumericAttributeBase(UAuraAttributeSet::GetHealthAttribute(), FMath::Clamp(MapData->PlayerData.Health, 0.f, Attributes->GetMaxHealth()));
						AuraASC->SetNumericAttributeBase(UAuraAttributeSet::GetManaAttribute(), FMath::Clamp(MapData->PlayerData.Mana, 0.f, Attributes->GetMaxMana()));
					}
				}
				UE_LOG(LogTemp, Log, TEXT("Aura: restored player location=%s level=%d xp=%d health=%.1f mana=%.1f abilities=%d."),
					*MapData->PlayerData.Transform.GetLocation().ToCompactString(),
					MapData->PlayerData.Level,
					MapData->PlayerData.XP,
					MapData->PlayerData.Health,
					MapData->PlayerData.Mana,
					MapData->PlayerData.Abilities.Num());
				PlayerState->SetSaveRestoreInProgress(false);
			}
		}
	}
}
