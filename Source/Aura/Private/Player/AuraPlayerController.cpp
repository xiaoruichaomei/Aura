


#include "Player/AuraPlayerController.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AI/EQS/AuraAutoMoveEQS.h"
#include "AITypes.h"
#include "AuraGameplayTags.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/HitResult.h"
#include "GameFramework/Pawn.h"
#include "Interface/EnemyInterface.h"
#include "Interface/CombatInterface.h"
#include "GameplayTagContainer.h"
#include "NavigationPath.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "AbilitySystem/AuraAbilitySystemComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQueryOption.h"
#include "Input/AuraInputComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Game/AuraGameModeBase.h"
#include "NiagaraFunctionLibrary.h"
#include "UI/Widget/DamageTextComponent.h"
#include "Actor/MagicCircle.h"
#include "UObject/ConstructorHelpers.h"
#include "Materials/MaterialInterface.h"
#include "Navigation/AuraCrowdFollowingComponent.h"
#include "Engine/OverlapResult.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraAutoMove, Log, All);

AAuraPlayerController::AAuraPlayerController()
{
	bReplicates = true;	// 当服务器上的actor发生变化时，服务器上的变化会复制发送到所有连接的客户端
	
	CrowdPathFollowingComponent = CreateDefaultSubobject<UAuraCrowdFollowingComponent>("CrowdPathFollowingComponent");
	CrowdPathFollowingComponent->SetCrowdAnticipateTurns(true, false);
	CrowdPathFollowingComponent->SetCrowdObstacleAvoidance(true, false);
	CrowdPathFollowingComponent->SetCrowdSeparation(false, false);
	CrowdPathFollowingComponent->SetCrowdOptimizeVisibility(true, false);
	CrowdPathFollowingComponent->SetCrowdOptimizeTopology(true, false);
	CrowdPathFollowingComponent->SetCrowdSlowdownAtGoal(false, false);
	CrowdPathFollowingComponent->SetCrowdSeparationWeight(2.f, false);
	CrowdPathFollowingComponent->SetCrowdCollisionQueryRange(400.f, false);
	CrowdPathFollowingComponent->SetCrowdPathOptimizationRange(1000.f, false);
	CrowdPathFollowingComponent->SetCrowdAvoidanceQuality(ECrowdAvoidanceQuality::Good, false);
	CrowdPathFollowingComponent->SetCrowdAvoidanceRangeMultiplier(1.f, false);
	CrowdPathFollowingComponent->SetCrowdAffectFallingVelocity(false);
	static ConstructorHelpers::FClassFinder<AMagicCircle> MagicCircleBlueprint(TEXT("/Game/Blueprints/Actor/MagicCircle/BP_MagicCircle"));
	if (MagicCircleBlueprint.Class)
	{
		MagicCircleClass = MagicCircleBlueprint.Class;
	}
}

void AAuraPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	CursorTrace();
	UpdateMagicCircleLocation();
	AutoRun(DeltaSeconds);
	ApplyClientAutoMoveSteering();
	UpdateAutoMoveFacing(DeltaSeconds);
}

void AAuraPlayerController::ShowDamageNumber_Implementation(float DamageAmount, ACharacter* TargetCharacter, bool bBlockedHit, bool bCriticalHit)
{
	if (IsValid(TargetCharacter) && DamageTextComponentClass && IsLocalController())
	{
		UDamageTextComponent* DamageText = NewObject<UDamageTextComponent>(TargetCharacter, DamageTextComponentClass);
		DamageText->RegisterComponent();
		DamageText->AttachToComponent(TargetCharacter->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
		DamageText->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		DamageText->SetDamageText(DamageAmount, bBlockedHit, bCriticalHit);
	}
}

void AAuraPlayerController::BeginPlay()
{
	Super::BeginPlay();
	// Older Blueprint defaults can retain zero after native property changes. A zero
	// radius turns valid path completions at the destination into recovery requests.
	AutoRunAcceptanceRadius = FMath::Max(AutoRunAcceptanceRadius, 50.f);
	CrowdPathFollowingComponent->SetAvoidanceRadiusPadding(AutoMoveCrowdRadiusPadding);
	CrowdPathFollowingComponent->Initialize();
	InitializeAutoMoveQueries();
	const UPathFollowingComponent* DiscoveredPathFollowing = FindComponentByClass<UPathFollowingComponent>();
	UE_LOG(LogAuraAutoMove, Display,
		TEXT("Initialize component=%s discovered=%s same=%d simulation=%d avoidance=%d"),
		*GetNameSafe(CrowdPathFollowingComponent), *GetNameSafe(DiscoveredPathFollowing),
		DiscoveredPathFollowing == CrowdPathFollowingComponent,
		static_cast<uint8>(CrowdPathFollowingComponent->GetCrowdSimulationState()),
		CrowdPathFollowingComponent->IsCrowdObstacleAvoidanceActive());

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		Subsystem->AddMappingContext(AuraContext, 0);
	}
	
	bShowMouseCursor = true;
	DefaultMouseCursor = EMouseCursor::Default;
	
	FInputModeGameAndUI InputModeData;
	InputModeData.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputModeData.SetHideCursorDuringCapture(false);
	SetInputMode(InputModeData);
	
	// 初始化固定相机：直接用玩家身上的相机位置
	UpdateFixedCameraToPlayer();
}

void AAuraPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	AbortAutoMoveQuery();
	Super::EndPlay(EndPlayReason);
}

void AAuraPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	
	UAuraInputComponent* AuraInputComponent = CastChecked<UAuraInputComponent>(InputComponent);

	AuraInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AAuraPlayerController::Move);
	AuraInputComponent->BindAction(CameraSnapAction, ETriggerEvent::Triggered, this, &AAuraPlayerController::SnapCameraToPlayer);
	AuraInputComponent->BindAbilityActions(InputConfig, this, &ThisClass::AbilityInputTagPressed, &ThisClass::AbilityInputTagReleased, &ThisClass::AbilityInputTagHeld);
}

void AAuraPlayerController::Move(const FInputActionValue& InputActionValue)
{
	if (IsInputBlocked())
	{
		return;
	}
	const FVector2D InputAxisVector = InputActionValue.Get<FVector2D>();
	if (!InputAxisVector.IsNearlyZero())
	{
		RequestStopAutoRun(false);
	}
	const FRotator Rotation = GetControlRotation();
	const FRotator YawRotation(0.f, Rotation.Yaw, 0.f);
	
	const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
	
	if (APawn* ControlledPawn = GetPawn<APawn>())
	{
		ControlledPawn->AddMovementInput(ForwardDirection, InputAxisVector.Y);
		ControlledPawn->AddMovementInput(RightDirection, InputAxisVector.X);
	}
}

void AAuraPlayerController::CursorTrace()
{
	if (MagicCircle)
	{
		if (ThisActor)
		{
			ThisActor->SetActorHighlight(false);
			ThisActor = nullptr;
		}
	}
	GetHitResultUnderCursor(ECC_Visibility, false, CursorHit);
	if (!CursorHit.bBlockingHit)
	{
		return;
	}
	
	LastActor = ThisActor;
	ThisActor = Cast<IEnemyInterface>(CursorHit.GetActor());
	
	if (LastActor != ThisActor)
	{
		if (LastActor)
		{
			LastActor->SetActorHighlight(false);
		}
		if (ThisActor)
		{
			ThisActor->SetActorHighlight(true);
		}
	}
}

void AAuraPlayerController::ShowMagicCircle(UMaterialInterface* DecalMaterial)
{
	if (!IsLocalController()) return;
	HideMagicCircle();
	if (!MagicCircleClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("Magic circle Blueprint class is unavailable; using the native fallback."));
		MagicCircleClass = AMagicCircle::StaticClass();
	}
	MagicCircle = GetWorld()->SpawnActor<AMagicCircle>(MagicCircleClass, FVector::ZeroVector, FRotator::ZeroRotator);
	if (MagicCircle)
	{
		MagicCircle->SetDecalMaterial(DecalMaterial);
		UpdateMagicCircleLocation();
	}
}

void AAuraPlayerController::HideMagicCircle()
{
	if (IsValid(MagicCircle)) MagicCircle->Destroy();
	MagicCircle = nullptr;
	bHasMagicCircleLocation = false;
}

bool AAuraPlayerController::GetMagicCircleLocation(FVector& OutLocation) const
{
	if (!bHasMagicCircleLocation) return false;
	OutLocation = MagicCircleLocation;
	return true;
}

void AAuraPlayerController::UpdateMagicCircleLocation()
{
	if (!MagicCircle || !IsLocalController()) return;
	FVector RayOrigin;
	FVector RayDirection;
	if (!DeprojectMousePositionToWorld(RayOrigin, RayDirection)) return;

	FCollisionObjectQueryParams GroundObjects;
	GroundObjects.AddObjectTypesToQuery(ECC_WorldStatic);
	GroundObjects.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MagicCircleGround), false, GetPawn());
	TArray<FHitResult> Hits;
	GetWorld()->LineTraceMultiByObjectType(Hits, RayOrigin, RayOrigin + RayDirection * 50000.f, GroundObjects, QueryParams);
	const FHitResult* GroundHit = Hits.FindByPredicate([](const FHitResult& Hit)
	{
		return Hit.bBlockingHit && Hit.ImpactNormal.Z >= 0.5f;
	});
	if (!GroundHit) return;

	MagicCircleLocation = GroundHit->ImpactPoint + FVector(0.f, 0.f, 2.f);
	bHasMagicCircleLocation = true;
	MagicCircle->SetActorLocation(MagicCircleLocation);
	ServerSetMagicCircleLocation(MagicCircleLocation);
}

void AAuraPlayerController::ServerSetMagicCircleLocation_Implementation(FVector_NetQuantize InLocation)
{
	if (!FVector(InLocation).ContainsNaN())
	{
		MagicCircleLocation = InLocation;
		bHasMagicCircleLocation = true;
	}
}

void AAuraPlayerController::ServerStartAutoMove_Implementation(FVector_NetQuantize Destination)
{
	StartAuthoritativeAutoMove(FVector(Destination));
}

void AAuraPlayerController::ServerStopAutoMove_Implementation(bool bStopImmediately)
{
	StopAutoRun(bStopImmediately);
}

void AAuraPlayerController::ClientSetAutoMoveActive_Implementation(bool bActive)
{
	bServerAutoMoveRequested = bActive;
	if (!bActive)
	{
		AutoMoveSteeringVelocity = FVector::ZeroVector;
	}
}

void AAuraPlayerController::ClientSetAutoMoveSteering_Implementation(FVector_NetQuantize10 SteeringVelocity)
{
	AutoMoveSteeringVelocity = FVector(SteeringVelocity);
}

void AAuraPlayerController::ServerTravelToLoadMenu_Implementation()
{
	if (!HasAuthority() || !GetWorld())
	{
		return;
	}

	if (AAuraGameModeBase* GameMode = GetWorld()->GetAuthGameMode<AAuraGameModeBase>())
	{
		GameMode->SaveAndReturnToMainMenu();
	}
}

void AAuraPlayerController::OnRep_Pawn()
{
	Super::OnRep_Pawn();

	// AController::OnRep_Pawn does not broadcast OnNewPawn on clients, while
	// UPathFollowingComponent relies on that delegate to refresh its movement
	// component. Reinitialize explicitly so initial possession and respawn both
	// bind crowd path following to the replicated Pawn instead of the old one.
	bTargeting = false;
	FollowTime = 0.f;
	StopAutoRun(true);
	ConsecutiveRecoveryFailures = 0;
	FailedRecoveryDirections.Reset();
	if (CrowdPathFollowingComponent && GetPawn())
	{
		CrowdPathFollowingComponent->Initialize();
		UE_LOG(LogAuraAutoMove, Display, TEXT("Pawn replicated: rebound path following to %s movement=%s"),
			*GetNameSafe(GetPawn()), *GetNameSafe(GetPawn()->GetMovementComponent()));
	}

	AuraAbilitySystemComponent = nullptr;
	UpdateFixedCameraToPlayer();
}

bool AAuraPlayerController::GetBeamCursorLocation(FVector& OutLocation) const
{
	if (IsLocalController() && CursorHit.bBlockingHit)
	{
		OutLocation = CursorHit.ImpactPoint;
		return true;
	}
	if (bHasBeamCursorLocation)
	{
		OutLocation = BeamCursorLocation;
		return true;
	}
	return false;
}

void AAuraPlayerController::SubmitBeamCursorLocation(const FVector& InLocation)
{
	if (InLocation.ContainsNaN())
	{
		return;
	}

	const bool bLocationChanged = !bHasBeamCursorLocation || !BeamCursorLocation.Equals(InLocation, 1.f);
	BeamCursorLocation = InLocation;
	bHasBeamCursorLocation = true;

	if (bLocationChanged && !HasAuthority())
	{
		ServerSetBeamCursorLocation(InLocation);
	}
}

void AAuraPlayerController::ServerSetBeamCursorLocation_Implementation(FVector_NetQuantize InLocation)
{
	if (!FVector(InLocation).ContainsNaN())
	{
		BeamCursorLocation = InLocation;
		bHasBeamCursorLocation = true;
	}
}

void AAuraPlayerController::MulticastSpawnClickEffect_Implementation(const FVector& CursorLocation)
{
	if (ClickNiagaraSystem)
	{
		// bAutoDestroy 默认为 true：特效播完自动销毁
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			this, ClickNiagaraSystem, CursorLocation, FRotator::ZeroRotator, FVector::OneVector,
			true, true, ENCPoolMethod::AutoRelease);
	}
}

bool AAuraPlayerController::IsInputBlocked() const
{
	if (APawn* ControlledPawn = GetPawn())
	{
		if (ControlledPawn->Implements<UCombatInterface>() && ICombatInterface::Execute_IsDead(ControlledPawn))
		{
			return true;
		}
		if (UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(ControlledPawn))
		{
			return ASC->HasMatchingGameplayTag(FAuraGameplayTags::Get().Player_Block);
		}
	}
	return false;
}

void AAuraPlayerController::AbilityInputTagPressed(FGameplayTag InputTag)
{
	if (const APawn* ControlledPawn = GetPawn(); ControlledPawn &&
		ControlledPawn->Implements<UCombatInterface>() && ICombatInterface::Execute_IsDead(ControlledPawn))
	{
		return;
	}
	if (!InputTag.MatchesTagExact(FAuraGameplayTags::Get().Input_RMB) && MagicCircle)
	{
		FVector TargetLocation;
		if (GetMagicCircleLocation(TargetLocation) && GetASC())
		{
			GetASC()->ConfirmArcaneShardsTarget(TargetLocation);
		}
		HideMagicCircle();
		return;
	}
	if (!InputTag.MatchesTagExact(FAuraGameplayTags::Get().Input_RMB) && GetASC())
	{
		GetASC()->AbilityInputTagPressed(InputTag);
		GetASC()->AbilityInputTagHeld(InputTag);
	}
	if (InputTag.MatchesTagExact(FAuraGameplayTags::Get().Input_RMB))
	{
		if (IsInputBlocked())
		{
			return;
		}

		bTargeting = ThisActor ? true : false;
		RequestStopAutoRun(false);

		// 点击地面（移动或施法）时在所有端生成点击特效
		if (CursorHit.bBlockingHit)
		{
			MulticastSpawnClickEffect(CursorHit.ImpactPoint);
		}
	}
}

void AAuraPlayerController::AbilityInputTagReleased(FGameplayTag InputTag)
{
	if (const APawn* ControlledPawn = GetPawn(); ControlledPawn &&
		ControlledPawn->Implements<UCombatInterface>() && ICombatInterface::Execute_IsDead(ControlledPawn))
	{
		return;
	}
	if (!InputTag.MatchesTagExact(FAuraGameplayTags::Get().Input_RMB))
	{
		if (GetASC())
		{
			GetASC()->AbilityInputTagReleased(InputTag);
		}
		return;
	}
	
	if (bTargeting)
	{
		if (GetASC())
		{
			GetASC()->AbilityInputTagReleased(InputTag);
		}
	}
	else
	{
		if (IsInputBlocked())
		{
			return;
		}

		APawn* ControlledPawn = GetPawn();
		if (ControlledPawn && FollowTime <= ShortPressThreshold)
		{
			RequestAutoMove(CachedDestination);
		}
		FollowTime = 0.f;
	}
}

void AAuraPlayerController::AbilityInputTagHeld(FGameplayTag InputTag)
{
	if (const APawn* ControlledPawn = GetPawn(); ControlledPawn &&
		ControlledPawn->Implements<UCombatInterface>() && ICombatInterface::Execute_IsDead(ControlledPawn))
	{
		return;
	}
	if (!InputTag.MatchesTagExact(FAuraGameplayTags::Get().Input_RMB))
	{
		if (GetASC())
		{
			GetASC()->AbilityInputTagHeld(InputTag);
		}
		return;
	}
	
	if (bTargeting)
	{
		if (GetASC())
		{
			GetASC()->AbilityInputTagHeld(InputTag);
		}
	}
	else
	{
		if (IsInputBlocked())
		{
			return;
		}

		FollowTime += GetWorld()->GetDeltaSeconds();

		if (CursorHit.bBlockingHit)
		{
			CachedDestination = CursorHit.ImpactPoint;
		}

		APawn* ControlledPawn = GetPawn();
		if (ControlledPawn)
		{
			const FVector WorldDirection = (CachedDestination - ControlledPawn->GetActorLocation()).GetSafeNormal();
			ControlledPawn->AddMovementInput(WorldDirection);
		}
	}
}

void AAuraPlayerController::HandleControlledPawnDeath()
{
	bTargeting = false;
	FollowTime = 0.f;
	HideMagicCircle();
	StopAutoRun(true);
}

void AAuraPlayerController::AutoRun(float DeltaSeconds)
{
	AutoMoveReplanCooldownRemaining = FMath::Max(0.f, AutoMoveReplanCooldownRemaining - DeltaSeconds);
	if (AutoMoveState == EAutoMoveState::Idle ||
		AutoMoveState == EAutoMoveState::ResolvingGoal ||
		AutoMoveState == EAutoMoveState::ResolvingRecovery)
	{
		return;
	}
	if (IsInputBlocked())
	{
		StopAutoRun(true);
		return;
	}

	const APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		StopAutoRun(false);
		return;
	}

	const float DistanceToFinalDestination = FVector::Dist2D(ControlledPawn->GetActorLocation(), ResolvedDestination);
	if (AutoMoveState == EAutoMoveState::Moving && DistanceToFinalDestination <= AutoRunAcceptanceRadius)
	{
		StopAutoRun(true);
		return;
	}

	if (AutoMoveState == EAutoMoveState::Recovering)
	{
		const bool bReachedRecoveryPoint = FVector::Dist2D(
			ControlledPawn->GetActorLocation(), AutoRunMoveDestination) <= AutoMoveRecoveryAcceptanceRadius;
		if (bReachedRecoveryPoint)
		{
			ConsecutiveRecoveryFailures = 0;
			FailedRecoveryDirections.Reset();
			if (!RequestAutoRunPath(ResolvedDestination, EAutoMoveState::Moving))
			{
				StartAutoMoveGoalQuery();
			}
			return;
		}
	}

	UpdateAutoMoveProgress(DeltaSeconds);
}

void AAuraPlayerController::ApplyClientAutoMoveSteering()
{
	if (HasAuthority() || !IsLocalController() || !bServerAutoMoveRequested || IsInputBlocked())
	{
		return;
	}

	APawn* ControlledPawn = GetPawn();
	const float SteeringSpeed = AutoMoveSteeringVelocity.Size2D();
	if (!ControlledPawn || SteeringSpeed <= UE_KINDA_SMALL_NUMBER)
	{
		return;
	}

	float MaxSpeed = SteeringSpeed;
	if (const ACharacter* ControlledCharacter = Cast<ACharacter>(ControlledPawn))
	{
		MaxSpeed = FMath::Max(1.f, ControlledCharacter->GetCharacterMovement()->GetMaxSpeed());
	}
	ControlledPawn->AddMovementInput(
		AutoMoveSteeringVelocity.GetSafeNormal2D(), FMath::Clamp(SteeringSpeed / MaxSpeed, 0.f, 1.f));
}

void AAuraPlayerController::UpdateAutoMoveFacing(float DeltaSeconds)
{
	APawn* ControlledPawn = GetPawn();
	if (!bServerAutoMoveRequested || !ControlledPawn)
	{
		return;
	}

	FVector MoveDirection = ControlledPawn->GetVelocity().GetSafeNormal2D();
	if (MoveDirection.IsNearlyZero())
	{
		MoveDirection = AutoMoveSteeringVelocity.GetSafeNormal2D();
	}
	if (MoveDirection.IsNearlyZero())
	{
		return;
	}

	float TurnRate = 400.f;
	if (const ACharacter* ControlledCharacter = Cast<ACharacter>(ControlledPawn))
	{
		TurnRate = FMath::Max(1.f, ControlledCharacter->GetCharacterMovement()->RotationRate.Yaw);
	}
	const FRotator CurrentRotation(0.f, ControlledPawn->GetActorRotation().Yaw, 0.f);
	const FRotator DesiredRotation(0.f, MoveDirection.Rotation().Yaw, 0.f);
	ControlledPawn->SetActorRotation(FMath::RInterpConstantTo(
		CurrentRotation, DesiredRotation, DeltaSeconds, TurnRate));
}

void AAuraPlayerController::HandleCrowdSteeringVelocity(const FVector& SteeringVelocity)
{
	if (!HasAuthority() || IsLocalController() || !bServerAutoMoveRequested || !GetWorld())
	{
		return;
	}

	const float CurrentTime = GetWorld()->GetTimeSeconds();
	const bool bStopped = SteeringVelocity.IsNearlyZero();
	const bool bWasStopped = LastSentAutoMoveSteeringVelocity.IsNearlyZero();
	const FVector NewDirection = SteeringVelocity.GetSafeNormal2D();
	const FVector PreviousDirection = LastSentAutoMoveSteeringVelocity.GetSafeNormal2D();
	const bool bSharpTurn = !bStopped && !bWasStopped &&
		FVector::DotProduct(NewDirection, PreviousDirection) < 0.94f;
	if (CurrentTime < NextAutoMoveSteeringSendTime && bStopped == bWasStopped && !bSharpTurn)
	{
		return;
	}

	LastSentAutoMoveSteeringVelocity = SteeringVelocity;
	AutoMoveSteeringVelocity = SteeringVelocity;
	NextAutoMoveSteeringSendTime = CurrentTime + 0.05f;
	ClientSetAutoMoveSteering(SteeringVelocity);
}

void AAuraPlayerController::RequestAutoMove(const FVector& Destination)
{
	if (Destination.ContainsNaN() || !GetPawn())
	{
		return;
	}

	if (HasAuthority())
	{
		StartAuthoritativeAutoMove(Destination);
		return;
	}

	// CharacterMovement is client-predicted, but PathFollowing/Crowd movement is
	// authority-driven. Let the server own the complete EQS and recovery state.
	bServerAutoMoveRequested = true;
	ServerStartAutoMove(Destination);
}

void AAuraPlayerController::StartAuthoritativeAutoMove(const FVector& Destination)
{
	APawn* ControlledPawn = GetPawn();
	if (!HasAuthority() || Destination.ContainsNaN() || !ControlledPawn || IsInputBlocked())
	{
		bServerAutoMoveRequested = false;
		ClientSetAutoMoveActive(false);
		return;
	}

	StopAutoRun(false);
	UNavigationPath* NavPath = UNavigationSystemV1::FindPathToLocationSynchronously(
		this, ControlledPawn->GetActorLocation(), Destination);
	if (!NavPath || !NavPath->IsValid() || NavPath->IsPartial() || NavPath->PathPoints.Num() == 0)
	{
		bServerAutoMoveRequested = false;
		ClientSetAutoMoveActive(false);
		UE_LOG(LogAuraAutoMove, Warning, TEXT("Server rejected auto move from=%s to=%s"),
			*ControlledPawn->GetActorLocation().ToCompactString(), *Destination.ToCompactString());
		return;
	}

	bServerAutoMoveRequested = true;
	ClientSetAutoMoveActive(true);
	ConsecutiveRecoveryFailures = 0;
	FailedRecoveryDirections.Reset();
	RequestedDestination = NavPath->PathPoints.Last();
	StartAutoMoveGoalQuery();
}

void AAuraPlayerController::RequestStopAutoRun(bool bStopImmediately)
{
	const bool bNotifyServer = !HasAuthority() && bServerAutoMoveRequested;
	StopAutoRun(bStopImmediately);
	if (bNotifyServer)
	{
		ServerStopAutoMove(bStopImmediately);
	}
}

void AAuraPlayerController::StopAutoRun(bool bStopImmediately)
{
	if (AutoMoveState != EAutoMoveState::Idle)
	{
		UE_LOG(LogAuraAutoMove, Log, TEXT("Stop state=%d immediate=%d location=%s destination=%s"),
			static_cast<uint8>(AutoMoveState), bStopImmediately,
			GetPawn() ? *GetPawn()->GetActorLocation().ToCompactString() : TEXT("NoPawn"),
			*ResolvedDestination.ToCompactString());
	}
	AbortAutoMoveQuery();
	bServerAutoMoveRequested = false;
	AutoMoveSteeringVelocity = FVector::ZeroVector;
	LastSentAutoMoveSteeringVelocity = FVector::ZeroVector;
	NextAutoMoveSteeringSendTime = 0.f;
	if (HasAuthority())
	{
		ClientSetAutoMoveActive(false);
	}
	AutoMoveState = EAutoMoveState::Idle;
	AutoMoveReplanCooldownRemaining = 0.f;
	ResetAutoMoveProgress();
	StopMovement();

	if (bStopImmediately)
	{
		if (APawn* ControlledPawn = GetPawn())
		{
			if (UPawnMovementComponent* MovementComponent = ControlledPawn->GetMovementComponent())
			{
				MovementComponent->StopMovementImmediately();
			}
		}
	}
}

bool AAuraPlayerController::RequestAutoRunPath(const FVector& Destination, EAutoMoveState MoveState)
{
	APawn* ControlledPawn = GetPawn();
	if (!HasAuthority() || !ControlledPawn || !CrowdPathFollowingComponent)
	{
		return false;
	}

	UNavigationSystemV1* NavSystem = UNavigationSystemV1::GetCurrent(GetWorld());
	const ANavigationData* NavData = NavSystem
		? NavSystem->GetNavDataForProps(GetNavAgentPropertiesRef(), GetNavAgentLocation())
		: nullptr;
	if (!NavSystem || !NavData)
	{
		UE_LOG(LogAuraAutoMove, Warning, TEXT("RequestPath failed: missing navigation system or nav data."));
		return false;
	}

	FPathFindingQuery Query(this, *NavData, GetNavAgentLocation(), Destination);
	FPathFindingResult PathResult = NavSystem->FindPathSync(Query);
	if (!PathResult.IsSuccessful() || PathResult.IsPartial() || !PathResult.Path.IsValid())
	{
		UE_LOG(LogAuraAutoMove, Warning,
			TEXT("RequestPath failed: result=%d partial=%d from=%s to=%s"),
			static_cast<uint8>(PathResult.Result), PathResult.IsPartial(),
			*GetNavAgentLocation().ToCompactString(), *Destination.ToCompactString());
		return false;
	}

	FAIMoveRequest MoveRequest(Destination);
	MoveRequest.SetAcceptanceRadius(AutoRunAcceptanceRadius);
	MoveRequest.SetReachTestIncludesAgentRadius(false);
	const FAIRequestID MoveRequestId = CrowdPathFollowingComponent->RequestMove(MoveRequest, PathResult.Path);
	if (!MoveRequestId.IsValid())
	{
		UE_LOG(LogAuraAutoMove, Warning, TEXT("RequestPath rejected by %s."),
			*GetNameSafe(CrowdPathFollowingComponent));
		return false;
	}

	AutoRunMoveDestination = Destination;
	AutoMoveState = MoveState;
	AutoMoveReplanCooldownRemaining = AutoMoveReplanCooldown;
	ResetAutoMoveProgress();
	UE_LOG(LogAuraAutoMove, Display,
		TEXT("RequestPath accepted id=%u state=%d points=%d status=%d simulation=%d destination=%s"),
		MoveRequestId.GetID(), static_cast<uint8>(MoveState), PathResult.Path->GetPathPoints().Num(),
		static_cast<uint8>(CrowdPathFollowingComponent->GetStatus()),
		static_cast<uint8>(CrowdPathFollowingComponent->GetCrowdSimulationState()),
		*Destination.ToCompactString());
	return true;
}

void AAuraPlayerController::InitializeAutoMoveQueries()
{
	auto BuildQuery = [this](const FName Name, bool bRecoveryQuery)
	{
		UEnvQuery* Query = NewObject<UEnvQuery>(this, Name);
		UEnvQueryOption* Option = NewObject<UEnvQueryOption>(Query);
		UAuraEnvQueryGenerator_AutoMove* Generator = NewObject<UAuraEnvQueryGenerator_AutoMove>(Option);
		UAuraEnvQueryTest_AutoMove* Test = NewObject<UAuraEnvQueryTest_AutoMove>(Option);
		Generator->SetRecoveryQuery(bRecoveryQuery);
		Test->SetRecoveryQuery(bRecoveryQuery);
		Option->Generator = Generator;
		Option->Tests.Add(Test);
		Query->GetOptionsMutable().Add(Option);
		return Query;
	};

	AutoMoveGoalQuery = BuildQuery(TEXT("AutoMoveGoalQuery"), false);
	AutoMoveRecoveryQuery = BuildQuery(TEXT("AutoMoveRecoveryQuery"), true);
}

void AAuraPlayerController::StartAutoMoveGoalQuery()
{
	AbortAutoMoveQuery();
	StopMovement();
	AutoMoveState = EAutoMoveState::ResolvingGoal;
	if (!AutoMoveGoalQuery)
	{
		StopAutoRun(false);
		return;
	}

	FEnvQueryRequest Request(AutoMoveGoalQuery, this);
	ActiveAutoMoveQueryId = Request.Execute(
		EEnvQueryRunMode::SingleResult, this, &AAuraPlayerController::HandleAutoMoveGoalQueryFinished);
	if (ActiveAutoMoveQueryId == INDEX_NONE)
	{
		StopAutoRun(false);
	}
}

void AAuraPlayerController::StartAutoMoveRecoveryQuery()
{
	AbortAutoMoveQuery();
	AutoMoveState = EAutoMoveState::ResolvingRecovery;
	if (!AutoMoveRecoveryQuery)
	{
		if (!RequestAutoRunPath(ResolvedDestination, EAutoMoveState::Moving))
		{
			StartAutoMoveGoalQuery();
		}
		return;
	}

	FEnvQueryRequest Request(AutoMoveRecoveryQuery, this);
	ActiveAutoMoveQueryId = Request.Execute(
		EEnvQueryRunMode::AllMatching, this, &AAuraPlayerController::HandleAutoMoveRecoveryQueryFinished);
	if (ActiveAutoMoveQueryId == INDEX_NONE)
	{
		if (!RequestAutoRunPath(ResolvedDestination, EAutoMoveState::Moving))
		{
			StartAutoMoveGoalQuery();
		}
	}
}

void AAuraPlayerController::AbortAutoMoveQuery()
{
	if (ActiveAutoMoveQueryId == INDEX_NONE)
	{
		return;
	}
	const int32 QueryIdToAbort = ActiveAutoMoveQueryId;
	ActiveAutoMoveQueryId = INDEX_NONE;
	if (UEnvQueryManager* QueryManager = UEnvQueryManager::GetCurrent(GetWorld()))
	{
		QueryManager->AbortQuery(QueryIdToAbort);
	}
}

void AAuraPlayerController::HandleAutoMoveGoalQueryFinished(TSharedPtr<FEnvQueryResult> Result)
{
	if (!Result || Result->QueryID != ActiveAutoMoveQueryId)
	{
		return;
	}
	ActiveAutoMoveQueryId = INDEX_NONE;
	if (!Result->IsSuccessful() || Result->Items.IsEmpty())
	{
		StopAutoRun(false);
		return;
	}

	ResolvedDestination = Result->GetItemAsLocation(0);
	if (const APawn* ControlledPawn = GetPawn(); ControlledPawn &&
		FVector::Dist2D(ControlledPawn->GetActorLocation(), ResolvedDestination) <= AutoRunAcceptanceRadius)
	{
		StopAutoRun(true);
		return;
	}
	if (!RequestAutoRunPath(ResolvedDestination, EAutoMoveState::Moving))
	{
		StartAutoMoveGoalQuery();
	}
}

void AAuraPlayerController::HandleAutoMoveRecoveryQueryFinished(TSharedPtr<FEnvQueryResult> Result)
{
	if (!Result || Result->QueryID != ActiveAutoMoveQueryId)
	{
		return;
	}
	ActiveAutoMoveQueryId = INDEX_NONE;
	UE_LOG(LogAuraAutoMove, Display, TEXT("Recovery query success=%d candidates=%d location=%s goal=%s"),
		Result->IsSuccessful(), Result->Items.Num(),
		GetPawn() ? *GetPawn()->GetActorLocation().ToCompactString() : TEXT("NoPawn"),
		*ResolvedDestination.ToCompactString());
	if (Result->IsSuccessful())
	{
		for (int32 ItemIndex = 0; ItemIndex < Result->Items.Num(); ++ItemIndex)
		{
			const FVector RecoveryLocation = Result->GetItemAsLocation(ItemIndex);
			if (RequestAutoRunPath(RecoveryLocation, EAutoMoveState::Recovering))
			{
				UE_LOG(LogAuraAutoMove, Display, TEXT("Recovery candidate selected index=%d location=%s"),
					ItemIndex, *RecoveryLocation.ToCompactString());
				return;
			}
		}
	}

	// Keep pursuing the original goal. A later blocked check can retry recovery if the crowd moves.
	UE_LOG(LogAuraAutoMove, Warning, TEXT("Recovery query had no usable candidate; retrying final destination."));
	if (!RequestAutoRunPath(ResolvedDestination, EAutoMoveState::Moving))
	{
		StopAutoRun(false);
	}
}

void AAuraPlayerController::GenerateAutoMoveEQSItems(FEnvQueryInstance& QueryInstance, bool bRecoveryQuery) const
{
	const APawn* ControlledPawn = GetPawn();
	UNavigationSystemV1* NavSystem = UNavigationSystemV1::GetCurrent(GetWorld());
	if (!ControlledPawn || !NavSystem)
	{
		return;
	}

	const FVector Center = bRecoveryQuery ? ControlledPawn->GetActorLocation() : RequestedDestination;
	const TArray<float>& SearchRadii = bRecoveryQuery ? AutoMoveRecoverySearchRadii : AutoMoveGoalSearchRadii;
	TArray<FNavLocation> Candidates;
	auto AddProjectedCandidate = [&](const FVector& Candidate)
	{
		FNavLocation Projected;
		if (NavSystem->ProjectPointToNavigation(
			Candidate, Projected, FVector(150.f, 150.f, 300.f), &GetNavAgentPropertiesRef()) &&
			!Candidates.ContainsByPredicate([&Projected](const FNavLocation& Existing)
			{
				return FVector::DistSquared2D(Existing.Location, Projected.Location) < FMath::Square(20.f);
			}))
		{
			Candidates.Add(Projected);
		}
	};

	if (!bRecoveryQuery)
	{
		AddProjectedCandidate(Center);
	}
	const int32 Samples = FMath::Max(4, AutoMoveSamplesPerRing);
	const FVector DirectionToGoal = (ResolvedDestination - Center).GetSafeNormal2D();
	const float BaseAngle = DirectionToGoal.IsNearlyZero()
		? 0.f
		: FMath::Atan2(DirectionToGoal.Y, DirectionToGoal.X);
	for (const float Radius : SearchRadii)
	{
		for (int32 Index = 0; Index < Samples; ++Index)
		{
			const float Angle = BaseAngle + UE_TWO_PI * static_cast<float>(Index) / static_cast<float>(Samples);
			const FVector Offset(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.f);
			AddProjectedCandidate(Center + Offset);
		}
	}

	for (const FNavLocation& Candidate : Candidates)
	{
		QueryInstance.AddItemData<UEnvQueryItemType_Point>(Candidate);
	}
}

bool AAuraPlayerController::ScoreAutoMoveEQSItem(
	const FVector& ItemLocation, bool bRecoveryQuery, float& OutScore) const
{
	const APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		return false;
	}

	float Clearance = 0.f;
	if (!GetAutoMoveCandidateClearance(ItemLocation, Clearance))
	{
		return false;
	}

	double PathFromPlayer = 0.0;
	if (!GetAutoRunPathLength(ControlledPawn->GetActorLocation(), ItemLocation, PathFromPlayer))
	{
		return false;
	}

	double Cost = PathFromPlayer;
	if (bRecoveryQuery)
	{
		const FVector CandidateDirection = (ItemLocation - ControlledPawn->GetActorLocation()).GetSafeNormal2D();
		if (FailedRecoveryDirections.ContainsByPredicate(
			[this, &CandidateDirection](const FVector& FailedDirection)
			{
				return FVector::DotProduct(CandidateDirection, FailedDirection) > AutoMoveFailedDirectionDotThreshold;
			}))
		{
			return false;
		}

		float ApproachClearance = 0.f;
		if (!GetAutoMoveSegmentClearance(ControlledPawn->GetActorLocation(), ItemLocation, ApproachClearance))
		{
			return false;
		}
		Cost += FMath::Max(0.f, AutoMovePathClearance - ApproachClearance) * 16.f;

		double PathToGoal = 0.0;
		if (!GetAutoRunPathLength(ItemLocation, ResolvedDestination, PathToGoal))
		{
			return false;
		}
		const float CurrentGoalDistance = FVector::Dist2D(ControlledPawn->GetActorLocation(), ResolvedDestination);
		const float CandidateGoalDistance = FVector::Dist2D(ItemLocation, ResolvedDestination);
		if (CandidateGoalDistance > CurrentGoalDistance + AutoMoveRecoveryMaxRetreat)
		{
			return false;
		}
		Cost += PathToGoal + FMath::Max(0.f, CandidateGoalDistance - CurrentGoalDistance) * 3.f;
		const float RecoveryDistance = FVector::Dist2D(ControlledPawn->GetActorLocation(), ItemLocation);
		const float DesiredRecoveryDistance = FMath::Min(
			700.f, 320.f + static_cast<float>(ConsecutiveRecoveryFailures) * 180.f);
		Cost += FMath::Max(0.f, DesiredRecoveryDistance - RecoveryDistance) * 4.f;

		const FVector DirectionFromCandidate = (ResolvedDestination - ItemLocation).GetSafeNormal2D();
		const float LookAheadDistance = FMath::Min(AutoMoveRecoveryLookAhead, CandidateGoalDistance);
		float ForwardClearance = 0.f;
		if (!DirectionFromCandidate.IsNearlyZero() && GetAutoMoveSegmentClearance(
			ItemLocation, ItemLocation + DirectionFromCandidate * LookAheadDistance, ForwardClearance))
		{
			Cost += FMath::Max(0.f, AutoMovePathClearance - ForwardClearance) * 12.f;
			Cost -= FMath::Min(ForwardClearance, 300.f) * 0.5f;
		}
		Cost -= FMath::Min(ApproachClearance, 300.f) * 0.5f;
	}
	else
	{
		Cost += FVector::Dist2D(ItemLocation, RequestedDestination) * 3.0;
	}

	Cost = FMath::Max(1.0, Cost - static_cast<double>(FMath::Min(Clearance, 500.f)) * 0.35);
	OutScore = static_cast<float>(1.0 / (1.0 + Cost / 1000.0));
	return true;
}

bool AAuraPlayerController::GetAutoMoveSegmentClearance(
	const FVector& Start, const FVector& End, float& OutClearance) const
{
	const APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn || !GetWorld())
	{
		return false;
	}

	const FVector Segment = (End - Start).GetSafeNormal2D();
	const float SegmentLength = FVector::Dist2D(Start, End);
	if (SegmentLength <= UE_KINDA_SMALL_NUMBER)
	{
		return GetAutoMoveCandidateClearance(Start, OutClearance);
	}

	float PlayerRadius = 34.f;
	if (const ACharacter* ControlledCharacter = Cast<ACharacter>(ControlledPawn))
	{
		PlayerRadius = ControlledCharacter->GetCapsuleComponent()->GetScaledCapsuleRadius();
	}

	const FVector Midpoint = (Start + End) * 0.5f;
	FCollisionObjectQueryParams ObjectQuery;
	ObjectQuery.AddObjectTypesToQuery(ECC_Pawn);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(AutoMoveSegmentClearance), false, ControlledPawn);
	TArray<FOverlapResult> Overlaps;
	GetWorld()->OverlapMultiByObjectType(
		Overlaps, Midpoint, FQuat::Identity, ObjectQuery,
		FCollisionShape::MakeSphere(SegmentLength * 0.5f + 650.f), QueryParams);

	OutClearance = 650.f;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		const APawn* OtherPawn = Cast<APawn>(Overlap.GetActor());
		if (!OtherPawn || OtherPawn == ControlledPawn)
		{
			continue;
		}

		const FVector ToPawn = OtherPawn->GetActorLocation() - Start;
		const float AlongSegment = FVector::DotProduct(ToPawn.GetSafeNormal2D(), Segment);
		const float ProjectedDistance = FVector::DotProduct(FVector(ToPawn.X, ToPawn.Y, 0.f), Segment);
		if (ProjectedDistance <= 0.f && AlongSegment <= 0.f)
		{
			continue;
		}

		const float ClampedDistance = FMath::Clamp(ProjectedDistance, 0.f, SegmentLength);
		const FVector ClosestPoint = Start + Segment * ClampedDistance;
		float OtherRadius = 34.f;
		if (const ACharacter* OtherCharacter = Cast<ACharacter>(OtherPawn))
		{
			OtherRadius = OtherCharacter->GetCapsuleComponent()->GetScaledCapsuleRadius();
		}
		const float EdgeClearance = FVector::Dist2D(ClosestPoint, OtherPawn->GetActorLocation()) -
			PlayerRadius - OtherRadius;
		OutClearance = FMath::Min(OutClearance, EdgeClearance);
	}
	return true;
}

bool AAuraPlayerController::GetAutoMoveCandidateClearance(const FVector& ItemLocation, float& OutClearance) const
{
	const APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn || !GetWorld())
	{
		return false;
	}

	float PlayerRadius = 34.f;
	if (const ACharacter* ControlledCharacter = Cast<ACharacter>(ControlledPawn))
	{
		PlayerRadius = ControlledCharacter->GetCapsuleComponent()->GetScaledCapsuleRadius();
	}

	FCollisionObjectQueryParams ObjectQuery;
	ObjectQuery.AddObjectTypesToQuery(ECC_Pawn);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(AutoMoveCandidateClearance), false, ControlledPawn);
	TArray<FOverlapResult> Overlaps;
	GetWorld()->OverlapMultiByObjectType(
		Overlaps, ItemLocation, FQuat::Identity, ObjectQuery, FCollisionShape::MakeSphere(650.f), QueryParams);

	OutClearance = 650.f;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		const APawn* OtherPawn = Cast<APawn>(Overlap.GetActor());
		if (!OtherPawn || OtherPawn == ControlledPawn)
		{
			continue;
		}
		float OtherRadius = 34.f;
		if (const ACharacter* OtherCharacter = Cast<ACharacter>(OtherPawn))
		{
			OtherRadius = OtherCharacter->GetCapsuleComponent()->GetScaledCapsuleRadius();
		}
		const float EdgeClearance = FVector::Dist2D(ItemLocation, OtherPawn->GetActorLocation()) -
			PlayerRadius - OtherRadius;
		if (EdgeClearance < AutoMovePawnClearance)
		{
			return false;
		}
		OutClearance = FMath::Min(OutClearance, EdgeClearance);
	}
	return true;
}

void AAuraPlayerController::ResetAutoMoveProgress()
{
	const APawn* ControlledPawn = GetPawn();
	AutoMoveProgressTarget = AutoRunMoveDestination;
	if (CrowdPathFollowingComponent && CrowdPathFollowingComponent->GetStatus() == EPathFollowingStatus::Moving)
	{
		AutoMoveProgressTarget = CrowdPathFollowingComponent->GetCurrentTargetLocation();
	}
	AutoMoveBestDistanceToTarget = ControlledPawn
		? FVector::Dist2D(ControlledPawn->GetActorLocation(), AutoMoveProgressTarget)
		: 0.f;
	AutoMoveProgressCheckElapsed = 0.f;
	AutoMoveBlockedTime = 0.f;
}

void AAuraPlayerController::UpdateAutoMoveProgress(float DeltaSeconds)
{
	const APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		return;
	}
	AutoMoveProgressCheckElapsed += DeltaSeconds;
	if (AutoMoveProgressCheckElapsed < AutoMoveProgressCheckInterval)
	{
		return;
	}

	FVector CurrentProgressTarget = AutoRunMoveDestination;
	if (CrowdPathFollowingComponent && CrowdPathFollowingComponent->GetStatus() == EPathFollowingStatus::Moving)
	{
		CurrentProgressTarget = CrowdPathFollowingComponent->GetCurrentTargetLocation();
	}
	const float CurrentDistanceToTarget = FVector::Dist2D(
		ControlledPawn->GetActorLocation(), CurrentProgressTarget);
	if (!CurrentProgressTarget.Equals(AutoMoveProgressTarget, 10.f))
	{
		AutoMoveProgressTarget = CurrentProgressTarget;
		AutoMoveBestDistanceToTarget = CurrentDistanceToTarget;
		AutoMoveBlockedTime = 0.f;
		AutoMoveProgressCheckElapsed = 0.f;
		return;
	}

	if (CurrentDistanceToTarget <= AutoMoveBestDistanceToTarget - AutoMoveMinimumProgress)
	{
		AutoMoveBestDistanceToTarget = CurrentDistanceToTarget;
		AutoMoveBlockedTime = 0.f;
		AutoMoveReplanCooldownRemaining = 0.f;
		if (AutoMoveState == EAutoMoveState::Moving)
		{
			ConsecutiveRecoveryFailures = 0;
			FailedRecoveryDirections.Reset();
		}
	}
	else
	{
		AutoMoveBlockedTime += AutoMoveProgressCheckElapsed;
		const FNavPathSharedPtr ActivePath = CrowdPathFollowingComponent
			? CrowdPathFollowingComponent->GetPath()
			: nullptr;
		UE_LOG(LogAuraAutoMove, Display,
			TEXT("Stall state=%d blocked=%.2f velocity=%s pathStatus=%d simulation=%d avoidance=%d "
				 "distance=%.1f best=%.1f target=%s pathPoints=%d"),
			static_cast<uint8>(AutoMoveState), AutoMoveBlockedTime,
			*ControlledPawn->GetVelocity().ToCompactString(),
			CrowdPathFollowingComponent ? static_cast<uint8>(CrowdPathFollowingComponent->GetStatus()) : 255,
			CrowdPathFollowingComponent
				? static_cast<uint8>(CrowdPathFollowingComponent->GetCrowdSimulationState()) : 255,
			CrowdPathFollowingComponent && CrowdPathFollowingComponent->IsCrowdObstacleAvoidanceActive(),
			CurrentDistanceToTarget, AutoMoveBestDistanceToTarget,
			*CurrentProgressTarget.ToCompactString(),
			ActivePath.IsValid() ? ActivePath->GetPathPoints().Num() : 0);
	}
	AutoMoveProgressCheckElapsed = 0.f;
	if (AutoMoveBlockedTime < AutoMoveBlockedTimeout || AutoMoveReplanCooldownRemaining > 0.f)
	{
		return;
	}

	AutoMoveBlockedTime = 0.f;
	HandleBlockedAutoMove();
}

void AAuraPlayerController::HandleBlockedAutoMove()
{
	UE_LOG(LogAuraAutoMove, Display, TEXT("Blocked state=%d location=%s moveTarget=%s finalTarget=%s"),
		static_cast<uint8>(AutoMoveState),
		GetPawn() ? *GetPawn()->GetActorLocation().ToCompactString() : TEXT("NoPawn"),
		*AutoRunMoveDestination.ToCompactString(), *ResolvedDestination.ToCompactString());
	if (AutoMoveState == EAutoMoveState::Recovering)
	{
		const APawn* ControlledPawn = GetPawn();
		const FVector FailedDirection = ControlledPawn
			? (AutoRunMoveDestination - ControlledPawn->GetActorLocation()).GetSafeNormal2D()
			: FVector::ZeroVector;
		if (!FailedDirection.IsNearlyZero() && !FailedRecoveryDirections.ContainsByPredicate(
			[this, &FailedDirection](const FVector& ExistingDirection)
			{
				return FVector::DotProduct(FailedDirection, ExistingDirection) > AutoMoveFailedDirectionDotThreshold;
			}))
		{
			FailedRecoveryDirections.Add(FailedDirection);
		}
		++ConsecutiveRecoveryFailures;
		UE_LOG(LogAuraAutoMove, Display, TEXT("Recovery direction failed count=%d sectors=%d direction=%s"),
			ConsecutiveRecoveryFailures, FailedRecoveryDirections.Num(), *FailedDirection.ToCompactString());
	}
	if (AutoMoveState == EAutoMoveState::Moving)
	{
		float DestinationClearance = 0.f;
		if (!GetAutoMoveCandidateClearance(ResolvedDestination, DestinationClearance))
		{
			StartAutoMoveGoalQuery();
			return;
		}
	}

	if (AutoMoveState == EAutoMoveState::Moving || AutoMoveState == EAutoMoveState::Recovering)
	{
		StartAutoMoveRecoveryQuery();
	}
}

void AAuraPlayerController::HandleAutoMovePathFinished(const FPathFollowingResult& Result)
{
	if (AutoMoveState != EAutoMoveState::Moving && AutoMoveState != EAutoMoveState::Recovering)
	{
		return;
	}

	// RequestMove aborts the previous request synchronously before installing the new path.
	if (Result.IsInterrupted())
	{
		return;
	}

	const APawn* ControlledPawn = GetPawn();
	const float DistanceToMoveDestination = ControlledPawn
		? FVector::Dist2D(ControlledPawn->GetActorLocation(), AutoRunMoveDestination)
		: TNumericLimits<float>::Max();
	const float RequiredAcceptanceRadius = AutoMoveState == EAutoMoveState::Recovering
		? AutoMoveRecoveryAcceptanceRadius
		: AutoRunAcceptanceRadius;
	const bool bActuallyReachedDestination = DistanceToMoveDestination <= RequiredAcceptanceRadius;

	if (Result.IsSuccess() && bActuallyReachedDestination)
	{
		if (AutoMoveState == EAutoMoveState::Moving)
		{
			StopAutoRun(true);
			return;
		}

		ConsecutiveRecoveryFailures = 0;
		FailedRecoveryDirections.Reset();
		if (!RequestAutoRunPath(ResolvedDestination, EAutoMoveState::Moving))
		{
			StartAutoMoveGoalQuery();
		}
		return;
	}
	if (Result.IsSuccess())
	{
		UE_LOG(LogAuraAutoMove, Warning,
			TEXT("Ignoring premature path success state=%d distance=%.1f acceptance=%.1f location=%s target=%s"),
			static_cast<uint8>(AutoMoveState), DistanceToMoveDestination, RequiredAcceptanceRadius,
			ControlledPawn ? *ControlledPawn->GetActorLocation().ToCompactString() : TEXT("NoPawn"),
			*AutoRunMoveDestination.ToCompactString());
	}

	// A completed failure already means the current path cannot make progress, so recover
	// immediately instead of waiting for the periodic stall detector to time out again.
	AutoMoveBlockedTime = 0.f;
	AutoMoveReplanCooldownRemaining = 0.f;
	HandleBlockedAutoMove();
}

bool AAuraPlayerController::GetAutoRunPathLength(const FVector& Start, const FVector& End, double& OutPathLength) const
{
	if (FVector::Dist2D(Start, End) <= AutoRunAcceptanceRadius)
	{
		OutPathLength = 0.0;
		return true;
	}
	UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(
		const_cast<AAuraPlayerController*>(this), Start, End);
	if (!Path || !Path->IsValid() || Path->IsPartial() || Path->PathPoints.Num() < 2)
	{
		return false;
	}
	OutPathLength = Path->GetPathLength();
	return true;
}

void AAuraPlayerController::SnapCameraToPlayer()
{
	// 按下空格：把相机拉到玩家当前相机位置
	UpdateFixedCameraToPlayer();
}

void AAuraPlayerController::UpdateFixedCameraToPlayer()
{
	if (APawn* ControlledPawn = GetPawn())
	{
		if (UCameraComponent* PlayerCamera = ControlledPawn->FindComponentByClass<UCameraComponent>())
		{
			FixedCameraLocation = PlayerCamera->GetComponentLocation();
			FixedCameraRotation = PlayerCamera->GetComponentRotation();
		}
	}
}

void AAuraPlayerController::GetPlayerViewPoint(FVector& Location, FRotator& Rotation) const
{
	// 如果 FixedCameraLocation 还没初始化，回退到默认行为
	if (FixedCameraLocation.IsZero())
	{
		Super::GetPlayerViewPoint(Location, Rotation);
		return;
	}
	
	Location = FixedCameraLocation;
	Rotation = FixedCameraRotation;
}

UAuraAbilitySystemComponent* AAuraPlayerController::GetASC()
{
	if (!AuraAbilitySystemComponent || AuraAbilitySystemComponent->GetAvatarActor() != GetPawn())
	{
		AuraAbilitySystemComponent = Cast<UAuraAbilitySystemComponent>(UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetPawn<APawn>()));
	}
	return AuraAbilitySystemComponent;
}
