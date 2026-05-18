#pragma once

#include "CoreMinimal.h"
#include "FOsmImportSettings.generated.h"

class UOsmAssetDictionary;

USTRUCT(BlueprintType)
struct OSM2MAP_API FOsmImportSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM Import")
	FString OsmFilePath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM Import|Terrain")
	FString ElevationDataPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM Import")
	float WorldScale = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM Import|Terrain")
	bool bImportTerrain = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM Import|Roads")
	bool bImportRoads = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM Import|Buildings")
	bool bImportBuildings = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM Import|Assets")
	bool bImportAssets = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM Import|Terrain", meta = (ClampMin = "127", ClampMax = "8129"))
	int32 LandscapeResolution = 1009;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM Import|Buildings", meta = (ClampMin = "3.0", ClampMax = "100.0"))
	float DefaultBuildingHeight = 9.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM Import|Roads", meta = (ClampMin = "0.0", ClampMax = "50.0"))
	float RoadZOffset = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM Import|Assets")
	TSoftObjectPtr<UOsmAssetDictionary> AssetDictionary;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OSM Import")
	bool bUseWorldPartition = false;
};
