


#include "Player/AuraPlayerController.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AuraGameplayTags.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/HitResult.h"
#include "GameFramework/Pawn.h"
#include "Interface/EnemyInterface.h"
#include "GameplayTagContainer.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "AbilitySystem/AuraAbilitySystemComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/Engine.h"
#include "Input/AuraInputComponent.h"
#include "GameFramework/Character.h"
#include "NiagaraFunctionLibrary.h"
#include "UI/Widget/DamageTextComponent.h"
#include "Actor/MagicCircle.h"
#include "UObject/ConstructorHelpers.h"
#include "../Aura.h"
#include "Materials/MaterialInterface.h"

AAuraPlayerController::AAuraPlayerController()
{
	bReplicates = true;	// 当服务器上的actor发生变化时，服务器上的变化会复制发送到所有连接的客户端
	
	Spline = CreateDefaultSubobject<USplineComponent>("Spline");
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
	AutoRun();
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
	const FVector2D InputAxisVector = InputActionValue.Get<FVector2D>();
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
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, ClickNiagaraSystem, CursorLocation);
	}
}

bool AAuraPlayerController::IsInputBlocked() const
{
	if (APawn* ControlledPawn = GetPawn())
	{
		if (UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(ControlledPawn))
		{
			return ASC->HasMatchingGameplayTag(FAuraGameplayTags::Get().Player_Block);
		}
	}
	return false;
}

void AAuraPlayerController::AbilityInputTagPressed(FGameplayTag InputTag)
{
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
		bAutoRunning = false;

		// 点击地面（移动或施法）时在所有端生成点击特效
		if (CursorHit.bBlockingHit)
		{
			MulticastSpawnClickEffect(CursorHit.ImpactPoint);
		}
	}
}

void AAuraPlayerController::AbilityInputTagReleased(FGameplayTag InputTag)
{
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
			UNavigationPath* NavPath = UNavigationSystemV1::FindPathToLocationSynchronously(this, ControlledPawn->GetActorLocation(), CachedDestination);
			if (NavPath)
			{
				Spline->ClearSplinePoints();
				for (const auto& PointLocation : NavPath->PathPoints)
				{
					Spline->AddSplinePoint(PointLocation, ESplineCoordinateSpace::World);
				}
				if (NavPath->PathPoints.Num() > 0)
				{
					CachedDestination = NavPath->PathPoints[NavPath->PathPoints.Num() - 1];
					bAutoRunning = true;
				}
			}
		}
		FollowTime = 0.f;
	}
}

void AAuraPlayerController::AbilityInputTagHeld(FGameplayTag InputTag)
{
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

void AAuraPlayerController::AutoRun()
{
	if (!bAutoRunning || IsInputBlocked())
	{
		return;
	}
	APawn* ControlledPawn = GetPawn();
	if (ControlledPawn)
	{
		const FVector LocationOnSpline = Spline->FindLocationClosestToWorldLocation(ControlledPawn->GetActorLocation(), ESplineCoordinateSpace::World);
		const FVector Direction = Spline->FindDirectionClosestToWorldLocation(LocationOnSpline, ESplineCoordinateSpace::World);
		ControlledPawn->AddMovementInput(Direction);
		const float DistanceToDestination = (LocationOnSpline - CachedDestination).Length();
		if (DistanceToDestination <= AutoRunAcceptanceRadius)
		{
			bAutoRunning = false;
		}
	}
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
	if (!AuraAbilitySystemComponent)
	{
		AuraAbilitySystemComponent = Cast<UAuraAbilitySystemComponent>(UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetPawn<APawn>()));
	}
	return AuraAbilitySystemComponent;
}
