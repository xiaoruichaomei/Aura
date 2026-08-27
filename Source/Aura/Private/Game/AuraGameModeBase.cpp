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
#include "Components/CapsuleComponent.h"
#include "AbilitySystem/AuraAttributeSet.h"
#include "AbilitySystem/AuraAbilitySystemComponent.h"
#include "AuraGameplayTags.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "UObject/SoftObjectPath.h"
#include "Engine/World.h"
#include "Engine/NetDriver.h"
#include "Engine/NetConnection.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/Character.h"
#include "NavigationSystem.h"
#include "Player/AuraPlayerController.h"
#include "TimerManager.h"

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

	bool NormalizeSavedAbilities(ULoadScreenSaveGame* SaveGame)
	{
		if (!SaveGame)
		{
			return false;
		}

		bool bChanged = false;
		for (TPair<FName, FMapSaveData>& SavedMap : SaveGame->SavedMaps)
		{
			FMapSaveData& MapData = SavedMap.Value;
			if (MapData.Players.IsEmpty() && MapData.PlayerData.bValid)
			{
				MapData.Players.Add(MapData.PlayerData);
				bChanged = true;
			}

			TArray<FPlayerSaveData*> PlayerRecords;
			PlayerRecords.Add(&MapData.PlayerData);
			for (FPlayerSaveData& PlayerData : MapData.Players)
			{
				PlayerRecords.Add(&PlayerData);
			}
			for (FPlayerSaveData* PlayerData : PlayerRecords)
			{
				TArray<FSavedAbilityData>& Abilities = PlayerData->Abilities;
				TMap<FGameplayTag, FSavedAbilityData> UniqueAbilities;
				for (const FSavedAbilityData& Ability : Abilities)
				{
					if (Ability.AbilityTag.IsValid())
					{
						UniqueAbilities.Add(Ability.AbilityTag, Ability);
					}
				}
				if (UniqueAbilities.Num() != Abilities.Num())
				{
					Abilities.Reset(UniqueAbilities.Num());
					for (const TPair<FGameplayTag, FSavedAbilityData>& Ability : UniqueAbilities)
					{
						Abilities.Add(Ability.Value);
					}
					bChanged = true;
				}
			}
		}
		if (SaveGame->SaveVersion < 3)
		{
			SaveGame->SaveVersion = 3;
			bChanged = true;
		}
		return bChanged;
	}

	void GetOrderedPlayerControllers(UWorld* World, TArray<APlayerController*>& OutControllers)
	{
		OutControllers.Reset();
		if (!World)
		{
			return;
		}
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			if (APlayerController* Controller = It->Get())
			{
				OutControllers.Add(Controller);
			}
		}
		OutControllers.Sort([](const APlayerController& A, const APlayerController& B)
		{
			if (A.IsLocalController() != B.IsLocalController())
			{
				return A.IsLocalController();
			}
			const APlayerState* AState = A.PlayerState;
			const APlayerState* BState = B.PlayerState;
			return (AState ? AState->GetPlayerId() : MAX_int32) <
				(BState ? BState->GetPlayerId() : MAX_int32);
		});
	}

	bool ExportPlayerData(AAuraCharacter* Player, FPlayerSaveData& PlayerData)
	{
		AAuraPlayerState* PlayerState = Player ? Player->GetPlayerState<AAuraPlayerState>() : nullptr;
		if (!PlayerState)
		{
			return false;
		}
		PlayerData.bValid = true;
		PlayerData.Transform = Player->GetActorTransform();
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
		return true;
	}

	bool ImportPlayerData(AAuraCharacter* Player, const FPlayerSaveData& PlayerData)
	{
		AAuraPlayerState* PlayerState = Player ? Player->GetPlayerState<AAuraPlayerState>() : nullptr;
		if (!PlayerState)
		{
			return false;
		}
		PlayerState->SetSaveRestoreInProgress(true);
		Player->SetActorTransform(PlayerData.Transform);
		PlayerState->SetLevel(PlayerData.Level);
		Player->RefreshAttributesAfterLoading();
		PlayerState->SetXP(PlayerData.XP);
		PlayerState->SetAttributePoints(PlayerData.AttributePoints);
		PlayerState->SetSpellPoints(PlayerData.SpellPoints);
		if (UAuraAbilitySystemComponent* AuraASC = Cast<UAuraAbilitySystemComponent>(PlayerState->GetAbilitySystemComponent()))
		{
			AuraASC->RestoreSavedAbilities(PlayerData.Abilities);
			if (UAuraAttributeSet* Attributes = Cast<UAuraAttributeSet>(PlayerState->GetAttributeSet()))
			{
				AuraASC->SetNumericAttributeBase(UAuraAttributeSet::GetHealthAttribute(),
					FMath::Clamp(PlayerData.Health, 0.f, Attributes->GetMaxHealth()));
				AuraASC->SetNumericAttributeBase(UAuraAttributeSet::GetManaAttribute(),
					FMath::Clamp(PlayerData.Mana, 0.f, Attributes->GetMaxMana()));
			}
		}
		PlayerState->SetSaveRestoreInProgress(false);
		return true;
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
	if (SlotName.IsEmpty() || SlotIndex < 0)
	{
		return nullptr;
	}

	USaveGame* SaveGameObject = nullptr;
	if (UGameplayStatics::DoesSaveGameExist(SlotName, SlotIndex))
	{
		SaveGameObject = UGameplayStatics::LoadGameFromSlot(SlotName, SlotIndex);
	}
	else
	{
		if (!LoadScreenSaveGameClass)
		{
			UE_LOG(LogTemp, Error, TEXT("Aura: cannot create save slot '%s'; LoadScreenSaveGameClass is not configured."), *SlotName);
			return nullptr;
		}
		SaveGameObject = UGameplayStatics::CreateSaveGameObject(LoadScreenSaveGameClass);
	}
	ULoadScreenSaveGame* LoadScreenSaveGame = Cast<ULoadScreenSaveGame>(SaveGameObject);
	const bool bCompactedAbilities = AuraSaveConstants::NormalizeSavedAbilities(LoadScreenSaveGame);
	bool bSaveMigratedData = bCompactedAbilities;
	if (LoadScreenSaveGame && LoadScreenSaveGame->SaveSlotStatus == Taken &&
		LoadScreenSaveGame->MapName != AuraSaveConstants::DungeonMapName)
	{
		LoadScreenSaveGame->MapName = AuraSaveConstants::DungeonMapName;
		bSaveMigratedData = true;
		UE_LOG(LogTemp, Log, TEXT("Aura: migrated slot '%s' map name to Dungeon."), *SlotName);
	}
	if (bSaveMigratedData)
	{
		UGameplayStatics::SaveGameToSlot(LoadScreenSaveGame, SlotName, SlotIndex);
		if (bCompactedAbilities)
		{
			UE_LOG(LogTemp, Log, TEXT("Aura: compacted duplicate ability records in slot '%s'."), *SlotName);
		}
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
	// Only the server owns level travel in a networked game. Clients must
	// remain connected and follow the server's travel.
	if (GetNetMode() == NM_Client)
	{
		UE_LOG(LogTemp, Warning, TEXT("Aura: client cannot start map travel; waiting for server."));
		return;
	}

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

	const FString MapPath = MapAsset->ToSoftObjectPath().GetLongPackageName();
	UE_LOG(LogTemp, Log, TEXT("Aura: travelling to map '%s' for slot '%s' (NetMode=%d)."),
		*MapPath, *SlotName, static_cast<int32>(GetNetMode()));

	if (GetNetMode() == NM_Standalone)
	{
		UGameplayStatics::OpenLevel(this, FName(*MapPath));
	}
	else
	{
		PendingMapTravelPath = MapPath;
		ExecutePendingMapTravel();
	}
}

void AAuraGameModeBase::ExecutePendingMapTravel()
{
	if (!HasAuthority() || !GetWorld() || PendingMapTravelPath.IsEmpty())
	{
		return;
	}

	bool bConnectionStillJoining = GetWorld()->GetTimeSeconds() < 1.f;
	if (const UNetDriver* NetDriver = GetWorld()->GetNetDriver())
	{
		for (const UNetConnection* Connection : NetDriver->ClientConnections)
		{
			if (IsValid(Connection) && !IsValid(Connection->PlayerController))
			{
				bConnectionStillJoining = true;
				break;
			}
		}
	}
	if (bConnectionStillJoining)
	{
		GetWorldTimerManager().SetTimer(PendingMapTravelTimer, this,
			&AAuraGameModeBase::ExecutePendingMapTravel, 0.2f, false);
		return;
	}

	const FString MapPath = MoveTemp(PendingMapTravelPath);
	PendingMapTravelPath.Reset();
	UE_LOG(LogTemp, Log, TEXT("Aura: network clients are ready; starting server travel to '%s'."), *MapPath);
	// Relative travel preserves the current listen URL and PIE port.
	GetWorld()->ServerTravel(MapPath, false);
}

void AAuraGameModeBase::BeginPlay()
{
	Super::BeginPlay();
	UE_LOG(LogTemp, Log, TEXT("Aura: GameMode BeginPlay NetMode=%d Authority=%d World=%s"),
		static_cast<int32>(GetNetMode()), HasAuthority() ? 1 : 0, *GetNameSafe(GetWorld()));
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
	if (HasAuthority() && !bSkipEndPlaySave && !AuraSaveConstants::IsMenuMap(CurrentMapName))
	{
		const bool bSaved = SaveCurrentWorldInternal();
		UE_LOG(LogTemp, Log, TEXT("Aura: saved world on EndPlay (%s): %s"),
			*UEnum::GetValueAsString(EndPlayReason), bSaved ? TEXT("Success") : TEXT("Failed"));
	}

	Super::EndPlay(EndPlayReason);
}

void AAuraGameModeBase::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	if (HasAuthority() && GetWorld())
	{
		// PostLogin can run just before the new pawn is fully ready. The restore
		// routine retries briefly when either the pawn or PlayerState is missing.
		GetWorldTimerManager().SetTimer(PlayerRestoreTimer, this,
			&AAuraGameModeBase::RestoreCurrentWorld, 0.2f, false);
	}
}

void AAuraGameModeBase::Logout(AController* Exiting)
{
	if (HasAuthority() && IsValid(Exiting))
	{
		const TWeakObjectPtr<AController> ControllerKey(Exiting);
		if (FTimerHandle* RespawnTimer = PlayerRespawnTimers.Find(ControllerKey))
		{
			GetWorldTimerManager().ClearTimer(*RespawnTimer);
		}
		PlayerRespawnTimers.Remove(ControllerKey);
		PendingRespawnControllers.Remove(ControllerKey);
		PlayerRespawnTransforms.Remove(ControllerKey);

		Exiting->StopMovement();
		if (AAuraPlayerState* AuraPlayerState = Exiting->GetPlayerState<AAuraPlayerState>())
		{
			if (UAuraAbilitySystemComponent* AuraASC =
				Cast<UAuraAbilitySystemComponent>(AuraPlayerState->GetAbilitySystemComponent()))
			{
				AuraASC->CancelAllAbilities();
				FGameplayTagContainer DebuffTags;
				DebuffTags.AddTag(FAuraGameplayTags::Get().Effects_Debuff);
				AuraASC->RemoveActiveEffectsWithTags(DebuffTags);
			}
		}

		if (APawn* LeavingPawn = Exiting->GetPawn())
		{
			for (TActorIterator<AAuraEnemy> It(GetWorld()); It; ++It)
			{
				It->HandleTargetActorInvalidated(LeavingPawn);
			}
		}

		if (APlayerController* LeavingPlayer = Cast<APlayerController>(Exiting))
		{
			const TWeakObjectPtr<APlayerController> PlayerKey(LeavingPlayer);
			if (const int32* PlayerIndex = PlayerSaveIndices.Find(PlayerKey))
			{
				// A reconnect using this session slot must be restored again.
				RestoredPlayerIndices.Remove(*PlayerIndex);
			}
			PlayerSaveIndices.Remove(PlayerKey);
		}
	}

	Super::Logout(Exiting);
}

void AAuraGameModeBase::HandlePlayerDeath(AAuraCharacter* DeadCharacter)
{
	if (!HasAuthority() || !IsValid(DeadCharacter))
	{
		return;
	}

	AController* Controller = DeadCharacter->GetController();
	if (!IsValid(Controller))
	{
		return;
	}

	const TWeakObjectPtr<AController> ControllerKey(Controller);
	if (PendingRespawnControllers.Contains(ControllerKey))
	{
		return;
	}

	if (!PlayerRespawnTransforms.Contains(ControllerKey))
	{
		PlayerRespawnTransforms.Add(ControllerKey, ResolveRespawnTransform(Controller, DeadCharacter));
	}
	PendingRespawnControllers.Add(ControllerKey);

	AAuraPlayerState* AuraPlayerState = Controller->GetPlayerState<AAuraPlayerState>();
	if (UAuraAbilitySystemComponent* AuraASC = AuraPlayerState
		? Cast<UAuraAbilitySystemComponent>(AuraPlayerState->GetAbilitySystemComponent()) : nullptr)
	{
		AuraASC->CancelAllAbilities();
		FGameplayTagContainer DebuffTags;
		DebuffTags.AddTag(FAuraGameplayTags::Get().Effects_Debuff);
		AuraASC->RemoveActiveEffectsWithTags(DebuffTags);
		AuraASC->SetLooseGameplayTagCount(FAuraGameplayTags::Get().Player_Block, 0);
	}
	for (TActorIterator<AAuraEnemy> It(GetWorld()); It; ++It)
	{
		It->HandleTargetActorInvalidated(DeadCharacter);
	}

	Controller->StopMovement();
	FTimerHandle& RespawnTimer = PlayerRespawnTimers.FindOrAdd(ControllerKey);
	FTimerDelegate RespawnDelegate;
	RespawnDelegate.BindUObject(this, &AAuraGameModeBase::RespawnPlayer, Controller);
	GetWorldTimerManager().SetTimer(RespawnTimer, RespawnDelegate, PlayerRespawnDelay, false);
	UE_LOG(LogTemp, Log, TEXT("Aura: player %s died; respawning in %.1f seconds."),
		*GetNameSafe(Controller), PlayerRespawnDelay);
}

FTransform AAuraGameModeBase::ResolveRespawnTransform(AController* Controller, const APawn* DeadPawn) const
{
	if (Controller)
	{
		if (const FTransform* SavedTransform = PlayerRespawnTransforms.Find(TWeakObjectPtr<AController>(Controller)))
		{
			return *SavedTransform;
		}
		if (Controller->StartSpot.IsValid())
		{
			return Controller->StartSpot->GetActorTransform();
		}
	}
	return DeadPawn ? DeadPawn->GetActorTransform() : FTransform::Identity;
}

bool AAuraGameModeBase::FindNearestValidRespawnTransform(AController* Controller,
	const FTransform& DesiredTransform, FTransform& OutTransform)
{
	if (!Controller || !GetWorld())
	{
		return false;
	}

	const UClass* PawnClass = GetDefaultPawnClassForController(Controller);
	const ACharacter* CharacterDefaults = PawnClass ? PawnClass->GetDefaultObject<ACharacter>() : nullptr;
	const UCapsuleComponent* Capsule = CharacterDefaults ? CharacterDefaults->GetCapsuleComponent() : nullptr;
	if (!Capsule)
	{
		return false;
	}

	float CapsuleRadius = 0.f;
	float CapsuleHalfHeight = 0.f;
	Capsule->GetScaledCapsuleSize(CapsuleRadius, CapsuleHalfHeight);
	const float ClearanceRadius = CapsuleRadius + FMath::Max(0.f, RespawnCollisionPadding);
	const float ClearanceHalfHeight = FMath::Max(CapsuleHalfHeight - 1.f, ClearanceRadius);
	const FCollisionShape ClearanceShape = FCollisionShape::MakeCapsule(ClearanceRadius, ClearanceHalfHeight);
	const FName CollisionProfile = Capsule->GetCollisionProfileName();

	auto IsLocationClear = [this, &ClearanceShape, CollisionProfile](const FVector& Location)
	{
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(AuraPlayerRespawnClearance), false);
		if (!CollisionProfile.IsNone())
		{
			return !GetWorld()->OverlapBlockingTestByProfile(
				Location, FQuat::Identity, CollisionProfile, ClearanceShape, QueryParams);
		}
		return !GetWorld()->OverlapBlockingTestByChannel(
			Location, FQuat::Identity, ECC_Pawn, ClearanceShape, QueryParams);
	};

	if (IsLocationClear(DesiredTransform.GetLocation()))
	{
		OutTransform = DesiredTransform;
		return true;
	}

	UNavigationSystemV1* NavigationSystem = UNavigationSystemV1::GetCurrent(GetWorld());
	if (!NavigationSystem)
	{
		return false;
	}

	struct FRespawnCandidate
	{
		FVector Location = FVector::ZeroVector;
		double DistanceSquared = 0.0;
	};

	TArray<FRespawnCandidate> Candidates;
	const float SearchStep = FMath::Max(25.f, RespawnSearchStep);
	const float SearchRadius = FMath::Max(SearchStep, RespawnSearchRadius);
	const int32 NumRings = FMath::CeilToInt(SearchRadius / SearchStep);
	const int32 MinimumSamples = FMath::Clamp(RespawnSamplesPerRing, 4, 96);
	const FVector DesiredFloorLocation = DesiredTransform.GetLocation()
		- FVector::UpVector * CapsuleHalfHeight;
	const FVector ProjectionExtent(
		FMath::Max(SearchStep * 0.5f, ClearanceRadius),
		FMath::Max(SearchStep * 0.5f, ClearanceRadius),
		FMath::Max(250.f, CapsuleHalfHeight * 2.f));
	const float DuplicateDistanceSquared = FMath::Square(FMath::Max(10.f, CapsuleRadius * 0.25f));

	for (int32 RingIndex = 1; RingIndex <= NumRings; ++RingIndex)
	{
		const float RingRadius = FMath::Min(RingIndex * SearchStep, SearchRadius);
		const int32 CircumferenceSamples = FMath::CeilToInt(2.f * UE_PI * RingRadius / SearchStep);
		const int32 NumSamples = FMath::Clamp(FMath::Max(MinimumSamples, CircumferenceSamples), 4, 96);
		const float AngleOffset = (RingIndex % 2 == 0) ? UE_PI / NumSamples : 0.f;

		for (int32 SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
		{
			const float Angle = AngleOffset + 2.f * UE_PI * SampleIndex / NumSamples;
			const FVector SampleLocation = DesiredFloorLocation + FVector(
				FMath::Cos(Angle) * RingRadius,
				FMath::Sin(Angle) * RingRadius,
				0.f);
			FNavLocation ProjectedLocation;
			if (!NavigationSystem->ProjectPointToNavigation(SampleLocation, ProjectedLocation, ProjectionExtent))
			{
				continue;
			}

			const FVector ActorLocation = ProjectedLocation.Location
				+ FVector::UpVector * (CapsuleHalfHeight + 2.f);
			if (Candidates.ContainsByPredicate([&ActorLocation, DuplicateDistanceSquared](const FRespawnCandidate& Existing)
			{
				return FVector::DistSquared(Existing.Location, ActorLocation) <= DuplicateDistanceSquared;
			}))
			{
				continue;
			}

			Candidates.Add({ActorLocation,
				FVector::DistSquared(DesiredTransform.GetLocation(), ActorLocation)});
		}
	}

	Candidates.Sort([](const FRespawnCandidate& A, const FRespawnCandidate& B)
	{
		return A.DistanceSquared < B.DistanceSquared;
	});

	for (const FRespawnCandidate& Candidate : Candidates)
	{
		if (IsLocationClear(Candidate.Location))
		{
			OutTransform = DesiredTransform;
			OutTransform.SetLocation(Candidate.Location);
			return true;
		}
	}

	return false;
}

void AAuraGameModeBase::ScheduleRespawnRetry(AController* Controller)
{
	if (!HasAuthority() || !IsValid(Controller))
	{
		return;
	}

	const TWeakObjectPtr<AController> ControllerKey(Controller);
	if (!PendingRespawnControllers.Contains(ControllerKey))
	{
		return;
	}

	FTimerHandle& RespawnTimer = PlayerRespawnTimers.FindOrAdd(ControllerKey);
	FTimerDelegate RespawnDelegate;
	RespawnDelegate.BindUObject(this, &AAuraGameModeBase::RespawnPlayer, Controller);
	GetWorldTimerManager().SetTimer(
		RespawnTimer, RespawnDelegate, FMath::Max(0.05f, RespawnRetryDelay), false);
}

void AAuraGameModeBase::RespawnPlayer(AController* Controller)
{
	const TWeakObjectPtr<AController> ControllerKey(Controller);
	PlayerRespawnTimers.Remove(ControllerKey);
	if (!HasAuthority() || !IsValid(Controller) || !PendingRespawnControllers.Contains(ControllerKey))
	{
		return;
	}

	const APawn* OldPawn = Controller->GetPawn();
	const FTransform DesiredRespawnTransform = ResolveRespawnTransform(Controller, OldPawn);
	APawn* PawnToDestroy = Controller->GetPawn();
	Controller->UnPossess();
	if (IsValid(PawnToDestroy))
	{
		PawnToDestroy->Destroy();
	}

	FTransform RespawnTransform;
	if (!FindNearestValidRespawnTransform(Controller, DesiredRespawnTransform, RespawnTransform))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Aura: no clear respawn point found for %s within %.0f units of %s; retrying."),
			*GetNameSafe(Controller), RespawnSearchRadius,
			*DesiredRespawnTransform.GetLocation().ToCompactString());
		ScheduleRespawnRetry(Controller);
		return;
	}

	RestartPlayerAtTransform(Controller, RespawnTransform);
	if (!IsValid(Controller->GetPawn()))
	{
		UE_LOG(LogTemp, Warning, TEXT("Aura: failed to respawn player %s at %s; retrying."),
			*GetNameSafe(Controller), *RespawnTransform.GetLocation().ToCompactString());
		ScheduleRespawnRetry(Controller);
		return;
	}
	PendingRespawnControllers.Remove(ControllerKey);

	if (AAuraPlayerState* AuraPlayerState = Controller->GetPlayerState<AAuraPlayerState>())
	{
		if (UAuraAbilitySystemComponent* AuraASC = Cast<UAuraAbilitySystemComponent>(AuraPlayerState->GetAbilitySystemComponent()))
		{
			if (const UAuraAttributeSet* Attributes = Cast<UAuraAttributeSet>(AuraPlayerState->GetAttributeSet()))
			{
				AuraASC->SetNumericAttributeBase(UAuraAttributeSet::GetHealthAttribute(),
					Attributes->GetMaxHealth() * FMath::Clamp(RespawnHealthFraction, 0.f, 1.f));
				AuraASC->SetNumericAttributeBase(UAuraAttributeSet::GetManaAttribute(),
					Attributes->GetMaxMana() * FMath::Clamp(RespawnManaFraction, 0.f, 1.f));
			}
			AuraASC->ActivateEquippedPassiveAbilities();
		}
	}

	UE_LOG(LogTemp, Log, TEXT("Aura: respawned player %s at %s (requested %s)."),
		*GetNameSafe(Controller), *RespawnTransform.GetLocation().ToCompactString(),
		*DesiredRespawnTransform.GetLocation().ToCompactString());
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
	SaveCurrentWorldInternal();
}

bool AAuraGameModeBase::SaveCurrentWorld()
{
	if (!HasAuthority())
	{
		if (AAuraPlayerController* PlayerController = Cast<AAuraPlayerController>(UGameplayStatics::GetPlayerController(this, 0)))
		{
			PlayerController->ServerTravelToLoadMenu();
		}
		return false;
	}

	const bool bSaved = SaveCurrentWorldInternal();
	if (bSaved && GetWorld() && !AuraSaveConstants::IsMenuMap(AuraSaveConstants::GetCanonicalMapName(this)))
	{
		bSkipEndPlaySave = true;
		GetWorld()->ServerTravel(TEXT("/Game/Maps/MainMenu"), false);
	}
	return bSaved;
}

bool AAuraGameModeBase::SaveCurrentWorldInternal()
{
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
	const FName SavedMapName = MapData.MapAssetName;
	int32 SavedPlayerCount = 0;
	TArray<APlayerController*> PlayerControllers;
	AuraSaveConstants::GetOrderedPlayerControllers(GetWorld(), PlayerControllers);
	for (int32 OrderedIndex = 0; OrderedIndex < PlayerControllers.Num(); ++OrderedIndex)
	{
		APlayerController* PlayerController = PlayerControllers[OrderedIndex];
		const int32 PlayerIndex = PlayerSaveIndices.FindOrAdd(PlayerController, OrderedIndex);
		if (!MapData.Players.IsValidIndex(PlayerIndex))
		{
			MapData.Players.SetNum(PlayerIndex + 1);
		}
		const bool bHasSavedPlayerData = MapData.Players.IsValidIndex(PlayerIndex)
			&& MapData.Players[PlayerIndex].bValid;
		const bool bHasLegacyHostData = PlayerIndex == 0 && MapData.PlayerData.bValid;
		if (PendingRespawnControllers.Contains(TWeakObjectPtr<AController>(PlayerController)))
		{
			if (!bHasSavedPlayerData && bHasLegacyHostData)
			{
				MapData.Players[PlayerIndex] = MapData.PlayerData;
			}
			UE_LOG(LogTemp, Warning,
				TEXT("Aura: preserving player index=%d while waiting to respawn."), PlayerIndex);
			continue;
		}
		if (!RestoredPlayerIndices.Contains(PlayerIndex) && (bHasSavedPlayerData || bHasLegacyHostData))
		{
			// A newly joined pawn still contains defaults until its delayed restore completes.
			// Preserve the existing record instead of overwriting it during an early exit/save.
			if (!bHasSavedPlayerData && bHasLegacyHostData)
			{
				MapData.Players[PlayerIndex] = MapData.PlayerData;
			}
			UE_LOG(LogTemp, Warning,
				TEXT("Aura: preserving player index=%d because save restore has not completed."),
				PlayerIndex);
			continue;
		}

		AAuraCharacter* Player = Cast<AAuraCharacter>(PlayerController->GetPawn());
		if (AuraSaveConstants::ExportPlayerData(Player, MapData.Players[PlayerIndex]))
		{
			++SavedPlayerCount;
			UE_LOG(LogTemp, Verbose, TEXT("Aura: saved controller %s to stable player index=%d."),
				*GetNameSafe(PlayerController), PlayerIndex);
		}
	}
	if (MapData.Players.IsValidIndex(0) && MapData.Players[0].bValid)
	{
		MapData.PlayerData = MapData.Players[0];
		SaveGame->PlayerLevel = MapData.PlayerData.Level;
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
	SaveGame->SaveVersion = 3;
	SaveGame->MapName = CurrentMapName;
	const int32 SavedSpawnerCount = MapData.Spawners.Num();
	const int32 SavedEnemyCount = MapData.Enemies.Num();
	SaveGame->SavedMaps.Add(MapData.MapAssetName, MoveTemp(MapData));
	const bool bSaved = UGameplayStatics::SaveGameToSlot(SaveGame, AuraGameInstance->CurrentSlotName, AuraGameInstance->CurrentSlotIndex);
	UE_LOG(LogTemp, Log, TEXT("Aura: SaveCurrentWorld slot=%s index=%d map=%s players=%d spawners=%d enemies=%d result=%s"),
		*AuraGameInstance->CurrentSlotName,
		AuraGameInstance->CurrentSlotIndex,
		*SavedMapName.ToString(),
		SavedPlayerCount,
		SavedSpawnerCount,
		SavedEnemyCount,
		bSaved ? TEXT("Success") : TEXT("Failed"));
	return bSaved;
}

void AAuraGameModeBase::SaveAndReturnToMainMenu()
{
	if (!HasAuthority() || !GetWorld())
	{
		return;
	}

	SaveCurrentWorldInternal();
	bSkipEndPlaySave = true;
	UE_LOG(LogTemp, Log, TEXT("Aura: server returning all players to MainMenu."));
	// Relative travel preserves the active listen address and PIE port so the
	// connected client can follow and remain in the same session.
	GetWorld()->ServerTravel(TEXT("/Game/Maps/MainMenu"), false);
}

void AAuraGameModeBase::HandleRemotePlayerLeave(APlayerController* LeavingPlayer)
{
	if (!HasAuthority() || !IsValid(LeavingPlayer) || LeavingPlayer->IsLocalController())
	{
		return;
	}

	// Capture this player's latest authoritative state while its Pawn and
	// PlayerState are still valid. The subsequent disconnect is client-only;
	// the listen server World must keep running for the remaining players.
	const bool bSaved = SaveCurrentWorldInternal();
	UE_LOG(LogTemp, Log, TEXT("Aura: remote player %s leaving session; snapshot=%s."),
		*GetNameSafe(LeavingPlayer), bSaved ? TEXT("Saved") : TEXT("Unavailable"));

	LeavingPlayer->StopMovement();
	LeavingPlayer->ClientReturnToMainMenuWithTextReason(FText::GetEmpty());
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
	TArray<APlayerController*> PlayerControllers;
	AuraSaveConstants::GetOrderedPlayerControllers(GetWorld(), PlayerControllers);
	bool bNeedsRetry = false;
	for (int32 OrderedIndex = 0; OrderedIndex < PlayerControllers.Num(); ++OrderedIndex)
	{
		APlayerController* PlayerController = PlayerControllers[OrderedIndex];
		const int32 PlayerIndex = PlayerSaveIndices.FindOrAdd(PlayerController, OrderedIndex);
		if (RestoredPlayerIndices.Contains(PlayerIndex))
		{
			continue;
		}

		const FPlayerSaveData* PlayerData = MapData->Players.IsValidIndex(PlayerIndex)
			? &MapData->Players[PlayerIndex]
			: (PlayerIndex == 0 ? &MapData->PlayerData : nullptr);
		if (!PlayerData || !PlayerData->bValid)
		{
			RestoredPlayerIndices.Add(PlayerIndex);
			continue;
		}

		AAuraCharacter* Player = Cast<AAuraCharacter>(PlayerController->GetPawn());
		if (!AuraSaveConstants::ImportPlayerData(Player, *PlayerData))
		{
			bNeedsRetry = true;
			continue;
		}

		RestoredPlayerIndices.Add(PlayerIndex);
		PlayerRespawnTransforms.Add(TWeakObjectPtr<AController>(PlayerController), PlayerData->Transform);
		UE_LOG(LogTemp, Log, TEXT("Aura: restored player index=%d location=%s level=%d xp=%d health=%.1f mana=%.1f abilities=%d."),
			PlayerIndex,
			*PlayerData->Transform.GetLocation().ToCompactString(),
			PlayerData->Level,
			PlayerData->XP,
			PlayerData->Health,
			PlayerData->Mana,
			PlayerData->Abilities.Num());
	}
	if (bNeedsRetry)
	{
		GetWorldTimerManager().SetTimer(PlayerRestoreTimer, this,
			&AAuraGameModeBase::RestoreCurrentWorld, 0.2f, false);
	}
}
