


#include "Character/AuraCharacter.h"

#include "AbilitySystemComponent.h"
#include "NiagaraComponent.h"
#include "AbilitySystem/AuraAbilitySystemComponent.h"
#include "AbilitySystem/Data/LevelUpInfo.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Player/AuraPlayerController.h"
#include "Player/AuraPlayerState.h"
#include "UI/HUD/AuraHUD.h"

AAuraCharacter::AAuraCharacter()
{
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
	
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 400.f, 0.f);
	GetCharacterMovement()->bConstrainToPlane = true;
	GetCharacterMovement()->bSnapToPlaneAtStart = true;
	
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;
	bUseControllerRotationYaw = false;
	
	CharacterClass = ECharacterClass::Elementalist;
}

void AAuraCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	
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
	
	InitializeDefaultAttributes();
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
