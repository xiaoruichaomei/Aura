#include "Actor/AuraFireBall.h"

#include "AbilitySystem/AuraAbilitySystemLibrary.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AuraGameplayTags.h"
#include "Aura/Aura.h"
#include "Components/SphereComponent.h"
#include "GameFramework/Actor.h"
#include "Interface/CombatInterface.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "UObject/ConstructorHelpers.h"
#include "Subsystem/AuraProjectilePoolSubsystem.h"

AAuraFireBall::AAuraFireBall()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = true;
	SetReplicateMovement(true);

	Sphere = CreateDefaultSubobject<USphereComponent>(TEXT("Sphere"));
	SetRootComponent(Sphere);
	Sphere->SetSphereRadius(24.f);
	Sphere->SetCollisionObjectType(ECC_Projectile);
	Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Sphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	Sphere->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
	Sphere->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Overlap);
	Sphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

	FireEffect = CreateDefaultSubobject<UNiagaraComponent>(TEXT("FireEffect"));
	FireEffect->SetupAttachment(Sphere);
	FireEffect->SetAutoActivate(true);

	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> DefaultFireEffect(
		TEXT("/Game/Assets/Effects/Projectiles/FireBolt/NS_Fireball.NS_Fireball"));
	if (DefaultFireEffect.Succeeded())
	{
		FireEffectAsset = DefaultFireEffect.Object;
		FireEffect->SetAsset(FireEffectAsset);
	}
}

void AAuraFireBall::BeginPlay()
{
	Super::BeginPlay();
	if (!bPoolManaged && LifeSpan > 0.f)
	{
		SetLifeSpan(LifeSpan);
	}
	Sphere->OnComponentBeginOverlap.AddUniqueDynamic(this, &AAuraFireBall::OnSphereOverlap);
	if (bPoolManaged)
	{
		OnRep_PoolActive();
	}
}

void AAuraFireBall::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority())
	{
		return;
	}

	if (State == EFireBallState::Outgoing)
	{
		OutgoingElapsed += DeltaSeconds;
		const float Alpha = FMath::Clamp(OutgoingElapsed / OutgoingDuration, 0.f, 1.f);
		SetActorLocation(FMath::InterpEaseOut(OutgoingStartLocation, OutgoingTargetLocation, Alpha, 2.f));
		if (Alpha >= 1.f)
		{
			ReturningElapsed = 0.f;
			SetFireBallState(EFireBallState::Returning);
		}
		return;
	}

	if (State == EFireBallState::Returning)
	{
		AActor* Source = SourceActor.Get();
		if (!IsValid(Source))
		{
			ReportFinished();
			ReturnToPool();
			return;
		}

		const FVector ReturnTarget = Source->GetActorLocation();
		if (FVector::DistSquared(GetActorLocation(), ReturnTarget) <= FMath::Square(ArrivalRadius))
		{
			SetFireBallState(EFireBallState::Arrived);
			Sphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			FireEffect->Deactivate();
			return;
		}

		ReturningElapsed += DeltaSeconds;
		const float SpeedAlpha = FMath::Clamp(ReturningElapsed / ReturnDuration, 0.f, 1.f);
		const float ReturnSpeed = FMath::Lerp(700.f, 2500.f, FMath::InterpEaseIn(0.f, 1.f, SpeedAlpha, 2.f));
		SetActorLocation(FMath::VInterpConstantTo(GetActorLocation(), ReturnTarget, DeltaSeconds, ReturnSpeed));
	}
}

void AAuraFireBall::Destroyed()
{
	if (!bPoolManaged)
	{
		ReportFinished();
		SetFireBallState(EFireBallState::Destroyed);
	}
	Super::Destroyed();
}

void AAuraFireBall::ActivateFromPool(const FTransform& Transform, AActor* NewOwner, APawn* NewInstigator)
{
	if (!bPoolManaged || !HasAuthority())
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(PoolLifeTimer);
	SetOwner(NewOwner);
	SetInstigator(NewInstigator);
	SetActorTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);
	SourceActor = nullptr;
	DamageEffectSpecHandle = FGameplayEffectSpecHandle();
	OutgoingHitActors.Reset();
	ReturningHitActors.Reset();
	LocalVisualHitActors.Reset();
	OutgoingElapsed = 0.f;
	ReturningElapsed = 0.f;
	bFinishReported = false;
	State = EFireBallState::Outgoing;
	bPoolActive = true;
	OnRep_PoolActive();
	GetWorldTimerManager().SetTimer(PoolLifeTimer, this, &ThisClass::HandleLifeExpired, LifeSpan, false);
	ForceNetUpdate();
}

void AAuraFireBall::DeactivateToPool()
{
	GetWorldTimerManager().ClearTimer(PoolLifeTimer);
	SetActorTickEnabled(false);
	bPoolActive = false;
	Sphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (FireEffect)
	{
		FireEffect->DeactivateImmediate();
	}
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	SourceActor = nullptr;
	DamageEffectSpecHandle = FGameplayEffectSpecHandle();
	OutgoingHitActors.Reset();
	ReturningHitActors.Reset();
	LocalVisualHitActors.Reset();
	OnStateChanged.Clear();
	OnFireBallFinished.Clear();
	SetOwner(nullptr);
	SetInstigator(nullptr);
	ForceNetUpdate();
}

void AAuraFireBall::ReturnToPool()
{
	if (bPoolManaged)
	{
		if (UAuraProjectilePoolSubsystem* Pool = GetWorld()->GetSubsystem<UAuraProjectilePoolSubsystem>())
		{
			Pool->ReleaseFireBall(this);
			return;
		}
	}
	Destroy();
}

void AAuraFireBall::HandleLifeExpired()
{
	ReportFinished();
	ReturnToPool();
}

void AAuraFireBall::OnRep_PoolActive()
{
	SetActorHiddenInGame(!bPoolActive);
	SetActorEnableCollision(bPoolActive);
	Sphere->SetCollisionEnabled(bPoolActive ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	if (FireEffect)
	{
		if (bPoolActive)
		{
			FireEffect->Activate(true);
		}
		else
		{
			FireEffect->DeactivateImmediate();
		}
	}
}

void AAuraFireBall::SetSourceActor(AActor* NewSourceActor)
{
	if (HasAuthority())
	{
		SourceActor = NewSourceActor;
	}
}

void AAuraFireBall::ConfigureFlight(const FVector& NewDirection, float NewMaxTravelDistance, float NewOutgoingDuration, float NewReturnDuration)
{
	if (!HasAuthority())
	{
		return;
	}

	OutgoingStartLocation = GetActorLocation();
	OutgoingTargetLocation = OutgoingStartLocation + NewDirection.GetSafeNormal2D() * FMath::Max(0.f, NewMaxTravelDistance);
	OutgoingDuration = FMath::Max(KINDA_SMALL_NUMBER, NewOutgoingDuration);
	ReturnDuration = FMath::Max(KINDA_SMALL_NUMBER, NewReturnDuration);
	OutgoingElapsed = 0.f;
	ReturningElapsed = 0.f;
	SetActorTickEnabled(true);
}

void AAuraFireBall::SetDamageEffectSpecHandle(const FGameplayEffectSpecHandle& InDamageEffectSpecHandle)
{
	if (HasAuthority())
	{
		DamageEffectSpecHandle = InDamageEffectSpecHandle;
	}
}

void AAuraFireBall::SetFireBallState(EFireBallState NewState)
{
	if (State == NewState)
	{
		return;
	}

	const EFireBallState PreviousState = State;
	State = NewState;
	if (NewState == EFireBallState::Returning)
	{
		// The return pass is a distinct visual hit opportunity.
		LocalVisualHitActors.Reset();
	}
	OnStateChanged.Broadcast(State);

	if (NewState == EFireBallState::Arrived || NewState == EFireBallState::Destroyed)
	{
		SetActorTickEnabled(false);
		if (NewState == EFireBallState::Arrived)
		{
			ReportFinished();
		}
	}

	if (HasAuthority() && PreviousState != NewState)
	{
		ForceNetUpdate();
	}
}

void AAuraFireBall::ReportFinished()
{
	if (!bFinishReported)
	{
		bFinishReported = true;
		OnFireBallFinished.Broadcast(this);
	}
}

void AAuraFireBall::OnRep_State(EFireBallState PreviousState)
{
	if (State == EFireBallState::Returning)
	{
		LocalVisualHitActors.Reset();
	}
	if (State == EFireBallState::Arrived || State == EFireBallState::Destroyed)
	{
		SetActorTickEnabled(false);
	}
	OnStateChanged.Broadcast(State);
}

void AAuraFireBall::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAuraFireBall, SourceActor);
	DOREPLIFETIME(AAuraFireBall, State);
	DOREPLIFETIME(AAuraFireBall, bPoolActive);
}

void AAuraFireBall::OnSphereOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// Pool activation and replicated SourceActor assignment are separate state
	// updates. Reject all ownership identities so a returning ball can never
	// damage its caster during that short transition.
	if (!IsValid(OtherActor) || OtherActor == SourceActor.Get() || OtherActor == GetOwner() || OtherActor == GetInstigator())
	{
		return;
	}
	if (!UAuraAbilitySystemLibrary::IsNotFriend(SourceActor.Get(), OtherActor))
	{
		return;
	}
	if (!OtherActor->Implements<UCombatInterface>() || ICombatInterface::Execute_IsDead(OtherActor))
	{
		return;
	}
	InvokeLocalHitCue(OtherActor, SweepResult);

	if (!HasAuthority() || !DamageEffectSpecHandle.Data.IsValid())
	{
		return;
	}

	TSet<TWeakObjectPtr<AActor>>* HitActors = State == EFireBallState::Outgoing ? &OutgoingHitActors :
		(State == EFireBallState::Returning ? &ReturningHitActors : nullptr);
	if (!HitActors || HitActors->Contains(OtherActor))
	{
		return;
	}

	UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(OtherActor);
	if (!TargetASC)
	{
		return;
	}

	HitActors->Add(OtherActor);
	TargetASC->ApplyGameplayEffectSpecToSelf(*DamageEffectSpecHandle.Data.Get());
}

void AAuraFireBall::InvokeLocalHitCue(AActor* OtherActor, const FHitResult& SweepResult)
{
	if (State != EFireBallState::Outgoing && State != EFireBallState::Returning)
	{
		return;
	}
	if (LocalVisualHitActors.Contains(OtherActor))
	{
		return;
	}

	UAbilitySystemComponent* SourceASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(SourceActor.Get());
	if (!SourceASC)
	{
		return;
	}

	LocalVisualHitActors.Add(OtherActor);
	FGameplayCueParameters CueParameters(SourceASC->MakeEffectContext());
	const FVector CueLocation = SweepResult.bBlockingHit ? FVector(SweepResult.ImpactPoint) : GetActorLocation();
	const FVector CueNormal = SweepResult.bBlockingHit ? FVector(SweepResult.ImpactNormal) : FVector::UpVector;
	CueParameters.Location = CueLocation;
	CueParameters.Normal = CueNormal;
	CueParameters.Instigator = SourceActor.Get();
	CueParameters.EffectCauser = this;
	SourceASC->InvokeGameplayCueEvent(FAuraGameplayTags::Get().GameplayCue_FireBlast_Hit, EGameplayCueEvent::Executed, CueParameters);
}
