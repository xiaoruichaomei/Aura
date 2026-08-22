


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
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "Subsystem/AuraEnemyPoolSubsystem.h"

AAuraEnemy::AAuraEnemy()
{
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
	bHitReacting = NewCount > 0;
	UpdateMovementSpeed();
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
			AbilitySystemComponent->CancelAbilities(&TagsToCancel);
		}

		// 播放循环眩晕蒙太奇（各端本地播放；标签事件已复制，动画不用再单独复制）
		if (StunMontage && !GetMesh()->GetAnimInstance()->Montage_IsPlaying(StunMontage))
		{
			PlayAnimMontage(StunMontage);
		}
	}
	else
	{
		if (StunMontage)
		{
			StopAnimMontage(StunMontage);
		}
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
				World->GetTimerManager().SetTimer(PoolReturnTimer, [Pool, this]()
				{
					Pool->ReleaseEnemy(this);
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
	AbilitySystemComponent->CancelAllAbilities();
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

void AAuraEnemy::DeactivateToPool()
{
	if (!bPoolManaged)
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(PoolReturnTimer);
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
	SetActorHiddenInGame(!bActive);
	SetActorEnableCollision(bActive);
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
