#pragma once

#include "CoreMinimal.h"
#include "Pipeline/FOsmRawData.h"
#include "Pipeline/FOsmCategorizer.h"
#include "Pipeline/FOsmImportSettings.h"
#include "Coord/FOsmCoordinateConverter.h"
#include "FOsmImportPipeline.generated.h"

class UWorld;
class UOsm2MapTerrainGenerator;

DECLARE_LOG_CATEGORY_EXTERN(LogOsm2Map, Log, All);

/**
 * Main orchestrator for the OSM import process.
 * Manages the full pipeline from parsing OSM XML to spawning UE actors.
 */
UCLASS(BlueprintType)
class OSM2MAP_API UOsmImportPipeline : public UObject
{
	GENERATED_BODY()

public:
	/** Create a new import pipeline instance for Blueprint or Python-driven workflows */
	UFUNCTION(BlueprintCallable, Category = "Osm2Map")
	static UOsmImportPipeline* NewPipeline();

	/** Execute the full import pipeline in the given world */
	UFUNCTION(BlueprintCallable, Category = "Osm2Map")
	bool Execute(UWorld* World, const FOsmImportSettings& Settings);

	/** Get the parsed raw data (available after Stage 1) */
	const FOsmRawData& GetRawData() const { return RawData; }

	/** Get the categories (available after Stage 2) */
	const FOsmCategories& GetCategories() const { return Categories; }

	/** Get the coordinate converter (available after Stage 3) */
	const FOsmCoordinateConverter& GetCoordinateConverter() const { return CoordConverter; }

	/** Get projected node positions in osm2xodr local meters (available after Stage 3) */
	const TMap<int64, FVector2D>& GetProjectedNodes() const { return ProjectedNodes; }

private:
	/** Stage 1: Parse OSM XML file */
	bool ParseOsmFile(const FString& FilePath);

	/** Stage 2: Categorize OSM data */
	bool CategorizeData();

	/** Stage 3: Project all coordinates */
	bool ProjectCoordinates(float WorldScale);

	/** Stage 4: Generate terrain landscape */
	bool GenerateTerrain(UWorld* World, const FOsmImportSettings& Settings);

	/** Stage 5: Generate road meshes */
	bool GenerateRoads(UWorld* World, const FOsmImportSettings& Settings);

	/** Stage 6: Generate building meshes */
	bool GenerateBuildings(UWorld* World, const FOsmImportSettings& Settings);

	/** Stage 7: Place assets (trees, lamps, etc.) */
	bool PlaceAssets(UWorld* World, const FOsmImportSettings& Settings);

	FOsmRawData RawData;
	FOsmCategories Categories;
	FOsmCoordinateConverter CoordConverter;

	/** Projected node positions in osm2xodr local meters (X=east, Y=north) */
	TMap<int64, FVector2D> ProjectedNodes;

	/** Terrain generator (kept alive for elevation queries by later stages) */
	UPROPERTY()
	TObjectPtr<UOsm2MapTerrainGenerator> TerrainGenerator;
};
