


#include "AbilitySystem/Data/LevelUpInfo.h"

void ULevelUpInfo::PostLoad()
{
	Super::PostLoad();

	// Preserve existing hand-authored tables. Changing MaxLevel causes the
	// missing intermediate entries to be generated automatically.
	if (bAutoGenerateLevelUpInfos && LevelUpInfos.Num() != FMath::Max(1, MaxLevel) + 1)
	{
		GenerateLevelUpInfos();
	}
}

#if WITH_EDITOR
void ULevelUpInfo::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (bAutoGenerateLevelUpInfos)
	{
		GenerateLevelUpInfos();
		MarkPackageDirty();
	}
}
#endif

void ULevelUpInfo::GenerateLevelUpInfos()
{
	const int32 SafeMaxLevel = FMath::Max(1, MaxLevel);
	LevelUpInfos.SetNum(SafeMaxLevel + 1);

	LevelUpInfos[0].LevelUpRequirement = 0;
	LevelUpInfos[0].AttributePointAward = 0;
	LevelUpInfos[0].SpellPointAward = 0;

	for (int32 Level = 1; Level <= SafeMaxLevel; ++Level)
	{
		FAuraLevelUpInfo& Info = LevelUpInfos[Level];
		Info.LevelUpRequirement = Level == 1
			? 0
			: FMath::Max(1, FMath::RoundToInt(static_cast<float>(FirstLevelUpXP) *
				FMath::Pow(FMath::Max(1.f, XPRequirementMultiplier), Level - 2)));
		Info.AttributePointAward = FMath::Max(0, AttributePointAwardPerLevel);
		Info.SpellPointAward = FMath::Max(0, SpellPointAwardPerLevel);
	}
}

int32 ULevelUpInfo::FindLevelForXP(int32 XP) const
{
	if (LevelUpInfos.Num() <= 1)
	{
		return 1;
	}

	int32 Level = 1;
	bool bSearching = true;
	while (bSearching)
	{
		// LevelUpInfos[1] = Level 1 Information
		if (LevelUpInfos.Num() - 1 <= Level)
		{
			return Level;
		}
		if (XP >= LevelUpInfos[Level].LevelUpRequirement)
		{
			++Level;
		}
		else
		{
			bSearching = false;
		}
	}
	return Level;
}
