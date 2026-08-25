


#include "Character/AuraEnemy.h"

#include "AbilitySystem/AuraAbilitySystemComponent.h"
#include "AbilitySystem/AuraAbilitySystemLibrary.h"
#include "AbilitySystem/AuraAttributeSet.h"
#include "Aura/Aura.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "UI/Widget/AuraUserWidget.h"
#include "AuraGameplayTags.h"
#include "AI/AuraAIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BrainComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Net/UnrealNetwork.h"
#include "Navigation/CrowdManager.h"
#include "Subsystem/AuraEnemyPoolSubsystem.h"

AAuraEnemy::AAuraEnemy()
{
	bReplicates = true;
	SetReplicateMovement(true);
	Tags.AddUnique(FName("Enemy"));
	GetMesh()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	
	AbilitySystemComponent = CreateDefaultSubobject<UAuraAbilitySystemComponent>("AbilitySystemComponent");
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);
	
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;
	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bUseControllerDesiredRotation = true;
	
	AttributeSet = CreateDefaultSubobject<UAuraAttributeSet>("AttributeSet");
	
	HealthBar = CreateDefaultSubobject<UWidgetComponent>("HealthBar");
	HealthBar->SetupAttachment(GetRootComponent());
}

void AAuraEnemy::BeginPlay()
{
	Super::BeginPlay();
	CapturePoolDefaults();
	UpdateClientCrowdRegistration(!bPoolManaged || PoolState == EEnemyPoolState::Active);
	
	GetMesh()->SetCustomDepthStencilValue(CUSTOM_DEPTH_RED);
	Weapon->SetCustomDepthStencilValue(CUSTOM_DEPTH_RED);
	
	GetCharacterMovement()->MaxWalkSpeed = BaseWalkSpeed;
	InitAbilityActorInfo();
	if (HasAuthority())
	{
		UAuraAbilitySystemLibrary::GiveStartupAbilities(this, AbilitySystemComponent, CharacterClass);
	}
	
	if (UAuraUserWidget* UserWidget = Cast<UAuraUserWidget>(HealthBar->GetUserWidgetObject()))
	{
		UserWidget->SetWidgetController(this);
	}
	
	if (const UAuraAttributeSet* AuraAS = Cast<UAuraAttributeSet>(AttributeSet))
	{
		AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(AuraAS->GetHealthAttribute()).AddLambda(
			[this](const FOnAttributeChangeData& Data)
			{
				OnHealthChanged.Broadcast(Data.NewValue);
			}
		);
		AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(AuraAS->GetMaxHealthAttribute()).AddLambda(
			[this](const FOnAttributeChangeData& Data)
			{
				OnMaxHealthChanged.Broadcast(Data.NewValue);
			}
		);
		AbilitySystemComponent->RegisterGameplayTagEvent(FAuraGameplayTags::Get().Effects_HitReact, EGameplayTagEventType::NewOrRemoved).AddUObject(this, &AAuraEnemy::HitReactTagChanged);
		AbilitySystemComponent->RegisterGameplayTagEvent(FAuraGameplayTags::Get().Effects_Debuff_Stun, EGameplayTagEventType::NewOrRemoved).AddUObject(this, &AAuraEnemy::StunTagChanged);
		
		OnHealthChanged.Broadcast(AuraAS->GetHealth());
		OnMaxHealthChanged.Broadcast(AuraAS->GetMaxHealth());
	}
}

void AAuraEnemy::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	
	if (!HasAuthority())
	{
		return;
	}
	
	AuraAIController = Cast<AAuraAIController>(NewController);
	if (!AuraAIController || !BehaviorTree || !BehaviorTree->BlackboardAsset)
	{
		UE_LOG(LogTemp, Warning, TEXT("Aura: enemy %s cannot start AI; controller or behavior tree is missing."), *GetName());
		return;
	}
	
	AuraAIController->GetBlackboardComponent()->InitializeBlackboard(*BehaviorTree->BlackboardAsset);
	AuraAIController->RunBehaviorTree(BehaviorTree);
	AuraAIController->GetBlackboardComponent()->SetValueAsBool(FName("HitReacting"), false);
	AuraAIController->GetBlackboardComponent()->SetValueAsBool(FName("RangedAttacker"), CharacterClass != ECharacterClass::Warrior);
}

void AAuraEnemy::HitReactTagChanged(const FGameplayTag CallbackTag, int32 NewCount)
{
	// Stun owns the montage while it is active. A hit-react tag arriving in the
	// same frame must not put the enemy back into the hit-react state.
	bHitReacting = NewCount > 0 && !bStunned;
	UpdateMovementSpeed();
	UpdateRootMotionMode();
	if (AuraAIController && AuraAIController->GetBlackboardComponent())
	{
		// 修正：之前固定写 false，客户端/黑板不知道真实受击状态
		AuraAIController->GetBlackboardComponent()->SetValueAsBool(FName("HitReacting"), bHitReacting);
	}
}

void AAuraEnemy::StunTagChanged(const FGameplayTag CallbackTag, int32 NewCount)
{
	bStunned = NewCount > 0;
	if (AuraAIController && AuraAIController->GetBlackboardComponent())
	{
		AuraAIController->GetBlackboardComponent()->SetValueAsBool(FName("Stunned"), bStunned);
	}

	UpdateMovementSpeed();
	UpdateRootMotionMode();

	if (bStunned)
	{
		// 停止移动/追击（服务器上 AI 才有意义；StopMovement 只影响当前 move 请求）
		if (AuraAIController)
		{
			AuraAIController->StopMovement();
		}

		// 取消正在执行的攻击能力（带 Abilities.Attack 标签），并靠攻击 GA 的
		// ActivationBlockedTags(Effects.Debuff.Stun) 阻止眩晕期间重新激活（资产侧配置）
		if (HasAuthority())
		{
			const FAuraGameplayTags& AuraTags = FAuraGameplayTags::Get();
			FGameplayTagContainer TagsToCancel;
			TagsToCancel.AddTag(AuraTags.Abilities_Attack);
			TagsToCancel.AddTag(AuraTags.Effects_HitReact);
			AbilitySystemComponent->CancelAbilities(&TagsToCancel);

			FGameplayTagContainer HitReactTags;
			HitReactTags.AddTag(AuraTags.Effects_HitReact);
			AbilitySystemComponent->RemoveActiveEffectsWithGrantedTags(HitReactTags);
		}

		// Hit-react and attack montages can be active in the same frame as the stun
		// tag. Stop them explicitly so the stun montage always wins its slot.
		if (StunMontage)
		{
			StopAnimMontage();
			EnsureStunAnimation();
			GetWorldTimerManager().SetTimer(
				StunAnimationTimer, this, &AAuraEnemy::EnsureStunAnimation, 0.1f, true);
		}
	}
	else
	{
		GetWorldTimerManager().ClearTimer(StunAnimationTimer);
		if (StunMontage)
		{
			StopAnimMontage(StunMontage);
		}
	}
}

void AAuraEnemy::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UpdateClientCrowdRegistration(false);
	Super::EndPlay(EndPlayReason);
}

void AAuraEnemy::EnsureStunAnimation()
{
	if (!bStunned || bDead || (bPoolManaged && PoolState != EEnemyPoolState::Active))
	{
		return;
	}

	UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (!StunMontage || !AnimInstance)
	{
		return;
	}

	if (!AnimInstance->Montage_IsPlaying(StunMontage))
	{
		PlayAnimMontage(StunMontage);
	}
	if (StunMontage->GetNumSections() > 0)
	{
		const FName FirstSection = StunMontage->GetSectionName(0);
		AnimInstance->Montage_SetNextSection(FirstSection, FirstSection, StunMontage);
	}
}

void AAuraEnemy::UpdateMovementSpeed()
{
	// 眩晕或受击任一生效都停走；都解除才恢复，避免受击结束把仍眩晕的敌人提前放行
	GetCharacterMovement()->MaxWalkSpeed = (bStunned || bHitReacting) ? 0.f : BaseWalkSpeed;
}

void AAuraEnemy::Die()
{
	if (bPoolManaged)
	{
		if (PoolState != EEnemyPoolState::Active)
		{
			return;
		}
		SetPoolState(EEnemyPoolState::Dying);
		OnEnemyDyingDelegate.Broadcast(this);
		if (UWorld* World = GetWorld())
		{
			if (UAuraEnemyPoolSubsystem* Pool = World->GetSubsystem<UAuraEnemyPoolSubsystem>())
			{
				Pool->NotifyEnemyDying(this);
				const TWeakObjectPtr<UAuraEnemyPoolSubsystem> WeakPool(Pool);
				const TWeakObjectPtr<AAuraEnemy> WeakEnemy(this);
				World->GetTimerManager().SetTimer(PoolReturnTimer, [WeakPool, WeakEnemy]()
				{
					if (WeakPool.IsValid() && WeakEnemy.IsValid())
					{
						WeakPool->ReleaseEnemy(WeakEnemy.Get());
					}
				}, LifeSpan, false);
			}
		}
	}
	else
	{
		SetLifeSpan(LifeSpan);
	}
	if (AuraAIController && AuraAIController->GetBlackboardComponent())
	{
		AuraAIController->GetBlackboardComponent()->SetValueAsBool(FName("IsDead"), true);
	}
	Super::Die();
}

void AAuraEnemy::MulticastHandleDeath_Implementation()
{
	UpdateClientCrowdRegistration(false);
	GetWorldTimerManager().ClearTimer(StunAnimationTimer);
	Super::MulticastHandleDeath_Implementation();
	if (HealthBar)
	{
		HealthBar->SetVisibility(false);
	}
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		// The capsule loses collision during ragdoll. Leaving movement active lets
		// it and the attached health bar fall while the physical mesh stays above.
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}
}

void AAuraEnemy::UpdateRootMotionMode()
{
	if (UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
	{
		// Hit-react and stun montages are cosmetic. Their root tracks must never
		// move the replicated character capsule below the walkable floor.
		AnimInstance->SetRootMotionMode((bStunned || bHitReacting)
			? ERootMotionMode::IgnoreRootMotion
			: InitialRootMotionMode);
	}
}

void AAuraEnemy::ActivateFromPool(const FTransform& InTransform, int32 InLevel)
{
	if (!bPoolManaged)
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(PoolReturnTimer);
	SetActorTransform(InTransform, false, nullptr, ETeleportType::TeleportPhysics);
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	GetMesh()->SetSimulatePhysics(false);
	GetMesh()->SetEnableGravity(false);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	GetCharacterMovement()->SetActive(true);
	GetCharacterMovement()->StopMovementImmediately();
	Level = FMath::Max(1, InLevel);
	// Keep long-lived listener/passive abilities alive across pooling. Only
	// transient combat abilities can legitimately survive into a respawn.
	FGameplayTagContainer AbilitiesToCancel;
	AbilitiesToCancel.AddTag(FAuraGameplayTags::Get().Abilities_Attack);
	AbilitiesToCancel.AddTag(FAuraGameplayTags::Get().Effects_HitReact);
	AbilitySystemComponent->CancelAbilities(&AbilitiesToCancel);
	FGameplayTagContainer DebuffTags;
	DebuffTags.AddTag(FAuraGameplayTags::Get().Effects_Debuff);
	AbilitySystemComponent->RemoveActiveEffectsWithTags(DebuffTags);
	if (UAuraAttributeSet* AuraAS = Cast<UAuraAttributeSet>(AttributeSet))
	{
		AbilitySystemComponent->SetNumericAttributeBase(UAuraAttributeSet::GetHealthAttribute(), AuraAS->GetMaxHealth());
		AbilitySystemComponent->SetNumericAttributeBase(UAuraAttributeSet::GetManaAttribute(), AuraAS->GetMaxMana());
	}
	bDead = false;
	bHitReacting = false;
	bStunned = false;
	CombatTarget = nullptr;
	SetPoolIdentity(FGuid::NewGuid(), FGuid());
	SetPoolState(EEnemyPoolState::Active);

	// Runtime-spawned pooled actors are not guaranteed to use the Blueprint
	// AutoPossessAI setting. Ensure they have a controller and restart the BT
	// every time they leave the pool.
	if (!AuraAIController)
	{
		SpawnDefaultController();
		AuraAIController = Cast<AAuraAIController>(GetController());
	}
	if (AuraAIController && BehaviorTree && BehaviorTree->BlackboardAsset)
	{
		AuraAIController->GetBlackboardComponent()->InitializeBlackboard(*BehaviorTree->BlackboardAsset);
		AuraAIController->RunBehaviorTree(BehaviorTree);
		if (AuraAIController->GetBrainComponent())
		{
			AuraAIController->GetBrainComponent()->RestartLogic();
		}
		AuraAIController->GetBlackboardComponent()->SetValueAsBool(FName("IsDead"), false);
		AuraAIController->GetBlackboardComponent()->SetValueAsBool(FName("HitReacting"), false);
		AuraAIController->GetBlackboardComponent()->SetValueAsBool(FName("Stunned"), false);
		AuraAIController->GetBlackboardComponent()->SetValueAsBool(FName("RangedAttacker"), CharacterClass != ECharacterClass::Warrior);
	}
	if (AuraAIController && AuraAIController->GetBlackboardComponent())
	{
		AuraAIController->GetBlackboardComponent()->SetValueAsBool(FName("IsDead"), false);
		AuraAIController->GetBlackboardComponent()->SetValueAsBool(FName("HitReacting"), false);
		AuraAIController->GetBlackboardComponent()->SetValueAsBool(FName("Stunned"), false);
		AuraAIController->GetBlackboardComponent()->ClearValue(FName("TargetToFollow"));
	}
}

void AAuraEnemy::CapturePoolDefaults()
{
	if (bPoolDefaultsCaptured)
	{
		return;
	}

	if (USkeletalMeshComponent* CharacterMesh = GetMesh())
	{
		InitialMeshRelativeTransform = CharacterMesh->GetRelativeTransform();
		InitialAnimClass = CharacterMesh->GetAnimClass();
		InitialMeshCollisionResponses = CharacterMesh->GetCollisionResponseToChannels();
		InitialMeshCollisionEnabled = CharacterMesh->GetCollisionEnabled();
		for (int32 MaterialIndex = 0; MaterialIndex < CharacterMesh->GetNumMaterials(); ++MaterialIndex)
		{
			InitialMeshMaterials.Add(CharacterMesh->GetMaterial(MaterialIndex));
		}
	}
	if (Weapon)
	{
		InitialWeaponRelativeTransform = Weapon->GetRelativeTransform();
		InitialWeaponAttachSocket = Weapon->GetAttachSocketName();
		for (int32 MaterialIndex = 0; MaterialIndex < Weapon->GetNumMaterials(); ++MaterialIndex)
		{
			InitialWeaponMaterials.Add(Weapon->GetMaterial(MaterialIndex));
		}
	}
	bPoolDefaultsCaptured = true;
}

void AAuraEnemy::ResetNativePoolState()
{
	CapturePoolDefaults();
	GetWorldTimerManager().ClearTimer(StunAnimationTimer);
	StopAnimMontage();
	bDead = false;
	bHitReacting = false;
	bStunned = false;

	// Death is multicast, so every client disables its local capsule. Pool
	// activation must restore collision and movement on every machine as well,
	// not only on the authoritative server that calls ActivateFromPool().
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->SetActive(true);
		Movement->StopMovementImmediately();
		Movement->CurrentRootMotion = FRootMotionSourceGroup();
		Movement->ServerCorrectionRootMotion = FRootMotionSourceGroup();
		Movement->RootMotionParams.Clear();
		Movement->AnimRootMotionVelocity = FVector::ZeroVector;
		Movement->SetMovementMode(MOVE_Walking);
		if (GetNetMode() == NM_Client)
		{
			Movement->ResetPredictionData_Client();
			Movement->bNetworkSmoothingComplete = true;
		}
	}

	if (USkeletalMeshComponent* CharacterMesh = GetMesh())
	{
		CharacterMesh->SetAllBodiesSimulatePhysics(false);
		CharacterMesh->SetSimulatePhysics(false);
		CharacterMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
		CharacterMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		CharacterMesh->SetEnableGravity(false);
		CharacterMesh->SetRelativeTransform(InitialMeshRelativeTransform);
		CharacterMesh->SetCollisionResponseToChannels(InitialMeshCollisionResponses);
		CharacterMesh->SetCollisionEnabled(InitialMeshCollisionEnabled);
		for (int32 MaterialIndex = 0; MaterialIndex < InitialMeshMaterials.Num(); ++MaterialIndex)
		{
			CharacterMesh->SetMaterial(MaterialIndex, InitialMeshMaterials[MaterialIndex]);
		}
		if (InitialAnimClass)
		{
			CharacterMesh->SetAnimInstanceClass(InitialAnimClass);
		}
		CharacterMesh->InitAnim(true);
		if (UAnimInstance* AnimInstance = CharacterMesh->GetAnimInstance())
		{
			AnimInstance->SetRootMotionMode(InitialRootMotionMode);
		}
		CharacterMesh->SetVisibility(true, true);
	}

	if (Weapon)
	{
		Weapon->SetAllBodiesSimulatePhysics(false);
		Weapon->SetSimulatePhysics(false);
		Weapon->SetPhysicsLinearVelocity(FVector::ZeroVector);
		Weapon->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		Weapon->SetEnableGravity(false);
		Weapon->AttachToComponent(GetMesh(), FAttachmentTransformRules::KeepRelativeTransform, InitialWeaponAttachSocket);
		Weapon->SetRelativeTransform(InitialWeaponRelativeTransform);
		Weapon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		for (int32 MaterialIndex = 0; MaterialIndex < InitialWeaponMaterials.Num(); ++MaterialIndex)
		{
			Weapon->SetMaterial(MaterialIndex, InitialWeaponMaterials[MaterialIndex]);
		}
		Weapon->SetVisibility(true, true);
	}

	if (HealthBar)
	{
		HealthBar->SetVisibility(true);
	}
	GetCharacterMovement()->bUseControllerDesiredRotation = true;
	GetCharacterMovement()->bOrientRotationToMovement = false;
}

void AAuraEnemy::DeactivateToPool()
{
	if (!bPoolManaged)
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(PoolReturnTimer);
	GetWorldTimerManager().ClearTimer(StunAnimationTimer);
	OnEnemyDyingDelegate.Clear();
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetMesh()->SetSimulatePhysics(false);
	GetMesh()->SetEnableGravity(false);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	if (AuraAIController)
	{
		AuraAIController->StopMovement();
		if (AuraAIController->GetBlackboardComponent())
		{
			AuraAIController->GetBlackboardComponent()->ClearValue(FName("TargetToFollow"));
			AuraAIController->GetBlackboardComponent()->SetValueAsBool(FName("IsDead"), true);
		}
	}
	SpawnInstanceId.Invalidate();
	SpawnerId.Invalidate();
	SetPoolState(EEnemyPoolState::Inactive);
}

void AAuraEnemy::SetPoolIdentity(const FGuid& InSpawnInstanceId, const FGuid& InSpawnerId)
{
	SpawnInstanceId = InSpawnInstanceId;
	SpawnerId = InSpawnerId;
	ForceNetUpdate();
}

void AAuraEnemy::SetPoolLevel(int32 InLevel)
{
	Level = FMath::Max(1, InLevel);
	ForceNetUpdate();
}

float AAuraEnemy::GetCurrentHealth() const
{
	if (const UAuraAttributeSet* AuraAS = Cast<UAuraAttributeSet>(AttributeSet))
	{
		return AuraAS->GetHealth();
	}
	return 0.f;
}

void AAuraEnemy::RestoreHealth(float InHealth)
{
	if (UAuraAttributeSet* AuraAS = Cast<UAuraAttributeSet>(AttributeSet))
	{
		const float ClampedHealth = FMath::Clamp(InHealth, 0.f, AuraAS->GetMaxHealth());
		AbilitySystemComponent->SetNumericAttributeBase(UAuraAttributeSet::GetHealthAttribute(), ClampedHealth);
	}
}

void AAuraEnemy::SetPoolState(EEnemyPoolState NewState)
{
	if (PoolState != NewState)
	{
		PoolState = NewState;
		OnRep_PoolState();
		ForceNetUpdate();
	}
}

void AAuraEnemy::OnRep_PoolState()
{
	const bool bActive = PoolState != EEnemyPoolState::Inactive;
	UpdateClientCrowdRegistration(PoolState == EEnemyPoolState::Active);
	if (PoolState == EEnemyPoolState::Active)
	{
		ResetPoolBlueprintState();
		ResetNativePoolState();
	}
	SetActorHiddenInGame(!bActive);
	SetActorEnableCollision(bActive);
}

FVector AAuraEnemy::GetCrowdAgentLocation() const
{
	return GetActorLocation();
}

FVector AAuraEnemy::GetCrowdAgentVelocity() const
{
	return GetVelocity();
}

void AAuraEnemy::GetCrowdAgentCollisions(float& CylinderRadius, float& CylinderHalfHeight) const
{
	GetCapsuleComponent()->GetScaledCapsuleSize(CylinderRadius, CylinderHalfHeight);
}

float AAuraEnemy::GetCrowdAgentMaxSpeed() const
{
	return GetCharacterMovement()->GetMaxSpeed();
}

void AAuraEnemy::UpdateClientCrowdRegistration(bool bShouldRegister)
{
	if (GetNetMode() != NM_Client || bClientCrowdRegistered == bShouldRegister)
	{
		return;
	}

	if (UCrowdManager* CrowdManager = UCrowdManager::GetCurrent(GetWorld()))
	{
		if (bShouldRegister)
		{
			CrowdManager->RegisterAgent(this);
		}
		else
		{
			CrowdManager->UnregisterAgent(this);
		}
		bClientCrowdRegistered = bShouldRegister;
	}
}

void AAuraEnemy::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAuraEnemy, PoolState);
	DOREPLIFETIME(AAuraEnemy, SpawnInstanceId);
	DOREPLIFETIME(AAuraEnemy, SpawnerId);
	DOREPLIFETIME(AAuraEnemy, Level);
}

void AAuraEnemy::SetActorHighlight(bool IsHighlight)
{
	GetMesh()->SetRenderCustomDepth(IsHighlight);
	Weapon->SetRenderCustomDepth(IsHighlight);
}

int32 AAuraEnemy::GetLevel_Implementation()
{
	return Level;
}

void AAuraEnemy::SetCombatTarget_Implementation(AActor* InCombatTarget)
{
	CombatTarget = InCombatTarget;
}

AActor* AAuraEnemy::GetCombatTarget_Implementation() const
{
	return CombatTarget;
}

void AAuraEnemy::HandleTargetActorInvalidated(AActor* InvalidTarget)
{
	UBlackboardComponent* BlackboardComponent = AuraAIController
		? AuraAIController->GetBlackboardComponent() : nullptr;
	const bool bHasInvalidCombatTarget = CombatTarget == InvalidTarget;
	const bool bHasInvalidBlackboardTarget = BlackboardComponent
		&& BlackboardComponent->GetValueAsObject(FName("TargetToFollow")) == InvalidTarget;
	if (!bHasInvalidCombatTarget && !bHasInvalidBlackboardTarget)
	{
		return;
	}

	CombatTarget = nullptr;
	FGameplayTagContainer AttackTags;
	AttackTags.AddTag(FAuraGameplayTags::Get().Abilities_Attack);
	AbilitySystemComponent->CancelAbilities(&AttackTags);

	if (AuraAIController)
	{
		AuraAIController->StopMovement();
		if (BlackboardComponent)
		{
			BlackboardComponent->ClearValue(FName("TargetToFollow"));
		}
		if (UBrainComponent* BrainComponent = AuraAIController->GetBrainComponent())
		{
			BrainComponent->RestartLogic();
		}
	}
}

void AAuraEnemy::InitAbilityActorInfo()
{
	AbilitySystemComponent->InitAbilityActorInfo(this, this);
	Cast<UAuraAbilitySystemComponent>(AbilitySystemComponent)->AbilityActorInfoSet();
	
	if (HasAuthority())
	{
		InitializeDefaultAttributes();
	}
}

void AAuraEnemy::InitializeDefaultAttributes() const
{
	UAuraAbilitySystemLibrary::InitializeDefaultAttributes(this, CharacterClass, Level, AbilitySystemComponent);
}
