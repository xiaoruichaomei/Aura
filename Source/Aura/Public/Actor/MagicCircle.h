#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MagicCircle.generated.h"

class UDecalComponent;
class UMaterialInterface;

UCLASS()
class AURA_API AMagicCircle : public AActor
{
	GENERATED_BODY()

public:
	AMagicCircle();
	void SetDecalMaterial(UMaterialInterface* InMaterial);
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;

protected:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UDecalComponent> DecalComponent;

	/** Y and Z control the projected magic-circle size. X is ignored in favor of ProjectionDepth. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Magic Circle", meta=(ClampMin="1.0"))
	FVector DecalSize = FVector(256.f, 350.f, 350.f);

	/** Positive values rotate the circle clockwise when viewed from above. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Magic Circle|Animation", meta=(ClampMin="0.0"))
	float ClockwiseRotationSpeed = 45.f;

private:
	void ApplyDecalSettings();
};
