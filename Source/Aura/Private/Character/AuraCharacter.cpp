


#include "Character/AuraCharacter.h"

#include "AbilitySystemComponent.h"
#include "NiagaraComponent.h"
#include "AbilitySystem/AuraAbilitySystemComponent.h"
#include "AbilitySystem/AuraNiagaraComponent.h"
#include "AbilitySystem/Data/LevelUpInfo.h"
#include "AuraGameplayTags.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Player/AuraPlayerController.h"
#include "Player/AuraPlayerState.h"
#include "UI/HUD/AuraHUD.h"
#include "NiagaraSystem.h"

AAuraCharacter::AAuraCharacter()
{
	bReplicates = true;
	SetReplicateMovement(true);
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>("CameraBoom");
	CameraBoom->SetupAttachment(GetRootComponent());
	CameraBoom->SetUsingAbsoluteRotation(true);
	CameraBoom->bDoCollisionTest = false;
	
	TopDownCameraComponent = CreateDefaultSubobject<UCameraComponent>("CameraComponent");
	TopDownCameraComponent->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	TopDownCameraComponent->bUsePawnControlRotation = false;
	
	LevelUpNiagaraComponent = CreateDefaultSubobject<UNiagaraComponent>("LevelUpNiagaraComponent");
	LevelUpNiagaraComponent->SetupAttachment(GetRootComponent());
	LevelUpNiagaraComponent->bAutoActivate = false;

	HaloNiagaraComponent = CreateDefaultSubobject<UAuraNiagaraComponent>("HaloNiagaraComponent");
	HaloNiagaraComponent->SetupAttachment(GetRootComponent());
	HaloNiagaraComponent->bLoopWhileActive = true;
	HaloNiagaraComponent->bDeactivateImmediately = true;
	HaloNiagaraComponent->InitialSimulationTime = 1.f;

	LifeSiphonNiagaraComponent = CreateDefaultSubobject<UAuraNiagaraComponent>("LifeSiphonNiagaraComponent");
	LifeSiphonNiagaraComponent->SetupAttachment(GetRootComponent());
	LifeSiphonNiagaraComponent->bLoopWhileActive = true;

	ManaSiphonNiagaraComponent = CreateDefaultSubobject<UAuraNiagaraComponent>("ManaSiphonNiagaraComponent");
	ManaSiphonNiagaraComponent->SetupAttachment(GetRootComponent());
	ManaSiphonNiagaraComponent->bLoopWhileActive = true;
	
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 400.f, 0.f);
	GetCharacterMovement()->bConstrainToPlane = true;
	GetCharacterMovement()->bSnapToPlaneAtStart = true;
	
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;
	bUseControllerRotationYaw = false;
	
	CharacterClass = ECharacterClass::Elementalist;
}

void AAuraCharacter::BeginPlay()
{
	Super::BeginPlay();

	const FAuraGameplayTags& GameplayTags = FAuraGameplayTags::Get();
	HaloNiagaraComponent->GameplayTag = GameplayTags.Effects_Passive_Halo_ShieldReady;
	LifeSiphonNiagaraComponent->GameplayTag = GameplayTags.Effects_Passive_LifeSiphon;
	ManaSiphonNiagaraComponent->GameplayTag = GameplayTags.Effects_Passive_ManaSiphon;

	if (!HaloNiagaraComponent->GetAsset())
	{
		HaloNiagaraComponent->SetAsset(LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/Assets/Effects/Stun/NS_Halo.NS_Halo")));
	}
	if (!LifeSiphonNiagaraComponent->GetAsset())
	{
		LifeSiphonNiagaraComponent->SetAsset(LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/Assets/Effects/Stun/NS_LifeSiphon.NS_LifeSiphon")));
	}
	if (!ManaSiphonNiagaraComponent->GetAsset())
	{
		ManaSiphonNiagaraComponent->SetAsset(LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/Assets/Effects/Stun/NS_ManaSiphon.NS_ManaSiphon")));
	}
}

void AAuraCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	UE_LOG(LogTemp, Log, TEXT("Aura: %s possessed by %s NetMode=%d Role=%d Authority=%d"),
		*GetName(), *GetNameSafe(NewController), static_cast<int32>(GetNetMode()),
		static_cast<int32>(GetLocalRole()), HasAuthority() ? 1 : 0);
	
	// Init ability actor info for the server
	InitAbilityActorInfo();
	AddCharacterAbilities();
}

void AAuraCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	
	// Init ability actor info for the client
	InitAbilityActorInfo();
}

void AAuraCharacter::RefreshAttributesAfterLoading()
{
	RefreshDefaultSecondaryAttributes();
}

int32 AAuraCharacter::GetLevel_Implementation()
{
	return GetAuraPlayerState()->GetPlayerLevel();
}

int32 AAuraCharacter::GetXP_Implementation() const
{
	return GetAuraPlayerState()->GetXP();
}

int32 AAuraCharacter::FindLevelForXP_Implementation(int32 XP) const
{
	return GetAuraPlayerState()->LevelUpInfo->FindLevelForXP(XP);
}

int32 AAuraCharacter::GetAttributePointsReward_Implementation(int32 Level) const
{
	return GetAuraPlayerState()->LevelUpInfo->LevelUpInfos[Level].AttributePointAward;
}

int32 AAuraCharacter::GetSpellPointsReward_Implementation(int32 Level) const
{
	return GetAuraPlayerState()->LevelUpInfo->LevelUpInfos[Level].SpellPointAward;
}

void AAuraCharacter::AddToPlayerLevel_Implementation(int32 Level)
{
	GetAuraPlayerState()->AddToLevel(Level);
	
	if (UAuraAbilitySystemComponent* AuraASC = Cast<UAuraAbilitySystemComponent>(GetAbilitySystemComponent()))
	{
		AuraASC->UpdateAbilityStatus(GetAuraPlayerState()->GetPlayerLevel());
	}
}

void AAuraCharacter::AddToAttributePoints_Implementation(int32 AttributePoints)
{
	GetAuraPlayerState()->AddToAttributePoints(AttributePoints);
}

void AAuraCharacter::AddToSpellPoints_Implementation(int32 SpellPoints)
{
	GetAuraPlayerState()->AddToSpellPoints(SpellPoints);
}

int32 AAuraCharacter::GetAttributePoints_Implementation() const
{
	return GetAuraPlayerState()->GetAttributePoints();
}

int32 AAuraCharacter::GetSpellPoints_Implementation() const
{
	return GetAuraPlayerState()->GetSpellPoints();
}

void AAuraCharacter::ShowMagicCircle_Implementation(UMaterialInterface* DecalMaterial)
{
	if (AAuraPlayerController* PC = Cast<AAuraPlayerController>(GetController())) PC->ShowMagicCircle(DecalMaterial);
}

void AAuraCharacter::HideMagicCircle_Implementation()
{
	if (AAuraPlayerController* PC = Cast<AAuraPlayerController>(GetController())) PC->HideMagicCircle();
}

FVector AAuraCharacter::GetMagicCircleLocation_Implementation() const
{
	FVector Location;
	if (const AAuraPlayerController* PC = Cast<AAuraPlayerController>(GetController()); PC && PC->GetMagicCircleLocation(Location)) return Location;
	return GetActorLocation();
}

void AAuraCharacter::AddToXP_Implementation(int32 XP)
{
	GetAuraPlayerState()->AddToXP(XP);
}

void AAuraCharacter::LevelUp_Implementation()
{
	MulticastLevelUpParticles();
}

void AAuraCharacter::InitAbilityActorInfo()
{
	AAuraPlayerState* AuraPlayerState = GetPlayerState<AAuraPlayerState>();
	check(AuraPlayerState);
	AuraPlayerState->GetAbilitySystemComponent()->InitAbilityActorInfo(AuraPlayerState, this);
	Cast<UAuraAbilitySystemComponent>(AuraPlayerState->GetAbilitySystemComponent())->AbilityActorInfoSet();
	AbilitySystemComponent = AuraPlayerState->GetAbilitySystemComponent();
	AttributeSet = AuraPlayerState->GetAttributeSet();
	
	if (AAuraPlayerController* AuraPlayerController = Cast<AAuraPlayerController>(GetController()))
	{
		if (AAuraHUD* AuraHUD = Cast<AAuraHUD>(AuraPlayerController->GetHUD()))
		{
			AuraHUD->InitOverlay(AuraPlayerController, AuraPlayerState, AbilitySystemComponent, AttributeSet);
		}
	}
	
	// Player attributes are authoritative and replicated from PlayerState.
	// Applying the default GameplayEffects again in OnRep_PlayerState creates
	// client-only Health/Mana bases; the first predicted/replicated ability
	// effect then recomputes them and makes the UI jump to those default values.
	if (HasAuthority())
	{
		InitializeDefaultAttributes();
	}
}

AAuraPlayerState* AAuraCharacter::GetAuraPlayerState() const
{
	AAuraPlayerState* AuraPlayerState = GetPlayerState<AAuraPlayerState>();
	check(AuraPlayerState);
	
	return AuraPlayerState;
}

void AAuraCharacter::MulticastLevelUpParticles_Implementation() const
{
	if (IsValid(LevelUpNiagaraComponent))
	{
		const FVector CameraLocation = TopDownCameraComponent->GetComponentLocation();
		const FVector NiagaraSystemLocation = LevelUpNiagaraComponent->GetComponentLocation();
		const FRotator ToCameraRotation = (CameraLocation - NiagaraSystemLocation).Rotation();
		LevelUpNiagaraComponent->SetWorldRotation(ToCameraRotation);
		LevelUpNiagaraComponent->Activate(true);
	}
}
