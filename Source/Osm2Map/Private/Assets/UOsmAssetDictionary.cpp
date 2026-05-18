#include "Assets/UOsmAssetDictionary.h"

int32 UOsmAssetDictionary::FindMatchingRule(const TMap<FString, FString>& Tags) const
{
	int32 BestIndex = INDEX_NONE;
	int32 BestPriority = TNumericLimits<int32>::Min();

	for (int32 i = 0; i < Rules.Num(); ++i)
	{
		const FOsmAssetMappingRule& Rule = Rules[i];

		if (Rule.Priority <= BestPriority && BestIndex != INDEX_NONE)
		{
			continue; // Already have a better match
		}

		// Check primary tag
		const FString* TagValue = Tags.Find(Rule.TagKey);
		if (!TagValue) continue;

		// If TagValue is specified, it must match
		if (!Rule.TagValue.IsEmpty() && *TagValue != Rule.TagValue)
		{
			continue;
		}

		// Check additional tags
		bool bAllAdditionalMatch = true;
		for (const auto& [Key, Value] : Rule.AdditionalTags)
		{
			const FString* ActualValue = Tags.Find(Key);
			if (!ActualValue || *ActualValue != Value)
			{
				bAllAdditionalMatch = false;
				break;
			}
		}
		if (!bAllAdditionalMatch) continue;

		// Check that the rule has at least one mesh variant
		if (Rule.MeshVariants.Num() == 0) continue;

		BestIndex = i;
		BestPriority = Rule.Priority;
	}

	return BestIndex;
}

void UOsmAssetDictionary::PopulateDefaults()
{
	Rules.Empty();

	// Trees
	{
		FOsmAssetMappingRule& Rule = Rules.AddDefaulted_GetRef();
		Rule.TagKey = TEXT("natural");
		Rule.TagValue = TEXT("tree");
		Rule.ScaleRange = FVector2D(0.8f, 1.2f);
		Rule.YawRange = FVector2D(0.0f, 360.0f);
		Rule.Priority = 10;
		Rule.bUseInstancing = true;
		Rule.bUseFoliageSystem = true;
	}

	// Street lamps
	{
		FOsmAssetMappingRule& Rule = Rules.AddDefaulted_GetRef();
		Rule.TagKey = TEXT("highway");
		Rule.TagValue = TEXT("street_lamp");
		Rule.ScaleRange = FVector2D(1.0f, 1.0f);
		Rule.YawRange = FVector2D(0.0f, 360.0f);
		Rule.Priority = 10;
		Rule.bUseInstancing = true;
	}

	// Traffic signals
	{
		FOsmAssetMappingRule& Rule = Rules.AddDefaulted_GetRef();
		Rule.TagKey = TEXT("highway");
		Rule.TagValue = TEXT("traffic_signals");
		Rule.ScaleRange = FVector2D(1.0f, 1.0f);
		Rule.YawRange = FVector2D(0.0f, 0.0f); // Oriented to road
		Rule.Priority = 10;
		Rule.bUseInstancing = false;
	}

	// Benches
	{
		FOsmAssetMappingRule& Rule = Rules.AddDefaulted_GetRef();
		Rule.TagKey = TEXT("amenity");
		Rule.TagValue = TEXT("bench");
		Rule.ScaleRange = FVector2D(1.0f, 1.0f);
		Rule.YawRange = FVector2D(0.0f, 0.0f); // Oriented to road
		Rule.Priority = 10;
		Rule.bUseInstancing = true;
	}

	// Waste baskets
	{
		FOsmAssetMappingRule& Rule = Rules.AddDefaulted_GetRef();
		Rule.TagKey = TEXT("amenity");
		Rule.TagValue = TEXT("waste_basket");
		Rule.ScaleRange = FVector2D(0.9f, 1.1f);
		Rule.YawRange = FVector2D(0.0f, 360.0f);
		Rule.Priority = 10;
		Rule.bUseInstancing = true;
	}

	// Bollards
	{
		FOsmAssetMappingRule& Rule = Rules.AddDefaulted_GetRef();
		Rule.TagKey = TEXT("barrier");
		Rule.TagValue = TEXT("bollard");
		Rule.ScaleRange = FVector2D(1.0f, 1.0f);
		Rule.YawRange = FVector2D(0.0f, 360.0f);
		Rule.Priority = 10;
		Rule.bUseInstancing = true;
	}

	// Fire hydrants
	{
		FOsmAssetMappingRule& Rule = Rules.AddDefaulted_GetRef();
		Rule.TagKey = TEXT("emergency");
		Rule.TagValue = TEXT("fire_hydrant");
		Rule.ScaleRange = FVector2D(1.0f, 1.0f);
		Rule.YawRange = FVector2D(0.0f, 360.0f);
		Rule.Priority = 10;
		Rule.bUseInstancing = true;
	}

	// Bus stops
	{
		FOsmAssetMappingRule& Rule = Rules.AddDefaulted_GetRef();
		Rule.TagKey = TEXT("highway");
		Rule.TagValue = TEXT("bus_stop");
		Rule.ScaleRange = FVector2D(1.0f, 1.0f);
		Rule.YawRange = FVector2D(0.0f, 0.0f);
		Rule.Priority = 10;
		Rule.bUseInstancing = false;
	}

	// Guide posts
	{
		FOsmAssetMappingRule& Rule = Rules.AddDefaulted_GetRef();
		Rule.TagKey = TEXT("information");
		Rule.TagValue = TEXT("guidepost");
		Rule.ScaleRange = FVector2D(1.0f, 1.0f);
		Rule.YawRange = FVector2D(0.0f, 360.0f);
		Rule.Priority = 5;
		Rule.bUseInstancing = true;
	}
}
