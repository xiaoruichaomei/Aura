#include "Actor/MagicCircle.h"

#include "Components/DecalComponent.h"

AMagicCircle::AMagicCircle()
{
	bReplicates = false;
	SetActorEnableCollision(false);
	PrimaryActorTick.bCanEverTick = true;
	DecalComponent = CreateDefaultSubobject<UDecalComponent>(TEXT("MagicCircleDecal"));
	SetRootComponent(DecalComponent);
	ApplyDecalSettings();
	DecalComponent->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f));
}

void AMagicCircle::SetDecalMaterial(UMaterialInterface* InMaterial)
{
	DecalComponent->SetDecalMaterial(InMaterial);
}

void AMagicCircle::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyDecalSettings();
}

void AMagicCircle::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!FMath::IsNearlyZero(ClockwiseRotationSpeed))
	{
		AddActorWorldRotation(FRotator(0.f, ClockwiseRotationSpeed * DeltaSeconds, 0.f));
	}
}

void AMagicCircle::ApplyDecalSettings()
{
	if (DecalComponent)
	{
		DecalComponent->DecalSize = DecalSize;
	}
}
