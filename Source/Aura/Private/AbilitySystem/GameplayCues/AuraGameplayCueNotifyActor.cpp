// Fill out your copyright notice in the Description page of Project Settings.

#include "AbilitySystem/GameplayCues/AuraGameplayCueNotifyActor.h"

#include "AbilitySystemComponent.h"
#include "Character/BaseCharacter.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Pawn.h"
#include "NiagaraComponent.h"
#include "Player/AuraPlayerController.h"

namespace
{
	FVector GetBeamAnchorLocation(const AActor* Actor)
	{
		if (const ABaseCharacter* Character = Cast<ABaseCharacter>(Actor))
		{
			if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
			{
				return Capsule->GetComponentLocation() + FVector::UpVector * Capsule->GetScaledCapsuleHalfHeight() * 0.25f;
			}
		}
		return Actor ? Actor->GetActorLocation() : FVector::ZeroVector;
	}
}

AAuraGameplayCueNotifyActor::AAuraGameplayCueNotifyActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bAutoDestroyOnRemove = true;
	NumPreallocatedInstances = 8;

	BeamNiagaraComponent = CreateDefaultSubobject<UNiagaraComponent>("BeamNiagaraComponent");
	SetRootComponent(BeamNiagaraComponent);
	BeamNiagaraComponent->bAutoActivate = false;

	LoopingSoundComponent = CreateDefaultSubobject<UAudioComponent>("LoopingSoundComponent");
	LoopingSoundComponent->SetupAttachment(BeamNiagaraComponent);
	LoopingSoundComponent->bAutoActivate = false;
}

bool AAuraGameplayCueNotifyActor::WhileActive_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters)
{
	bCueActive = true;

	AActor* SourceActor = nullptr;
	if (UAbilitySystemComponent* SourceASC = Parameters.EffectContext.GetInstigatorAbilitySystemComponent())
	{
		SourceActor = SourceASC->GetAvatarActor();
	}
	if (!SourceActor)
	{
		SourceActor = Parameters.Instigator.Get();
	}
	BeamSourceActor = SourceActor;

	BeamTargetActor = IsValid(MyTarget) && MyTarget != SourceActor ? MyTarget : nullptr;
	bUsesCursorEnd = !BeamTargetActor.IsValid();
	FixedBeamEnd = BeamTargetActor.IsValid() ? GetBeamAnchorLocation(BeamTargetActor.Get()) : FVector(Parameters.Location);

	if (BeamNiagaraComponent)
	{
		// Cue actors are pooled, so a previous staff attachment must not leak into a chain segment.
		BeamNiagaraComponent->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		BeamStartActor = nullptr;
		FixedBeamStart = Parameters.Location;

		if (USceneComponent* AttachTarget = Parameters.TargetAttachComponent.Get())
		{
			BeamNiagaraComponent->AttachToComponent(
				AttachTarget,
				FAttachmentTransformRules::SnapToTargetNotIncludingScale,
				FName("TipSocket"));
		}
		else
		{
			BeamStartActor = Cast<AActor>(const_cast<UObject*>(Parameters.SourceObject.Get()));
			if (BeamStartActor.IsValid())
			{
				FixedBeamStart = GetBeamAnchorLocation(BeamStartActor.Get());
			}
			BeamNiagaraComponent->SetWorldLocation(FixedBeamStart);
		}

		BeamNiagaraComponent->SetVectorParameter(FName("Beam Start"), FVector::ZeroVector);
		BeamNiagaraComponent->SetVectorParameter(FName("Beam End"), FixedBeamEnd);
		BeamNiagaraComponent->Activate(true);
	}
	if (LoopingSoundComponent)
	{
		LoopingSoundComponent->Play();
	}
	return true;
}

void AAuraGameplayCueNotifyActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bCueActive || !BeamNiagaraComponent)
	{
		return;
	}

	// Finite Niagara/audio assets are restarted while the persistent cue remains active.
	if (!BeamNiagaraComponent->IsActive())
	{
		BeamNiagaraComponent->Activate(true);
	}
	if (LoopingSoundComponent && !LoopingSoundComponent->IsPlaying())
	{
		LoopingSoundComponent->Play();
	}

	if (BeamStartActor.IsValid())
	{
		FixedBeamStart = GetBeamAnchorLocation(BeamStartActor.Get());
		BeamNiagaraComponent->SetWorldLocation(FixedBeamStart);
	}

	if (BeamTargetActor.IsValid())
	{
		FixedBeamEnd = GetBeamAnchorLocation(BeamTargetActor.Get());
	}
	else if (bUsesCursorEnd)
	{
		if (const APawn* SourcePawn = Cast<APawn>(BeamSourceActor.Get()))
		{
			FVector CursorLocation;
			if (const AAuraPlayerController* PlayerController = Cast<AAuraPlayerController>(SourcePawn->GetController());
				PlayerController && PlayerController->GetBeamCursorLocation(CursorLocation))
			{
				FixedBeamEnd = CursorLocation;
			}
		}
	}
	// Always publish only the latest endpoint. Endpoint interpolation makes overlapping
	// Niagara beam particles fan across every intermediate direction during fast aiming.
	BeamNiagaraComponent->SetVectorParameter(FName("Beam End"), FixedBeamEnd);
}

bool AAuraGameplayCueNotifyActor::OnRemove_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters)
{
	bCueActive = false;
	if (BeamNiagaraComponent)
	{
		BeamNiagaraComponent->DeactivateImmediate();
		BeamNiagaraComponent->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	}
	if (LoopingSoundComponent)
	{
		LoopingSoundComponent->Stop();
	}

	BeamTargetActor = nullptr;
	BeamStartActor = nullptr;
	BeamSourceActor = nullptr;
	bUsesCursorEnd = false;
	return true;
}

bool AAuraGameplayCueNotifyActor::Recycle()
{
	bCueActive = false;
	SetActorTickEnabled(false);
	if (BeamNiagaraComponent)
	{
		BeamNiagaraComponent->DeactivateImmediate();
		BeamNiagaraComponent->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	}
	if (LoopingSoundComponent)
	{
		LoopingSoundComponent->Stop();
	}
	BeamTargetActor = nullptr;
	BeamStartActor = nullptr;
	BeamSourceActor = nullptr;
	bUsesCursorEnd = false;
	return Super::Recycle();
}

void AAuraGameplayCueNotifyActor::ReuseAfterRecycle()
{
	Super::ReuseAfterRecycle();
	bCueActive = false;
	SetActorTickEnabled(true);
}
