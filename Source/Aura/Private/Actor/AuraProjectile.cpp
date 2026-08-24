


#include "Actor/AuraProjectile.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "AbilitySystem/AuraAbilitySystemLibrary.h"
#include "Aura/Aura.h"
#include "Components/AudioComponent.h"
#include "Components/SphereComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Subsystem/AuraProjectilePoolSubsystem.h"

AAuraProjectile::AAuraProjectile()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);

	Sphere = CreateDefaultSubobject<USphereComponent>("Sphere");
	SetRootComponent(Sphere);
	Sphere->SetCollisionObjectType(ECC_Projectile);
	Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Sphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	Sphere->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
	Sphere->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Overlap);
	Sphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	
	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>("ProjectileMovement");
	ProjectileMovement->InitialSpeed = 550.f;
	ProjectileMovement->MaxSpeed = 550.f;
	ProjectileMovement->ProjectileGravityScale = 0.f;
}

void AAuraProjectile::BeginPlay()
{
	Super::BeginPlay();
	Sphere->OnComponentBeginOverlap.AddUniqueDynamic(this, &AAuraProjectile::OnSphereOverlap);
	if (bPoolManaged)
	{
		OnRep_PoolActive();
	}
	else
	{
		SetLifeSpan(LifeSpan);
		StartFlightAudio();
	}
}

void AAuraProjectile::Destroyed()
{
	if (!bPoolManaged && !bHit && !HasAuthority())
	{
		PlayImpactEffects(GetActorLocation());
	}

	Super::Destroyed();
}

void AAuraProjectile::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAuraProjectile, bPoolActive);
}

void AAuraProjectile::ActivateFromPool(const FTransform& Transform, AActor* NewOwner, APawn* NewInstigator)
{
	if (!bPoolManaged || !HasAuthority())
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(PoolLifeTimer);
	SetOwner(NewOwner);
	SetInstigator(NewInstigator);
	SetActorTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);
	DamageEffectSpecHandle = FGameplayEffectSpecHandle();
	bHit = false;
	bPoolActive = true;
	OnRep_PoolActive();
	ProjectileMovement->StopMovementImmediately();
	ProjectileMovement->Velocity = Transform.GetRotation().GetForwardVector() * ProjectileMovement->InitialSpeed;
	ProjectileMovement->Activate(true);
	GetWorldTimerManager().SetTimer(PoolLifeTimer, this, &ThisClass::HandleLifeExpired, LifeSpan, false);
	ForceNetUpdate();
}

void AAuraProjectile::DeactivateToPool()
{
	GetWorldTimerManager().ClearTimer(PoolLifeTimer);
	bPoolActive = false;
	SetTrailEffectsActive(false);
	ProjectileMovement->StopMovementImmediately();
	ProjectileMovement->Deactivate();
	Sphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	if (FlySoundComponent)
	{
		FlySoundComponent->Stop();
	}
	DamageEffectSpecHandle = FGameplayEffectSpecHandle();
	SetOwner(nullptr);
	SetInstigator(nullptr);
	ForceNetUpdate();
}

void AAuraProjectile::OnRep_PoolActive()
{
	if (bPoolActive)
	{
		bHit = false;
		SetActorHiddenInGame(false);
		SetActorEnableCollision(true);
		Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		SetTrailEffectsActive(true);
		ProjectileMovement->Activate(true);
		StartFlightAudio();
	}
	else
	{
		SetTrailEffectsActive(false);
		ProjectileMovement->StopMovementImmediately();
		ProjectileMovement->Deactivate();
		Sphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SetActorEnableCollision(false);
		SetActorHiddenInGame(true);
		if (FlySoundComponent)
		{
			FlySoundComponent->Stop();
		}
	}
}

void AAuraProjectile::HandleLifeExpired()
{
	if (!bPoolManaged && !bHit)
	{
		PlayImpactEffects(GetActorLocation());
	}
	ReturnToPool();
}

void AAuraProjectile::ReturnToPool()
{
	if (bPoolManaged)
	{
		if (UAuraProjectilePoolSubsystem* Pool = GetWorld()->GetSubsystem<UAuraProjectilePoolSubsystem>())
		{
			Pool->ReleaseProjectile(this);
			return;
		}
	}
	Destroy();
}

void AAuraProjectile::MulticastPlayImpactEffects_Implementation(FVector_NetQuantize ImpactLocation)
{
	PlayImpactEffects(ImpactLocation);
}

void AAuraProjectile::PlayImpactEffects(const FVector& ImpactLocation)
{
	if (bHit)
	{
		return;
	}
	bHit = true;
	UGameplayStatics::PlaySoundAtLocation(this, ImpactSound, ImpactLocation, FRotator::ZeroRotator);
	UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		this, ImpactEffect, ImpactLocation, FRotator::ZeroRotator, FVector::OneVector, true, true,
		ENCPoolMethod::AutoRelease);
	if (FlySoundComponent)
	{
		FlySoundComponent->Stop();
	}
}

void AAuraProjectile::StartFlightAudio()
{
	if (FlySound && (!FlySoundComponent || !FlySoundComponent->IsPlaying()))
	{
		FlySoundComponent = UGameplayStatics::SpawnSoundAttached(FlySound, GetRootComponent());
	}
}

void AAuraProjectile::SetTrailEffectsActive(bool bActive)
{
	TInlineComponentArray<UNiagaraComponent*> NiagaraComponents;
	GetComponents(NiagaraComponents);
	for (UNiagaraComponent* NiagaraComponent : NiagaraComponents)
	{
		if (!IsValid(NiagaraComponent))
		{
			continue;
		}

		if (bActive)
		{
			NiagaraComponent->Activate(true);
		}
		else
		{
			NiagaraComponent->DeactivateImmediate();
		}
	}
}

void AAuraProjectile::OnSphereOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!DamageEffectSpecHandle.IsValid() || DamageEffectSpecHandle.Data.Get()->GetContext().GetEffectCauser() == OtherActor)
	{
		return;
	}
	
	if (!UAuraAbilitySystemLibrary::IsNotFriend(OtherActor, DamageEffectSpecHandle.Data.Get()->GetContext().GetEffectCauser()))
	{
		return;
	}
	
	if (HasAuthority())
	{
		if (bHit)
		{
			return;
		}

		FVector ImpactLocation = GetActorLocation();
		if (bFromSweep)
		{
			ImpactLocation = FVector(SweepResult.ImpactPoint);
		}
		MulticastPlayImpactEffects(ImpactLocation);

		if (UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(OtherActor))
		{
			TargetASC->ApplyGameplayEffectSpecToSelf(*DamageEffectSpecHandle.Data.Get());
		}
		
		ReturnToPool();
	}
}
