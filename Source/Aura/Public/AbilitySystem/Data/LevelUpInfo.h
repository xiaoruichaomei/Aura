

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "LevelUpInfo.generated.h"

USTRUCT(BlueprintType)
struct FAuraLevelUpInfo
{
	GENERATED_BODY()
	
	UPROPERTY(EditDefaultsOnly)
	int32 LevelUpRequirement = 0;
	
	UPROPERTY(EditDefaultsOnly)
	int32 AttributePointAward = 1;
	
	UPROPERTY(EditDefaultsOnly)
	int32 SpellPointAward = 1;
};

/**
 * 
 */
UCLASS()
class AURA_API ULevelUpInfo : public UDataAsset
{
	GENERATED_BODY()
	
public:
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	int32 FindLevelForXP(int32 XP) const;

	/** Maximum player level. Intermediate level entries are generated automatically. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Auto Generation", meta=(ClampMin="1"))
	int32 MaxLevel = 10;

	/** XP required to reach level 2. Level 1 starts at 0 XP. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Auto Generation", meta=(ClampMin="1"))
	int32 FirstLevelUpXP = 100;

	/** Multiplier applied to the previous level's XP requirement. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Auto Generation", meta=(ClampMin="1.0"))
	float XPRequirementMultiplier = 1.25f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Auto Generation", meta=(ClampMin="0"))
	int32 AttributePointAwardPerLevel = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Auto Generation", meta=(ClampMin="0"))
	int32 SpellPointAwardPerLevel = 1;

	/** Keeps the legacy array synchronized with the configuration above. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Auto Generation")
	bool bAutoGenerateLevelUpInfos = true;

	/** Generated entries. Index 0 is reserved as the level-1 placeholder. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Generated Data")
	TArray<FAuraLevelUpInfo> LevelUpInfos;

private:
	void GenerateLevelUpInfos();
};
