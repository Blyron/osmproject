#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/StaticMesh.h"
#include "UOsmAssetDictionary.generated.h"

/**
 * A single mapping rule from OSM tags to UE mesh assets.
 */
USTRUCT(BlueprintType)
struct OSM2MAP_API FOsmAssetMappingRule
{
	GENERATED_BODY()

	/** OSM tag key to match (e.g., "natural", "amenity", "highway") */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mapping")
	FString TagKey;

	/** OSM tag value to match (e.g., "tree", "bench", "street_lamp"). Empty = match any value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mapping")
	FString TagValue;

	/** Additional tag requirements for more specific matching */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mapping")
	TMap<FString, FString> AdditionalTags;

	/** Mesh variants to randomly choose from when placing */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asset")
	TArray<TSoftObjectPtr<UStaticMesh>> MeshVariants;

	/** Random scale range (min, max) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	FVector2D ScaleRange = FVector2D(0.9f, 1.1f);

	/** Random yaw rotation range in degrees (min, max) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement")
	FVector2D YawRange = FVector2D(0.0f, 360.0f);

	/** Vertical offset from ground (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement")
	float ZOffset = 0.0f;

	/** Higher priority rules override lower ones */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mapping")
	int32 Priority = 0;

	/** Use instanced rendering for performance (recommended for frequent assets) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
	bool bUseInstancing = true;

	/** Use UE Foliage system instead of HISM (better for trees with wind animation) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
	bool bUseFoliageSystem = false;
};

/**
 * Data asset that defines the mapping from OSM tags to UE mesh assets.
 * Assign different dictionaries for different visual styles or level of detail.
 */
UCLASS(BlueprintType)
class OSM2MAP_API UOsmAssetDictionary : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Mapping rules, evaluated in priority order (highest first) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asset Dictionary")
	TArray<FOsmAssetMappingRule> Rules;

	/** Export the current mapping rules to a JSON file for external editing. */
	UFUNCTION(BlueprintCallable, Category = "Asset Dictionary")
	bool ExportToJson(const FString& FilePath) const;

	/** Import mapping rules from a JSON file. Existing rules are replaced on success. */
	UFUNCTION(BlueprintCallable, Category = "Asset Dictionary")
	bool ImportFromJson(const FString& FilePath);

	/**
	 * Find the best matching rule for a set of OSM tags.
	 * @return Index into Rules array, or INDEX_NONE if no match.
	 */
	int32 FindMatchingRule(const TMap<FString, FString>& Tags) const;

	/** Populate with default OSM tag mappings (trees, lamps, benches, etc.) */
	UFUNCTION(BlueprintCallable, Category = "Asset Dictionary")
	void PopulateDefaults();
};
