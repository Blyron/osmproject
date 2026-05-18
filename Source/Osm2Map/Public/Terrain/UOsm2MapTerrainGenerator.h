#pragma once

#include "CoreMinimal.h"
#include "Terrain/FElevationDataProvider.h"
#include "Pipeline/FOsmRawData.h"
#include "Pipeline/FOsmCategorizer.h"
#include "Coord/FOsmCoordinateConverter.h"
#include "UOsm2MapTerrainGenerator.generated.h"

class ALandscape;
class UWorld;

/**
 * Generates UE Landscape from elevation data and sculpts natural features.
 * - Loads SRTM heightmaps for terrain elevation
 * - Carves rivers as indentations
 * - Flattens water body areas
 * - Paints landscape material layers for parks, forests, etc.
 */
UCLASS(BlueprintType)
class OSM2MAP_API UOsm2MapTerrainGenerator : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Generate the landscape in the given world.
	 * @param World Target world
	 * @param RawData Full OSM data
	 * @param Categories Categorized OSM indices
	 * @param ProjectedNodes Node positions in osm2xodr local meters
	 * @param CoordConverter Coordinate converter
	 * @param ElevationPath Directory containing .hgt SRTM files
	 * @param LandscapeResolution Target heightmap resolution (e.g. 1009)
	 * @return The created landscape actor, or nullptr on failure
	 */
	ALandscape* Generate(
		UWorld* World,
		const FOsmRawData& RawData,
		const FOsmCategories& Categories,
		const TMap<int64, FVector2D>& ProjectedNodes,
		const FOsmCoordinateConverter& CoordConverter,
		const FString& ElevationPath,
		int32 LandscapeResolution
	);

	/** Get elevation at osm2xodr local coordinates (meters). Available after Generate(). */
	float GetElevationAtOsmXY(double OsmX, double OsmY) const;

	const FElevationDataProvider& GetElevationProvider() const { return ElevationProvider; }

private:
	/** Carve river indentations into the heightmap */
	void CarveRivers(TArray<uint16>& Heightmap, int32 Resolution,
		const FOsmRawData& RawData, const FOsmCategories& Categories,
		const TMap<int64, FVector2D>& ProjectedNodes,
		double MinOsmX, double MaxOsmX, double MinOsmY, double MaxOsmY);

	/** Flatten water body areas in the heightmap */
	void FlattenWaterBodies(TArray<uint16>& Heightmap, int32 Resolution,
		const FOsmRawData& RawData, const FOsmCategories& Categories,
		const TMap<int64, FVector2D>& ProjectedNodes,
		double MinOsmX, double MaxOsmX, double MinOsmY, double MaxOsmY);

	/** Point-in-polygon test */
	static bool IsPointInPolygon(const FVector2D& Point, const TArray<FVector2D>& Polygon);

	/** Get default river width from waterway type */
	static float GetDefaultRiverWidth(const FString& WaterwayType);

	FElevationDataProvider ElevationProvider;
	double StoredMinLat = 0, StoredMaxLat = 0, StoredMinLon = 0, StoredMaxLon = 0;
};
